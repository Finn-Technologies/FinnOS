// Native POSIX PTY + VT100/ANSI Qt Quick terminal renderer implementation.

#include "terminal_view.hpp"
#include <FinnSDK/FinnSDK.hpp>

#include <QClipboard>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

namespace finn::apps {

FinnTerminalView::FinnTerminalView(QQuickItem* parent)
    : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setFocus(true);
    setActiveFocusOnTab(true);

#ifdef __APPLE__
    font_ = QFont(QStringLiteral("Menlo"));
#else
    font_ = QFont(QStringLiteral("JetBrains Mono"));
#endif
    font_.setStyleHint(QFont::Monospace);
    font_.setFixedPitch(true);
    font_.setPixelSize(font_size_);
    updateMetrics();
    resizeGrid(80, 24);
}

FinnTerminalView::~FinnTerminalView() {
    if (notifier_) {
        notifier_->setEnabled(false);
        delete notifier_;
        notifier_ = nullptr;
    }
    if (pty_fd_ >= 0) {
        ::close(pty_fd_);
        pty_fd_ = -1;
    }
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGHUP);
        ::waitpid(child_pid_, nullptr, WNOHANG);
        child_pid_ = -1;
    }
}

void FinnTerminalView::setFontSize(int px) {
    const int clamped = std::clamp(px, 10, 28);
    if (clamped == font_size_) return;
    font_size_ = clamped;
    font_.setPixelSize(font_size_);
    updateMetrics();
    const int nc = std::max(20, static_cast<int>((width() - 20) / cell_w_));
    const int nr = std::max(6, static_cast<int>((height() - 16) / cell_h_));
    resizeGrid(nc, nr);
    emit fontSizeChanged();
    update();
}

void FinnTerminalView::updateMetrics() {
    QFontMetrics fm(font_);
    cell_w_ = std::max(6, fm.horizontalAdvance(QLatin1Char('M')));
    cell_h_ = std::max(14, fm.height() + 2);
}

void FinnTerminalView::resizeGrid(int new_cols, int new_rows) {
    new_cols = std::clamp(new_cols, 20, 240);
    new_rows = std::clamp(new_rows, 6, 120);
    if (new_cols == cols_ && new_rows == rows_ && grid_.size() == rows_) return;

    QVector<QVector<Cell>> next(new_rows, QVector<Cell>(new_cols));
    const int copy_r = std::min(new_rows, static_cast<int>(grid_.size()));
    for (int r = 0; r < copy_r; ++r) {
        const int copy_c = std::min(new_cols, static_cast<int>(grid_[r].size()));
        for (int c = 0; c < copy_c; ++c) {
            next[r][c] = grid_[r][c];
        }
    }
    grid_ = std::move(next);
    cols_ = new_cols;
    rows_ = new_rows;
    cur_x_ = std::clamp(cur_x_, 0, cols_ - 1);
    cur_y_ = std::clamp(cur_y_, 0, rows_ - 1);

    if (pty_fd_ >= 0) {
        struct winsize ws {};
        ws.ws_col = static_cast<unsigned short>(cols_);
        ws.ws_row = static_cast<unsigned short>(rows_);
        ::ioctl(pty_fd_, TIOCSWINSZ, &ws);
    }
}

void FinnTerminalView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() > 32 && newGeometry.height() > 32) {
        const int nc = std::max(20, static_cast<int>((newGeometry.width() - 20) / cell_w_));
        const int nr = std::max(6, static_cast<int>((newGeometry.height() - 16) / cell_h_));
        resizeGrid(nc, nr);
        update();
    }
}

void FinnTerminalView::startShell(const QString& workdir) {
    if (pty_fd_ >= 0) return;

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols_);
    ws.ws_row = static_cast<unsigned short>(rows_);

    int master_fd = -1;
    const pid_t pid = ::forkpty(&master_fd, nullptr, nullptr, &ws);
    if (pid < 0) {
        feedBytes("finn-terminal: failed to allocate PTY\r\n");
        return;
    }
    if (pid == 0) {
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        const QString dir = workdir.isEmpty() ? QDir::homePath() : workdir;
        (void)::chdir(dir.toLocal8Bit().constData());
        const char* sh = std::getenv("SHELL");
        if (!sh || !*sh) sh = "/bin/sh";
        ::execl(sh, sh, "-i", nullptr);
        ::_exit(127);
    }

    pty_fd_ = master_fd;
    child_pid_ = pid;
    const int flags = ::fcntl(pty_fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(pty_fd_, F_SETFL, flags | O_NONBLOCK);

    notifier_ = new QSocketNotifier(pty_fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &FinnTerminalView::readFromPty);

    const char* sh_env = std::getenv("SHELL");
    title_ = sh_env ? QString::fromLocal8Bit(sh_env).section(QLatin1Char('/'), -1) : QStringLiteral("sh");
    emit titleChanged();
    emit runningChanged();
}

