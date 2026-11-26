// Native POSIX PTY + VT100/ANSI QWidget terminal renderer implementation.
// Supports interactive TUIs (nano, vim, cfdisk, nmtui, htop), 256-color &
// truecolor SGR, alternate screen buffer, DECSTBM scroll regions, and
// 2000-line scrollback across linuxfb, eglfs, Wayland, and offscreen.

#include "finn_terminal_widget.hpp"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
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

namespace finn::recovery {

FinnTerminalWidget::FinnTerminalWidget(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_InputMethodEnabled, true);

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

FinnTerminalWidget::~FinnTerminalWidget() {
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

bool FinnTerminalWidget::focusNextPrevChild(bool) {
    // Pass Tab directly to the PTY (bash completion, cfdisk, nmtui).
    return false;
}

void FinnTerminalWidget::setFontSize(int px) {
    const int clamped = std::clamp(px, 10, 28);
    if (clamped == font_size_) return;
    font_size_ = clamped;
    font_.setPixelSize(font_size_);
    updateMetrics();
    const int nc = std::max(20, (width() - 24) / cell_w_);
    const int nr = std::max(6, (height() - 20) / cell_h_);
    resizeGrid(nc, nr);
    update();
}

void FinnTerminalWidget::updateMetrics() {
    QFontMetrics fm(font_);
    cell_w_ = std::max(6, fm.horizontalAdvance(QLatin1Char('M')));
    cell_h_ = std::max(14, fm.height() + 2);
}

void FinnTerminalWidget::resizeGrid(int new_cols, int new_rows) {
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
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    cur_x_ = std::clamp(cur_x_, 0, cols_ - 1);
    cur_y_ = std::clamp(cur_y_, 0, rows_ - 1);
    wrap_pending_ = false;

    if (pty_fd_ >= 0) {
        struct winsize ws {};
        ws.ws_col = static_cast<unsigned short>(cols_);
        ws.ws_row = static_cast<unsigned short>(rows_);
        ws.ws_xpixel = static_cast<unsigned short>(width());
        ws.ws_ypixel = static_cast<unsigned short>(height());
        ::ioctl(pty_fd_, TIOCSWINSZ, &ws);
    }
}

void FinnTerminalWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (width() > 40 && height() > 40) {
        const int nc = std::max(20, (width() - 24) / cell_w_);
        const int nr = std::max(6, (height() - 20) / cell_h_);
        resizeGrid(nc, nr);
        update();
    }
}

void FinnTerminalWidget::startShell(const QString& workdir) {
    if (pty_fd_ >= 0) return;

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols_);
    ws.ws_row = static_cast<unsigned short>(rows_);
    ws.ws_xpixel = static_cast<unsigned short>(width());
    ws.ws_ypixel = static_cast<unsigned short>(height());

    int master_fd = -1;
    const pid_t pid = ::forkpty(&master_fd, nullptr, nullptr, &ws);
    if (pid < 0) {
        feedBytes("finn-terminal: failed to allocate PTY\r\n");
        update();
        return;
    }
    if (pid == 0) {
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        const QString dir = workdir.isEmpty() ? QDir::homePath() : workdir;
        (void)::chdir(dir.toLocal8Bit().constData());
        const char* sh = std::getenv("SHELL");
        if (!sh || !*sh) {
            sh = QFile::exists(QStringLiteral("/bin/bash")) ? "/bin/bash" : "/bin/sh";
        }
        ::execl(sh, sh, "-i", nullptr);
        ::_exit(127);
    }