void FinnTerminalView::readFromPty() {
    if (pty_fd_ < 0) return;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(pty_fd_, buf, sizeof(buf));
        if (n > 0) {
            feedBytes(QByteArray(buf, static_cast<int>(n)));
        } else if (n == 0) {
            notifier_->setEnabled(false);
            ::close(pty_fd_);
            pty_fd_ = -1;
            int status = 0;
            if (child_pid_ > 0) {
                ::waitpid(child_pid_, &status, WNOHANG);
                child_pid_ = -1;
            }
            emit runningChanged();
            emit shellExited(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
            break;
        } else {
            break;
        }
    }
    update();
}

void FinnTerminalView::writePty(const QByteArray& bytes) {
    if (pty_fd_ < 0 || bytes.isEmpty()) return;
    (void)::write(pty_fd_, bytes.constData(), static_cast<size_t>(bytes.size()));
}

void FinnTerminalView::sendText(const QString& text) {
    writePty(text.toUtf8());
}

void FinnTerminalView::sendControl(const QString& key) {
    if (key == QLatin1String("esc")) writePty("\x1b");
    else if (key == QLatin1String("tab")) writePty("\t");
    else if (key == QLatin1String("ctrl_c")) writePty("\x03");
    else if (key == QLatin1String("ctrl_d")) writePty("\x04");
    else if (key == QLatin1String("ctrl_l")) writePty("\x0c");
    else if (key == QLatin1String("up")) writePty("\x1b[A");
    else if (key == QLatin1String("down")) writePty("\x1b[B");
    else if (key == QLatin1String("left")) writePty("\x1b[D");
    else if (key == QLatin1String("right")) writePty("\x1b[C");
}

void FinnTerminalView::clearScreen() {
    for (auto& row : grid_) {
        for (auto& cell : row) cell = Cell{};
    }
    cur_x_ = 0;
    cur_y_ = 0;
    update();
}

void FinnTerminalView::feedBytes(const QByteArray& data) {
    const QString text = QString::fromUtf8(data);
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if (state_ == ParseState::Osc) {
            if (u == 0x07) {
                const int semi = esc_buf_.indexOf(QLatin1Char(';'));
                if (semi >= 0) {
                    title_ = esc_buf_.mid(semi + 1).trimmed();
                    emit titleChanged();
                }
                esc_buf_.clear();
                state_ = ParseState::Ground;
            } else if (u == 0x1b) {
                esc_buf_.clear();
                state_ = ParseState::Ground;
            } else if (esc_buf_.size() < 256) {
                esc_buf_.append(ch);
            }
            continue;
        }
        if (state_ == ParseState::Esc) {
            if (ch == QLatin1Char('[')) {
                esc_buf_.clear();
                state_ = ParseState::Csi;
            } else if (ch == QLatin1Char(']')) {
                esc_buf_.clear();
                state_ = ParseState::Osc;
            } else {
                state_ = ParseState::Ground;
            }
            continue;
        }
        if (state_ == ParseState::Csi) {
            if (u >= 0x40 && u <= 0x7E) {
                handleCsi(static_cast<char>(u), esc_buf_);
                esc_buf_.clear();
                state_ = ParseState::Ground;
            } else if (esc_buf_.size() < 64) {
                esc_buf_.append(ch);
            }
            continue;
        }

        if (u == 0x1b) {
            state_ = ParseState::Esc;
        } else if (u == '\r') {
            cur_x_ = 0;
        } else if (u == '\n') {
            newLine();
        } else if (u == '\b') {
            if (cur_x_ > 0) --cur_x_;
        } else if (u == '\t') {
            cur_x_ = std::min(cols_ - 1, ((cur_x_ / 8) + 1) * 8);
        } else if (u >= 0x20) {
            putChar(ch);
        }
    }
}

void FinnTerminalView::putChar(QChar ch) {
    if (cur_y_ < 0 || cur_y_ >= rows_) return;
    if (cur_x_ >= cols_) {
        cur_x_ = 0;
        newLine();
    }
    grid_[cur_y_][cur_x_] = Cell{ch, cur_fg_, cur_bold_};
    ++cur_x_;
}

void FinnTerminalView::newLine() {
    if (cur_y_ + 1 < rows_) {
        ++cur_y_;
    } else {
        scrollUp();
    }
}

void FinnTerminalView::scrollUp() {
    if (rows_ <= 1) return;
    scrollback_.append(grid_[0]);
    if (scrollback_.size() > 2000) {
        scrollback_.removeFirst();
    }
    for (int r = 1; r < rows_; ++r) {
        grid_[r - 1] = std::move(grid_[r]);
    }
    grid_[rows_ - 1] = QVector<Cell>(cols_);
}

QColor FinnTerminalView::ansiColor(int idx, bool bright) const {
    static const QColor kNormal[8] = {
        QColor(0x1E, 0x20, 0x26), QColor(0xE0, 0x52, 0x63),
        QColor(0x2E, 0xB8, 0x72), QColor(0xE5, 0xA9, 0x3C),
        QColor(0x3B, 0x82, 0xF6), QColor(0xA8, 0x55, 0xF7),
        QColor(0x14, 0xB8, 0xA6), QColor(0xD4, 0xD8, 0xE2),
    };
    static const QColor kBright[8] = {
        QColor(0x6B, 0x72, 0x80), QColor(0xF8, 0x71, 0x71),
        QColor(0x4A, 0xDE, 0x80), QColor(0xFA, 0xCC, 0x15),
        QColor(0x60, 0xA5, 0xFA), QColor(0xC0, 0x84, 0xFC),
        QColor(0x2D, 0xD4, 0xBF), QColor(0xFF, 0xFF, 0xFF),
    };
    const int clamped = std::clamp(idx, 0, 7);
    return bright ? kBright[clamped] : kNormal[clamped];
}

void FinnTerminalView::handleCsi(char final_ch, const QString& params) {
    QString clean = params;
    if (clean.startsWith(QLatin1Char('?'))) clean = clean.mid(1);
    const QStringList parts = clean.isEmpty() ? QStringList{} : clean.split(QLatin1Char(';'));
    auto arg = [&](int idx, int def) {
        if (idx >= parts.size() || parts[idx].isEmpty()) return def;
        bool ok = false;
        const int v = parts[idx].toInt(&ok);
        return ok ? v : def;
    };

    switch (final_ch) {
        case 'A': cur_y_ = std::max(0, cur_y_ - std::max(1, arg(0, 1))); break;
        case 'B': cur_y_ = std::min(rows_ - 1, cur_y_ + std::max(1, arg(0, 1))); break;
        case 'C': cur_x_ = std::min(cols_ - 1, cur_x_ + std::max(1, arg(0, 1))); break;
        case 'D': cur_x_ = std::max(0, cur_x_ - std::max(1, arg(0, 1))); break;
        case 'H':
        case 'f':
            cur_y_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
            cur_x_ = std::clamp(arg(1, 1) - 1, 0, cols_ - 1);
            break;
        case 'J': {
            const int mode = arg(0, 0);
            if (mode == 2 || mode == 3) {
                for (auto& r : grid_) for (auto& c : r) c = Cell{};
                cur_x_ = 0;
                cur_y_ = 0;
            }
            break;
        }
        case 'K': {
            const int mode = arg(0, 0);
            if (cur_y_ >= 0 && cur_y_ < rows_) {
                const int start = (mode == 0) ? cur_x_ : 0;
                const int end = (mode == 1) ? cur_x_ : cols_;
                for (int c = start; c < std::min(end, cols_); ++c) {
                    grid_[cur_y_][c] = Cell{};
                }
            }
            break;
        }
        case 'm': {
            if (parts.isEmpty()) {
                cur_fg_ = QColor();
                cur_bold_ = false;
                break;
            }
            for (const QString& p : parts) {
                const int code = p.toInt();
                if (code == 0) { cur_fg_ = QColor(); cur_bold_ = false; }
                else if (code == 1) cur_bold_ = true;
                else if (code == 22) cur_bold_ = false;
                else if (code >= 30 && code <= 37) cur_fg_ = ansiColor(code - 30, false);
                else if (code >= 90 && code <= 97) cur_fg_ = ansiColor(code - 90, true);
                else if (code == 39) cur_fg_ = QColor();
            }
            break;
        }
        default: break;
    }
}