    pty_fd_ = master_fd;
    child_pid_ = pid;
    const int flags = ::fcntl(pty_fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(pty_fd_, F_SETFL, flags | O_NONBLOCK);

    notifier_ = new QSocketNotifier(pty_fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &FinnTerminalWidget::readFromPty);

    const char* sh_env = std::getenv("SHELL");
    const QString sh_name = sh_env
        ? QString::fromLocal8Bit(sh_env).section(QLatin1Char('/'), -1)
        : QStringLiteral("bash");
    title_ = QStringLiteral("Finn Terminal (%1)").arg(sh_name);
    emit titleChanged(title_);
    emit runningChanged(true);
}

void FinnTerminalWidget::readFromPty() {
    if (pty_fd_ < 0) return;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(pty_fd_, buf, sizeof(buf));
        if (n > 0) {
            feedBytes(QByteArray(buf, static_cast<int>(n)));
        } else if (n == 0) {
            if (notifier_) {
                notifier_->setEnabled(false);
                notifier_->deleteLater();
                notifier_ = nullptr;
            }
            ::close(pty_fd_);
            pty_fd_ = -1;
            int status = 0;
            if (child_pid_ > 0) {
                ::waitpid(child_pid_, &status, WNOHANG);
                child_pid_ = -1;
            }
            emit runningChanged(false);
            emit shellExited(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
            break;
        } else {
            break;
        }
    }
    update();
}

void FinnTerminalWidget::writePty(const QByteArray& bytes) {
    if (pty_fd_ < 0 || bytes.isEmpty()) return;
    (void)::write(pty_fd_, bytes.constData(), static_cast<size_t>(bytes.size()));
}

void FinnTerminalWidget::sendText(const QString& text) {
    writePty(text.toUtf8());
}

void FinnTerminalWidget::sendControl(const QString& key) {
    if (key == QLatin1String("esc")) writePty("\x1b");
    else if (key == QLatin1String("tab")) writePty("\t");
    else if (key == QLatin1String("ctrl_c")) writePty("\x03");
    else if (key == QLatin1String("ctrl_d")) writePty("\x04");
    else if (key == QLatin1String("ctrl_l")) writePty("\x0c");
    else if (key == QLatin1String("up")) writePty(app_cursor_keys_ ? "\x1bOA" : "\x1b[A");
    else if (key == QLatin1String("down")) writePty(app_cursor_keys_ ? "\x1bOB" : "\x1b[B");
    else if (key == QLatin1String("left")) writePty(app_cursor_keys_ ? "\x1bOD" : "\x1b[D");
    else if (key == QLatin1String("right")) writePty(app_cursor_keys_ ? "\x1bOC" : "\x1b[C");
}

void FinnTerminalWidget::clearScreen() {
    for (auto& row : grid_) {
        for (auto& cell : row) cell = Cell{};
    }
    cur_x_ = 0;
    cur_y_ = 0;
    scroll_offset_ = 0;
    wrap_pending_ = false;
    update();
}

void FinnTerminalWidget::enterAltScreen() {
    if (alt_screen_) return;
    alt_saved_grid_ = grid_;
    alt_saved_x_ = cur_x_;
    alt_saved_y_ = cur_y_;
    alt_screen_ = true;
    scroll_offset_ = 0;
    for (auto& row : grid_) {
        for (auto& cell : row) cell = Cell{};
    }
    cur_x_ = 0;
    cur_y_ = 0;
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    wrap_pending_ = false;
}

void FinnTerminalWidget::leaveAltScreen() {
    if (!alt_screen_) return;
    alt_screen_ = false;
    if (alt_saved_grid_.size() == rows_ && !alt_saved_grid_.isEmpty() &&
        alt_saved_grid_[0].size() == cols_) {
        grid_ = std::move(alt_saved_grid_);
    } else {
        for (auto& row : grid_) {
            for (auto& cell : row) cell = Cell{};
        }
    }
    cur_x_ = std::clamp(alt_saved_x_, 0, cols_ - 1);
    cur_y_ = std::clamp(alt_saved_y_, 0, rows_ - 1);
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    wrap_pending_ = false;
}

void FinnTerminalWidget::feedBytes(const QByteArray& data) {
    // Preserve incomplete multi-byte UTF-8 sequences across PTY reads.
    QByteArray combined = utf8_pending_ + data;
    utf8_pending_.clear();
    int valid_len = combined.size();
    for (int tail = 1; tail <= 3 && tail <= combined.size(); ++tail) {
        const unsigned char b = static_cast<unsigned char>(combined[combined.size() - tail]);
        if ((b & 0xC0) == 0xC0) {
            int expected = 1;
            if ((b & 0xE0) == 0xC0) expected = 2;
            else if ((b & 0xF0) == 0xE0) expected = 3;
            else if ((b & 0xF8) == 0xF0) expected = 4;
            if (tail < expected) {
                valid_len = combined.size() - tail;
                utf8_pending_ = combined.mid(valid_len);
            }
            break;
        }
    }

    const QString text = QString::fromUtf8(combined.constData(), valid_len);
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        if (state_ == ParseState::Osc) {
            if (u == 0x07) {
                const int semi = esc_buf_.indexOf(QLatin1Char(';'));
                if (semi >= 0) {
                    const QString t = esc_buf_.mid(semi + 1).trimmed();
                    if (!t.isEmpty()) {
                        title_ = t;
                        emit titleChanged(title_);
                    }
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
        if (state_ == ParseState::EscCharset) {
            state_ = ParseState::Ground;
            continue;
        }
        if (state_ == ParseState::Esc) {
            if (ch == QLatin1Char('[')) {
                esc_buf_.clear();
                state_ = ParseState::Csi;
            } else if (ch == QLatin1Char(']')) {
                esc_buf_.clear();
                state_ = ParseState::Osc;
            } else if (ch == QLatin1Char('(') || ch == QLatin1Char(')')) {
                state_ = ParseState::EscCharset;
            } else {
                handleEsc(ch);
                state_ = ParseState::Ground;
            }
            continue;
        }
        if (state_ == ParseState::Csi) {
            if (u >= 0x40 && u <= 0x7E) {
                const bool priv = esc_buf_.startsWith(QLatin1Char('?'));
                const QString params = priv ? esc_buf_.mid(1) : esc_buf_;
                handleCsi(static_cast<char>(u), params, priv);
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
            wrap_pending_ = false;
        } else if (u == '\n' || u == 0x0b || u == 0x0c) {
            wrap_pending_ = false;
            newLine();
        } else if (u == '\b') {
            wrap_pending_ = false;
            if (cur_x_ > 0) --cur_x_;
        } else if (u == '\t') {
            wrap_pending_ = false;
            cur_x_ = std::min(cols_ - 1, ((cur_x_ / 8) + 1) * 8);
        } else if (u >= 0x20 && u != 0x7f) {
            putChar(ch);
        }
    }
}

void FinnTerminalWidget::handleEsc(QChar ch) {
    if (ch == QLatin1Char('7')) {
        saved_x_ = cur_x_;
        saved_y_ = cur_y_;
    } else if (ch == QLatin1Char('8')) {
        cur_x_ = std::clamp(saved_x_, 0, cols_ - 1);
        cur_y_ = std::clamp(saved_y_, 0, rows_ - 1);
        wrap_pending_ = false;
    } else if (ch == QLatin1Char('M')) {
        reverseIndex();
    } else if (ch == QLatin1Char('D')) {
        newLine();
    } else if (ch == QLatin1Char('E')) {
        cur_x_ = 0;
        newLine();
    } else if (ch == QLatin1Char('c')) {
        clearScreen();
        cur_fg_ = QColor();
        cur_bg_ = QColor();
        cur_bold_ = cur_dim_ = cur_underline_ = cur_inverse_ = false;
        scroll_top_ = 0;
        scroll_bottom_ = rows_ - 1;
    }
}

void FinnTerminalWidget::putChar(QChar ch) {
    if (cur_y_ < 0 || cur_y_ >= rows_) return;
    if (wrap_pending_) {
        wrap_pending_ = false;
        cur_x_ = 0;
        newLine();
    }
    if (cur_x_ >= cols_) {
        cur_x_ = 0;
        newLine();
    }
    grid_[cur_y_][cur_x_] = Cell{ch, cur_fg_, cur_bg_, cur_bold_, cur_dim_, cur_underline_, cur_inverse_};
    if (cur_x_ + 1 >= cols_) {
        wrap_pending_ = true;
    } else {
        ++cur_x_;
    }
}

void FinnTerminalWidget::newLine() {
    if (cur_y_ == scroll_bottom_) {
        scrollUpRegion(scroll_top_, scroll_bottom_, 1, !alt_screen_ && scroll_top_ == 0 && scroll_bottom_ == rows_ - 1);
    } else if (cur_y_ + 1 < rows_) {
        ++cur_y_;
    }
}

void FinnTerminalWidget::reverseIndex() {
    if (cur_y_ == scroll_top_) {
        scrollDownRegion(scroll_top_, scroll_bottom_, 1);
    } else if (cur_y_ > 0) {
        --cur_y_;
    }
}

void FinnTerminalWidget::scrollUpRegion(int top, int bottom, int count, bool push_scrollback) {
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::clamp(count, 1, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        if (push_scrollback) {
            scrollback_.append(grid_[top]);
            if (scrollback_.size() > 2000) scrollback_.removeFirst();
        }
        for (int r = top + 1; r <= bottom; ++r) {
            grid_[r - 1] = std::move(grid_[r]);
        }
        grid_[bottom] = QVector<Cell>(cols_);
    }
}

void FinnTerminalWidget::scrollDownRegion(int top, int bottom, int count) {
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, top, rows_ - 1);
    count = std::clamp(count, 1, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        for (int r = bottom; r > top; --r) {
            grid_[r] = std::move(grid_[r - 1]);
        }
        grid_[top] = QVector<Cell>(cols_);
    }
}

QColor FinnTerminalWidget::ansiColor(int idx, bool bright) const {
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

QColor FinnTerminalWidget::xterm256Color(int idx) const {
    idx = std::clamp(idx, 0, 255);
    if (idx < 8) return ansiColor(idx, false);
    if (idx < 16) return ansiColor(idx - 8, true);
    if (idx < 232) {
        const int v = idx - 16;
        const int r = (v / 36) % 6;
        const int g = (v / 6) % 6;
        const int b = v % 6;
        auto level = [](int c) { return c == 0 ? 0 : (55 + c * 40); };
        return QColor(level(r), level(g), level(b));
    }
    const int gray = 8 + (idx - 232) * 10;
    return QColor(gray, gray, gray);
}

void FinnTerminalWidget::handleCsi(char final_ch, const QString& params, bool priv) {
    const QStringList parts = params.isEmpty() ? QStringList{} : params.split(QLatin1Char(';'));
    auto arg = [&](int idx, int def) {
        if (idx >= parts.size() || parts[idx].isEmpty()) return def;
        bool ok = false;
        const int v = parts[idx].toInt(&ok);
        return ok ? v : def;
    };

    if (priv) {
        for (const QString& p : parts) {
            const int mode = p.toInt();
            if (final_ch == 'h') {
                if (mode == 1) app_cursor_keys_ = true;
                else if (mode == 25) cursor_visible_ = true;
                else if (mode == 47 || mode == 1047 || mode == 1049) enterAltScreen();
            } else if (final_ch == 'l') {
                if (mode == 1) app_cursor_keys_ = false;
                else if (mode == 25) cursor_visible_ = false;
                else if (mode == 47 || mode == 1047 || mode == 1049) leaveAltScreen();
            }
        }
        return;
    }

    switch (final_ch) {
        case 'A':
            cur_y_ = std::max(scroll_top_, cur_y_ - std::max(1, arg(0, 1)));
            wrap_pending_ = false;
            break;
        case 'B':
            cur_y_ = std::min(scroll_bottom_, cur_y_ + std::max(1, arg(0, 1)));
            wrap_pending_ = false;
            break;
        case 'C':
            cur_x_ = std::min(cols_ - 1, cur_x_ + std::max(1, arg(0, 1)));
            wrap_pending_ = false;
            break;
        case 'D':
            cur_x_ = std::max(0, cur_x_ - std::max(1, arg(0, 1)));
            wrap_pending_ = false;
            break;
        case 'G':
            cur_x_ = std::clamp(arg(0, 1) - 1, 0, cols_ - 1);
            wrap_pending_ = false;
            break;
        case 'd':
            cur_y_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
            wrap_pending_ = false;
            break;
        case 'H':
        case 'f':
            cur_y_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
            cur_x_ = std::clamp(arg(1, 1) - 1, 0, cols_ - 1);
            wrap_pending_ = false;
            break;
        case 'J': {
            const int mode = arg(0, 0);
            if (mode == 0) {
                for (int c = cur_x_; c < cols_; ++c) grid_[cur_y_][c] = Cell{};
                for (int r = cur_y_ + 1; r < rows_; ++r)
                    for (int c = 0; c < cols_; ++c) grid_[r][c] = Cell{};
            } else if (mode == 1) {
                for (int r = 0; r < cur_y_; ++r)
                    for (int c = 0; c < cols_; ++c) grid_[r][c] = Cell{};
                for (int c = 0; c <= cur_x_ && c < cols_; ++c) grid_[cur_y_][c] = Cell{};
            } else if (mode == 2 || mode == 3) {
                for (auto& r : grid_)
                    for (auto& c : r) c = Cell{};
                if (mode == 3) scrollback_.clear();
                cur_x_ = 0;
                cur_y_ = 0;
                wrap_pending_ = false;
            }
            break;
        }
        case 'K': {
            const int mode = arg(0, 0);
            if (cur_y_ >= 0 && cur_y_ < rows_) {
                const int start = (mode == 0) ? cur_x_ : 0;
                const int end = (mode == 1) ? std::min(cols_, cur_x_ + 1) : cols_;
                for (int c = start; c < end; ++c) {
                    grid_[cur_y_][c] = Cell{};
                }
            }
            break;
        }
        case 'L':
            if (cur_y_ >= scroll_top_ && cur_y_ <= scroll_bottom_) {
                scrollDownRegion(cur_y_, scroll_bottom_, std::max(1, arg(0, 1)));
            }
            break;
        case 'M':
            if (cur_y_ >= scroll_top_ && cur_y_ <= scroll_bottom_) {
                scrollUpRegion(cur_y_, scroll_bottom_, std::max(1, arg(0, 1)), false);
            }
            break;
        case 'P': {
            const int n = std::clamp(arg(0, 1), 1, cols_ - cur_x_);
            for (int c = cur_x_; c < cols_ - n; ++c) grid_[cur_y_][c] = grid_[cur_y_][c + n];
            for (int c = cols_ - n; c < cols_; ++c) grid_[cur_y_][c] = Cell{};
            break;
        }
        case '@': {
            const int n = std::clamp(arg(0, 1), 1, cols_ - cur_x_);
            for (int c = cols_ - 1; c >= cur_x_ + n; --c) grid_[cur_y_][c] = grid_[cur_y_][c - n];
            for (int c = cur_x_; c < cur_x_ + n; ++c) grid_[cur_y_][c] = Cell{};
            break;
        }
        case 'X': {
            const int n = std::clamp(arg(0, 1), 1, cols_ - cur_x_);
            for (int c = cur_x_; c < cur_x_ + n; ++c) grid_[cur_y_][c] = Cell{};
            break;
        }
        case 'S':
            scrollUpRegion(scroll_top_, scroll_bottom_, std::max(1, arg(0, 1)), false);
            break;
        case 'T':
            scrollDownRegion(scroll_top_, scroll_bottom_, std::max(1, arg(0, 1)));
            break;
        case 'r': {
            const int top = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
            const int bot = std::clamp(arg(1, rows_) - 1, top, rows_ - 1);
            scroll_top_ = top;
            scroll_bottom_ = bot;
            cur_x_ = 0;
            cur_y_ = 0;
            wrap_pending_ = false;
            break;
        }
        case 's':
            saved_x_ = cur_x_;
            saved_y_ = cur_y_;
            break;
        case 'u':
            cur_x_ = std::clamp(saved_x_, 0, cols_ - 1);
            cur_y_ = std::clamp(saved_y_, 0, rows_ - 1);
            wrap_pending_ = false;
            break;
        case 'n':
            if (arg(0, 0) == 6) {
                writePty(QStringLiteral("\x1b[%1;%2R").arg(cur_y_ + 1).arg(cur_x_ + 1).toLatin1());
            }
            break;
        case 'm': {
            if (parts.isEmpty()) {
                cur_fg_ = QColor();
                cur_bg_ = QColor();
                cur_bold_ = cur_dim_ = cur_underline_ = cur_inverse_ = false;
                break;
            }
            for (int i = 0; i < parts.size(); ++i) {
                const int code = parts[i].toInt();
                if (code == 0) {
                    cur_fg_ = QColor();
                    cur_bg_ = QColor();
                    cur_bold_ = cur_dim_ = cur_underline_ = cur_inverse_ = false;
                } else if (code == 1) cur_bold_ = true;
                else if (code == 2) cur_dim_ = true;
                else if (code == 4) cur_underline_ = true;
                else if (code == 7) cur_inverse_ = true;
                else if (code == 22) { cur_bold_ = false; cur_dim_ = false; }
                else if (code == 24) cur_underline_ = false;
                else if (code == 27) cur_inverse_ = false;
                else if (code >= 30 && code <= 37) cur_fg_ = ansiColor(code - 30, false);
                else if (code >= 90 && code <= 97) cur_fg_ = ansiColor(code - 90, true);
                else if (code == 39) cur_fg_ = QColor();
                else if (code >= 40 && code <= 47) cur_bg_ = ansiColor(code - 40, false);
                else if (code >= 100 && code <= 107) cur_bg_ = ansiColor(code - 100, true);
                else if (code == 49) cur_bg_ = QColor();
                else if ((code == 38 || code == 48) && i + 1 < parts.size()) {
                    const bool is_fg = (code == 38);
                    const int mode = parts[++i].toInt();
                    if (mode == 5 && i + 1 < parts.size()) {
                        const QColor col = xterm256Color(parts[++i].toInt());
                        if (is_fg) cur_fg_ = col; else cur_bg_ = col;
                    } else if (mode == 2 && i + 3 < parts.size()) {
                        const int r = std::clamp(parts[++i].toInt(), 0, 255);
                        const int g = std::clamp(parts[++i].toInt(), 0, 255);
                        const int b = std::clamp(parts[++i].toInt(), 0, 255);
                        const QColor col(r, g, b);
                        if (is_fg) cur_fg_ = col; else cur_bg_ = col;
                    }
                }
            }
            break;
        }
        default: break;
    }
}

void FinnTerminalWidget::keyPressEvent(QKeyEvent* event) {
    const int k = event->key();
    const auto mods = event->modifiers();

    // Ctrl+Shift+V paste & Ctrl+Shift+C copy
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

    if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier) &&
        k >= Qt::Key_A && k <= Qt::Key_Z) {
        const char ctrl = static_cast<char>(k - Qt::Key_A + 1);
        writePty(QByteArray(1, ctrl));
        event->accept();
        return;
    }

    switch (k) {
        case Qt::Key_Return:
        case Qt::Key_Enter:    writePty("\r"); break;
        case Qt::Key_Backspace: writePty("\x7f"); break;
        case Qt::Key_Tab:      writePty("\t"); break;
        case Qt::Key_Backtab:  writePty("\x1b[Z"); break;
        case Qt::Key_Escape:   writePty("\x1b"); break;
        case Qt::Key_Up:       writePty(app_cursor_keys_ ? "\x1bOA" : "\x1b[A"); break;
        case Qt::Key_Down:     writePty(app_cursor_keys_ ? "\x1bOB" : "\x1b[B"); break;
        case Qt::Key_Right:    writePty(app_cursor_keys_ ? "\x1bOC" : "\x1b[C"); break;
        case Qt::Key_Left:     writePty(app_cursor_keys_ ? "\x1bOD" : "\x1b[D"); break;
        case Qt::Key_Home:     writePty("\x1b[H"); break;
        case Qt::Key_End:      writePty("\x1b[F"); break;
        case Qt::Key_Insert:   writePty("\x1b[2~"); break;
        case Qt::Key_Delete:   writePty("\x1b[3~"); break;
        case Qt::Key_PageUp:   writePty("\x1b[5~"); break;
        case Qt::Key_PageDown: writePty("\x1b[6~"); break;
        case Qt::Key_F1:       writePty("\x1bOP"); break;
        case Qt::Key_F2:       writePty("\x1bOQ"); break;
        case Qt::Key_F3:       writePty("\x1bOR"); break;
        case Qt::Key_F4:       writePty("\x1bOS"); break;
        case Qt::Key_F5:       writePty("\x1b[15~"); break;
        case Qt::Key_F6:       writePty("\x1b[17~"); break;
        case Qt::Key_F7:       writePty("\x1b[18~"); break;
        case Qt::Key_F8:       writePty("\x1b[19~"); break;
        case Qt::Key_F9:       writePty("\x1b[20~"); break;
        case Qt::Key_F10:      writePty("\x1b[21~"); break;
        case Qt::Key_F11:      writePty("\x1b[23~"); break;
        case Qt::Key_F12:      writePty("\x1b[24~"); break;
        default:
            if (!event->text().isEmpty()) writePty(event->text().toUtf8());
            break;
    }
    event->accept();
}

void FinnTerminalWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void FinnTerminalWidget::wheelEvent(QWheelEvent* event) {
    if (alt_screen_) {
        event->accept();
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta > 0) {
        scroll_offset_ = std::min(static_cast<int>(scrollback_.size()), scroll_offset_ + 3);
    } else if (delta < 0) {
        scroll_offset_ = std::max(0, scroll_offset_ - 3);
    }
    update();
    event->accept();
}

void FinnTerminalWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor default_bg(0x12, 0x14, 0x1A);
    const QColor default_fg(0xEB, 0xEF, 0xF5);
    const QColor cursor_col(0x00, 0x7B, 0xFF);
    p.fillRect(rect(), default_bg);
    p.setFont(font_);

    const int pad_x = 12;
    const int pad_y = 10;
    QFontMetrics fm(font_);
    const int ascent = fm.ascent();

    const int sb_size = alt_screen_ ? 0 : static_cast<int>(scrollback_.size());
    const int eff_scroll = alt_screen_ ? 0 : scroll_offset_;

    for (int r = 0; r < rows_; ++r) {
        const int row_y = pad_y + r * cell_h_;
        const int text_y = row_y + ascent;
        const int virtual_idx = sb_size - eff_scroll + r;
        const QVector<Cell>* row_ptr = nullptr;
        if (!alt_screen_ && virtual_idx >= 0 && virtual_idx < sb_size) {
            row_ptr = &scrollback_[virtual_idx];
        } else {
            const int g = virtual_idx - sb_size;
            if (g >= 0 && g < grid_.size()) row_ptr = &grid_[g];
        }
        if (!row_ptr) continue;
        const auto& row = *row_ptr;
        for (int c = 0; c < cols_ && c < row.size(); ++c) {
            const Cell& cell = row[c];
            QColor fg = cell.fg.isValid() ? cell.fg : default_fg;
            QColor bg = cell.bg.isValid() ? cell.bg : default_bg;
            if (cell.inverse) std::swap(fg, bg);
            if (cell.dim) fg.setAlpha(150);

            const int cx = pad_x + c * cell_w_;
            if (bg != default_bg) {
                p.fillRect(cx, row_y, cell_w_, cell_h_, bg);
            }
            if (cell.ch != QLatin1Char(' ')) {
                p.setPen(fg);
                if (cell.bold || cell.underline) {
                    QFont cf = font_;
                    cf.setBold(cell.bold);
                    cf.setUnderline(cell.underline);
                    p.setFont(cf);
                    p.drawText(cx, text_y, QString(cell.ch));
                    p.setFont(font_);
                } else {
                    p.drawText(cx, text_y, QString(cell.ch));
                }
            }
        }
    }

    if (cursor_visible_ && eff_scroll == 0) {
        const int cx = pad_x + cur_x_ * cell_w_;
        const int cy = pad_y + cur_y_ * cell_h_;
        p.fillRect(QRectF(cx, cy, cell_w_, cell_h_),
                   QColor(cursor_col.red(), cursor_col.green(), cursor_col.blue(), 195));
    }
}

}  // namespace finn::recovery