void FinnTerminalView::keyPressEvent(QKeyEvent* event) {
    const int k = event->key();
    const auto mods = event->modifiers();
    // Konsole-style Ctrl+Shift+V paste & Ctrl+Shift+C copy
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        if (k == Qt::Key_V) {
            if (QClipboard* cb = QGuiApplication::clipboard()) {
                const QString clip = cb->text();
                if (!clip.isEmpty()) {
                    scroll_offset_ = 0;
                    writePty(clip.toUtf8());
                }
            }
            event->accept();
            return;
        }
        if (k == Qt::Key_C) {
            QString text;
            for (const auto& row : grid_) {
                QString line;
                for (const auto& cell : row) line.append(cell.ch);
                while (line.endsWith(QLatin1Char(' '))) line.chop(1);
                if (!line.isEmpty()) {
                    if (!text.isEmpty()) text.append(QLatin1Char('\n'));
                    text.append(line);
                }
            }
            if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(text);
            event->accept();
            return;
        }
    }
    if (scroll_offset_ != 0) {
        scroll_offset_ = 0;
        update();
    }
    if ((mods & Qt::ControlModifier) && k >= Qt::Key_A && k <= Qt::Key_Z) {
        const char ctrl = static_cast<char>(k - Qt::Key_A + 1);
        writePty(QByteArray(1, ctrl));
        event->accept();
        return;
    }
    switch (k) {
        case Qt::Key_Return:
        case Qt::Key_Enter: writePty("\r"); break;
        case Qt::Key_Backspace: writePty("\x7f"); break;
        case Qt::Key_Tab: writePty("\t"); break;
        case Qt::Key_Escape: writePty("\x1b"); break;
        case Qt::Key_Up: writePty("\x1b[A"); break;
        case Qt::Key_Down: writePty("\x1b[B"); break;
        case Qt::Key_Right: writePty("\x1b[C"); break;
        case Qt::Key_Left: writePty("\x1b[D"); break;
        default:
            if (!event->text().isEmpty()) writePty(event->text().toUtf8());
            break;
    }
    event->accept();
}

void FinnTerminalView::mousePressEvent(QMouseEvent* event) {
    forceActiveFocus();
    event->accept();
}

void FinnTerminalView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        scroll_offset_ = std::min(static_cast<int>(scrollback_.size()), scroll_offset_ + 3);
    } else if (delta < 0) {
        scroll_offset_ = std::max(0, scroll_offset_ - 3);
    }
    update();
    event->accept();
}

void FinnTerminalView::paint(QPainter* p) {
    p->setRenderHint(QPainter::TextAntialiasing, true);
    const QColor default_fg(0xEB, 0xEF, 0xF5);
    const QColor cursor_col(0x00, 0x7B, 0xFF);
    p->setFont(font_);

    const int pad_x = 12;
    const int pad_y = 10;
    QFontMetrics fm(font_);
    const int ascent = fm.ascent();

    const int sb_size = static_cast<int>(scrollback_.size());
    for (int r = 0; r < rows_; ++r) {
        const int y = pad_y + r * cell_h_ + ascent;
        const int virtual_idx = sb_size - scroll_offset_ + r;
        const QVector<Cell>* row_ptr = nullptr;
        if (virtual_idx >= 0 && virtual_idx < sb_size) {
            row_ptr = &scrollback_[virtual_idx];
        } else {
            const int g = virtual_idx - sb_size;
            if (g >= 0 && g < grid_.size()) row_ptr = &grid_[g];
        }
        if (!row_ptr) continue;
        const auto& row = *row_ptr;
        for (int c = 0; c < cols_ && c < row.size(); ++c) {
            const Cell& cell = row[c];
            if (cell.ch == QLatin1Char(' ')) continue;
            p->setPen(cell.fg.isValid() ? cell.fg : default_fg);
            if (cell.bold) {
                QFont bf = font_;
                bf.setBold(true);
                p->setFont(bf);
                p->drawText(pad_x + c * cell_w_, y, QString(cell.ch));
                p->setFont(font_);
            } else {
                p->drawText(pad_x + c * cell_w_, y, QString(cell.ch));
            }
        }
    }

    // Cursor block (only when viewing live bottom)
    if (scroll_offset_ == 0) {
        const int cx = pad_x + cur_x_ * cell_w_;
        const int cy = pad_y + cur_y_ * cell_h_;
        p->fillRect(QRectF(cx, cy, cell_w_, cell_h_), QColor(cursor_col.red(), cursor_col.green(), cursor_col.blue(), 190));
    }
}

}  // namespace finn::apps
