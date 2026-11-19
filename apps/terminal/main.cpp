// Finn Terminal: native C++20 Qt6 + FinnSDK interactive PTY terminal emulator
// redesigned to match Figma Settings (20:64) 1:1 with ChromeMode::FigmaSplit,
// 250px collapsible session & quick-command sidebar with 26x26 circular colored
// badges, sleek 16px Regular header row, and crisp r=6 terminal card surface.

#include <FinnSDK/FinnSDK.hpp>

#include <QByteArrayView>
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QStackedWidget>
#include <QStringDecoder>
#include <QStringList>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace finn::ui;

void apply_header_16px_regular(FinnLabel* lbl) {
    if (!lbl) return;
    auto sync = [lbl]() {
        QFont f = finn_font(12.0, QFont::Normal);
        f.setPixelSize(16);
        lbl->setFont(f);
    };
    sync();
    QObject::connect(&FinnThemeManager::instance(),
                     &FinnThemeManager::themeChanged,
                     lbl, sync);
}

struct Cell {
    QChar ch = QLatin1Char(' ');
    QColor fg;
    QColor bg;
    bool has_fg = false;
    bool has_bg = false;
    bool bold = false;
    bool inverse = false;
};

QColor ansi_16(int idx, bool dark) {
    static const QRgb kDarkPalette[16] = {
        0xFF1B1F27, 0xFFF87171, 0xFF4ADE80, 0xFFFBBF24,
        0xFF60A5FA, 0xFFC084FC, 0xFF22D3EE, 0xFFE5E7EB,
        0xFF6B7280, 0xFFFCA5A5, 0xFF86EFAC, 0xFFFDE68A,
        0xFF93C5FD, 0xFFD8B4FE, 0xFF67E8F9, 0xFFFFFFFF,
    };
    static const QRgb kLightPalette[16] = {
        0xFF1F2937, 0xFFDC2626, 0xFF16A34A, 0xFFD97706,
        0xFF2563EB, 0xFF9333EA, 0xFF0891B2, 0xFF4B5563,
        0xFF6B7280, 0xFFEF4444, 0xFF22C55E, 0xFFF59E0B,
        0xFF3B82F6, 0xFFA855F7, 0xFF06B6D4, 0xFF111827,
    };
    idx = std::clamp(idx, 0, 15);
    return QColor(dark ? kDarkPalette[idx] : kLightPalette[idx]);
}

QColor ansi_256(int n, bool dark) {
    if (n < 16) return ansi_16(n, dark);
    if (n < 232) {
        int c = n - 16;
        int r = c / 36;
        int g = (c / 6) % 6;
        int b = c % 6;
        auto conv = [](int v) { return v ? (v * 40 + 55) : 0; };
        return QColor(conv(r), conv(g), conv(b));
    }
    int gray = (n - 232) * 10 + 8;
    return QColor(gray, gray, gray);
}

class TerminalWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TerminalWidget(const QString& start_cwd = {},
                            const QString& exec_cmd = {},
                            QWidget* parent = nullptr)
        : QWidget(parent), start_cwd_(start_cwd), exec_cmd_(exec_cmd) {
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("Terminal emulator"));
        setAttribute(Qt::WA_InputMethodEnabled, true);
        updateMetrics();
        resetScreen(80, 24);
        connect(&FinnThemeManager::instance(),
                &FinnThemeManager::themeChanged, this,
                qOverload<>(&QWidget::update));
        startShell();
    }

    ~TerminalWidget() override {
        if (master_fd_ >= 0) ::close(master_fd_);
        if (child_pid_ > 0) {
            ::kill(child_pid_, SIGHUP);
            ::waitpid(child_pid_, nullptr, WNOHANG);
        }
    }

    void zoomIn() {
        font_pt_ = std::min(22.0, font_pt_ + 1.0);
        updateMetrics();
        resizeGridToWidget();
    }
    void zoomOut() {
        font_pt_ = std::max(7.5, font_pt_ - 1.0);
        updateMetrics();
        resizeGridToWidget();
    }
    void clearScrollback() {
        scrollback_.clear();
        scroll_offset_ = 0;
        clearGrid();
        cur_row_ = 0;
        cur_col_ = 0;
        writeToPty("\f", 1);
        update();
    }
    void copySelectionOrScreen() {
        QString out;
        for (int r = 0; r < rows_; ++r) {
            QString line;
            for (int c = 0; c < cols_; ++c) line.append(grid_[r][c].ch);
            while (line.endsWith(QLatin1Char(' '))) line.chop(1);
            out += line + QLatin1Char('\n');
        }
        while (out.endsWith(QStringLiteral("\n\n"))) out.chop(1);
        QGuiApplication::clipboard()->setText(out);
        emit toastRequested(QStringLiteral("Copied terminal buffer to clipboard"));
    }
    void pasteClipboard() {
        const QString text = QGuiApplication::clipboard()->text();
        if (text.isEmpty()) return;
        const QByteArray bytes = text.toUtf8();
        writeToPty(bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    void sendCommandLine(const QString& cmd) {
        const QByteArray bytes = (cmd + QLatin1Char('\r')).toUtf8();
        writeToPty(bytes.constData(), static_cast<size_t>(bytes.size()));
        setFocus();
    }

signals:
    void titleChanged(const QString& title);
    void processExited(int status);
    void toastRequested(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        resizeGridToWidget();
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF card_r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor bg = t.dark ? t.surface_canvas : t.tile;
        const QColor fg = t.ink;

        QPainterPath clip;
        clip.addRoundedRect(card_r, kRadiusMd, kRadiusMd);
        p.save();
        p.setClipPath(clip);
        p.fillRect(card_r, bg);

        QFont normal = term_font_;
        QFont bold_font = term_font_;
        bold_font.setBold(true);

        const qreal pad_x = 14.0;
        const qreal pad_y = 12.0;

        for (int r = 0; r < rows_; ++r) {
            const auto& row = visibleRow(r);
            const qreal y = pad_y + r * cell_h_;
            for (int c = 0; c < cols_ && c < static_cast<int>(row.size()); ++c) {
                const Cell& cell = row[c];
                QColor cfg = cell.has_fg ? cell.fg : fg;
                QColor cbg = cell.has_bg ? cell.bg : bg;
                if (cell.inverse) std::swap(cfg, cbg);

                const QRectF cell_r(pad_x + c * cell_w_, y, cell_w_ + 0.4, cell_h_);
                if (cbg != bg) {
                    p.fillRect(cell_r, cbg);
                }
                const bool is_cursor =
                    (scroll_offset_ == 0 && r == cur_row_ && c == cur_col_);
                if (is_cursor) {
                    p.fillRect(cell_r, t.accent);
                    cfg = Qt::white;
                }
                if (cell.ch != QLatin1Char(' ')) {
                    p.setFont(cell.bold ? bold_font : normal);
                    p.setPen(cfg);
                    p.drawText(cell_r, Qt::AlignLeft | Qt::AlignVCenter,
                               QString(cell.ch));
                }
            }
        }

        if (!scrollback_.empty() && height() > 40) {
            const qreal total_lines =
                static_cast<qreal>(scrollback_.size()) + static_cast<qreal>(rows_);
            const qreal track_h = static_cast<qreal>(height()) - 16.0;
            const qreal thumb_h = std::max<qreal>(
                24.0, track_h * (static_cast<qreal>(rows_) / total_lines));
            const qreal max_scroll = static_cast<qreal>(scrollback_.size());
            const qreal ratio = 1.0 - (static_cast<qreal>(scroll_offset_) / max_scroll);
            const qreal thumb_y = 8.0 + (track_h - thumb_h) * std::clamp(ratio, 0.0, 1.0);
            const int alpha = (scroll_offset_ > 0) ? (t.dark ? 115 : 95)
                                                   : (t.dark ? 55 : 42);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(t.ink.red(), t.ink.green(), t.ink.blue(), alpha));
            p.drawRoundedRect(QRectF(width() - 9.0, thumb_y, 5.0, thumb_h), 2.5, 2.5);
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.hair, 1.0));
        p.drawRoundedRect(card_r, kRadiusMd, kRadiusMd);
    }

    void keyPressEvent(QKeyEvent* event) override {
        const auto mods = event->modifiers();
        if ((mods & (Qt::ControlModifier | Qt::ShiftModifier)) ==
            (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (event->key() == Qt::Key_C) {
                copySelectionOrScreen();
                return;
            }
            if (event->key() == Qt::Key_V) {
                pasteClipboard();
                return;
            }
        }

        scroll_offset_ = 0;
        QByteArray seq;
        switch (event->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                seq = "\r";
                break;
            case Qt::Key_Backspace:
                seq = "\x7f";
                break;
            case Qt::Key_Tab:
                seq = "\t";
                break;
            case Qt::Key_Backtab:
                seq = "\x1b[Z";
                break;
            case Qt::Key_Escape:
                seq = "\x1b";
                break;
            case Qt::Key_Up:
                seq = "\x1b[A";
                break;
            case Qt::Key_Down:
                seq = "\x1b[B";
                break;
            case Qt::Key_Right:
                seq = "\x1b[C";
                break;
            case Qt::Key_Left:
                seq = "\x1b[D";
                break;
            case Qt::Key_Home:
                seq = "\x1b[H";
                break;
            case Qt::Key_End:
                seq = "\x1b[F";
                break;
            case Qt::Key_PageUp:
                seq = "\x1b[5~";
                break;
            case Qt::Key_PageDown:
                seq = "\x1b[6~";
                break;
            case Qt::Key_Delete:
                seq = "\x1b[3~";
                break;
            default: {
                const QString txt = event->text();
                if (!txt.isEmpty()) seq = txt.toUtf8();
                break;
            }
        }
        if (!seq.isEmpty()) {
            writeToPty(seq.constData(), static_cast<size_t>(seq.size()));
            update();
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        const int delta = event->angleDelta().y() > 0 ? 3 : -3;
        scroll_offset_ = std::clamp(
            scroll_offset_ + delta, 0, static_cast<int>(scrollback_.size()));
        update();
    }

private:
    void updateMetrics() {
        term_font_ = finn_mono_font(font_pt_, QFont::Normal);
        QFontMetricsF fm(term_font_);
        cell_w_ = std::max<qreal>(6.0, fm.horizontalAdvance(QLatin1Char('M')));
        cell_h_ = std::max<qreal>(12.0, std::ceil(fm.height()));
    }

    void resetScreen(int cols, int rows) {
        cols_ = std::max(10, cols);
        rows_ = std::max(4, rows);
        grid_.resize(static_cast<std::size_t>(rows_));
        for (auto& row : grid_) {
            row.assign(static_cast<std::size_t>(cols_), Cell{});
        }
        cur_row_ = std::clamp(cur_row_, 0, rows_ - 1);
        cur_col_ = std::clamp(cur_col_, 0, cols_ - 1);
    }

    void clearGrid() {
        for (auto& row : grid_) {
            row.assign(static_cast<std::size_t>(cols_), Cell{});
        }
    }

    void resizeGridToWidget() {
        const int next_cols = std::max(20, static_cast<int>((width() - 32) / cell_w_));
        const int next_rows = std::max(6, static_cast<int>((height() - 24) / cell_h_));
        if (next_cols == cols_ && next_rows == rows_) return;

        std::vector<std::vector<Cell>> next(next_rows, std::vector<Cell>(next_cols));
        const int copy_r = std::min(rows_, next_rows);
        const int copy_c = std::min(cols_, next_cols);
        for (int r = 0; r < copy_r; ++r) {
            for (int c = 0; c < copy_c; ++c) next[r][c] = grid_[r][c];
        }
        grid_ = std::move(next);
        cols_ = next_cols;
        rows_ = next_rows;
        cur_row_ = std::clamp(cur_row_, 0, rows_ - 1);
        cur_col_ = std::clamp(cur_col_, 0, cols_ - 1);

        if (master_fd_ >= 0) {
            winsize ws{};
            ws.ws_col = static_cast<unsigned short>(cols_);
            ws.ws_row = static_cast<unsigned short>(rows_);
            ws.ws_xpixel = static_cast<unsigned short>(width());
            ws.ws_ypixel = static_cast<unsigned short>(height());
            ::ioctl(master_fd_, TIOCSWINSZ, &ws);
        }
        update();
    }

    const std::vector<Cell>& visibleRow(int screen_row) const {
        const int total_sb = static_cast<int>(scrollback_.size());
        const int virtual_idx = total_sb - scroll_offset_ + screen_row;
        if (virtual_idx >= 0 && virtual_idx < total_sb) {
            return scrollback_[virtual_idx];
        }
        const int g_idx = std::clamp(virtual_idx - total_sb, 0, rows_ - 1);
        return grid_[g_idx];
    }

    void startShell() {
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols_);
        ws.ws_row = static_cast<unsigned short>(rows_);
        pid_t pid = ::forkpty(&master_fd_, nullptr, nullptr, &ws);
        if (pid < 0) return;

        if (pid == 0) {
            if (!start_cwd_.isEmpty()) {
                ::chdir(start_cwd_.toUtf8().constData());
            }
            ::setenv("TERM", "xterm-256color", 1);
            ::setenv("COLORTERM", "truecolor", 1);
            const char* shell = ::getenv("SHELL");
            if (!shell || !*shell) shell = "/bin/bash";
            if (!exec_cmd_.isEmpty()) {
                const QByteArray cmd = exec_cmd_.toUtf8();
                ::execl(shell, shell, "-c", cmd.constData(), nullptr);
            } else {
                ::execl(shell, shell, "-l", nullptr);
            }
            _exit(127);
        }

        child_pid_ = pid;
        const int flags = ::fcntl(master_fd_, F_GETFL, 0);
        if (flags >= 0) ::fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

        notifier_ = new QSocketNotifier(master_fd_, QSocketNotifier::Read, this);
        connect(notifier_, &QSocketNotifier::activated, this,
                &TerminalWidget::readPty);
    }

    void writeToPty(const char* data, size_t len) {
        if (master_fd_ < 0 || !data || len == 0) return;
        size_t off = 0;
        while (off < len) {
            ssize_t w = ::write(master_fd_, data + off, len - off);
            if (w > 0) {
                off += static_cast<size_t>(w);
            } else if (errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
    }

    void readPty() {
        char buf[8192];
        while (master_fd_ >= 0) {
            ssize_t n = ::read(master_fd_, buf, sizeof(buf));
            if (n > 0) {
                feedBytes(buf, static_cast<size_t>(n));
            } else if (n < 0 && errno == EAGAIN) {
                break;
            } else {
                if (notifier_) notifier_->setEnabled(false);
                int status = 0;
                if (child_pid_ > 0) ::waitpid(child_pid_, &status, WNOHANG);
                emit processExited(status);
                break;
            }
        }
        update();
    }

    void scrollUpOne() {
        if (grid_.empty()) return;
        scrollback_.push_back(grid_.front());
        if (scrollback_.size() > 3000) {
            scrollback_.erase(scrollback_.begin());
        }
        for (int r = 1; r < rows_; ++r) grid_[r - 1] = std::move(grid_[r]);
        grid_.back().assign(cols_, Cell{});
    }

    void putChar(QChar ch) {
        if (cur_col_ >= cols_) {
            cur_col_ = 0;
            if (++cur_row_ >= rows_) {
                cur_row_ = rows_ - 1;
                scrollUpOne();
            }
        }
        Cell& cell = grid_[cur_row_][cur_col_++];
        cell = attr_;
        cell.ch = ch;
    }

    void applySgr(const std::vector<int>& params) {
        const bool dark = FinnThemeManager::instance().isDark();
        if (params.empty()) {
            attr_ = Cell{};
            return;
        }
        for (size_t i = 0; i < params.size(); ++i) {
            const int code = params[i];
            if (code == 0) {
                attr_ = Cell{};
            } else if (code == 1) {
                attr_.bold = true;
            } else if (code == 7) {
                attr_.inverse = true;
            } else if (code == 22) {
                attr_.bold = false;
            } else if (code == 27) {
                attr_.inverse = false;
            } else if (code == 39) {
                attr_.has_fg = false;
            } else if (code == 49) {
                attr_.has_bg = false;
            } else if (code >= 30 && code <= 37) {
                attr_.fg = ansi_16(code - 30, dark);
                attr_.has_fg = true;
            } else if (code >= 90 && code <= 97) {
                attr_.fg = ansi_16(8 + code - 90, dark);
                attr_.has_fg = true;
            } else if (code >= 40 && code <= 47) {
                attr_.bg = ansi_16(code - 40, dark);
                attr_.has_bg = true;
            } else if (code >= 100 && code <= 107) {
                attr_.bg = ansi_16(8 + code - 100, dark);
                attr_.has_bg = true;
            } else if ((code == 38 || code == 48) && i + 1 < params.size()) {
                const bool is_fg = (code == 38);
                if (params[i + 1] == 5 && i + 2 < params.size()) {
                    const QColor col = ansi_256(params[i + 2], dark);
                    if (is_fg) {
                        attr_.fg = col;
                        attr_.has_fg = true;
                    } else {
                        attr_.bg = col;
                        attr_.has_bg = true;
                    }
                    i += 2;
                } else if (params[i + 1] == 2 && i + 4 < params.size()) {
                    const QColor col(std::clamp(params[i + 2], 0, 255),
                                     std::clamp(params[i + 3], 0, 255),
                                     std::clamp(params[i + 4], 0, 255));
                    if (is_fg) {
                        attr_.fg = col;
                        attr_.has_fg = true;
                    } else {
                        attr_.bg = col;
                        attr_.has_bg = true;
                    }
                    i += 4;
                }
            }
        }
    }

    void handleCsi(char final_ch) {
        std::vector<int> params;
        const QString body = QString::fromLatin1(csi_buf_);
        QString cleaned = body;
        const bool priv = cleaned.startsWith(QLatin1Char('?'));
        if (priv) cleaned = cleaned.mid(1);
        if (!cleaned.isEmpty()) {
            for (const QString& part : cleaned.split(QLatin1Char(';'))) {
                params.push_back(part.isEmpty() ? 0 : part.toInt());
            }
        }
        auto arg = [&](size_t idx, int def) {
            return (idx < params.size() && params[idx] > 0) ? params[idx] : def;
        };

        switch (final_ch) {
            case 'm':
                applySgr(params);
                break;
            case 'H':
            case 'f':
                cur_row_ = std::clamp(arg(0, 1) - 1, 0, rows_ - 1);
                cur_col_ = std::clamp(arg(1, 1) - 1, 0, cols_ - 1);
                break;
            case 'A':
                cur_row_ = std::max(0, cur_row_ - arg(0, 1));
                break;
            case 'B':
                cur_row_ = std::min(rows_ - 1, cur_row_ + arg(0, 1));
                break;
            case 'C':
                cur_col_ = std::min(cols_ - 1, cur_col_ + arg(0, 1));
                break;
            case 'D':
                cur_col_ = std::max(0, cur_col_ - arg(0, 1));
                break;
            case 'G':
                cur_col_ = std::clamp(arg(0, 1) - 1, 0, cols_ - 1);
                break;
            case 'K': {
                const int mode = params.empty() ? 0 : params[0];
                int from = (mode == 0) ? cur_col_ : 0;
                int to = (mode == 1) ? cur_col_ + 1 : cols_;
                for (int c = from; c < std::min(cols_, to); ++c) {
                    grid_[cur_row_][c] = Cell{};
                }
                break;
            }
            case 'J': {
                const int mode = params.empty() ? 0 : params[0];
                if (mode == 2 || mode == 3) {
                    clearGrid();
                    cur_row_ = 0;
                    cur_col_ = 0;
                } else if (mode == 0) {
                    for (int c = cur_col_; c < cols_; ++c) grid_[cur_row_][c] = Cell{};
                    for (int r = cur_row_ + 1; r < rows_; ++r) {
                        grid_[r].assign(cols_, Cell{});
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void feedBytes(const char* data, size_t len) {
        const QString decoded = utf8_decoder_.decode(
            QByteArrayView(data, static_cast<qsizetype>(len)));
        for (QChar qc : decoded) {
            const ushort uc = qc.unicode();
            if (state_ == State::Esc) {
                if (uc == '[') {
                    state_ = State::Csi;
                    csi_buf_.clear();
                } else if (uc == ']') {
                    state_ = State::Osc;
                    osc_buf_.clear();
                } else {
                    state_ = State::Normal;
                }
                continue;
            }
            if (state_ == State::Csi) {
                if (uc >= 0x40 && uc <= 0x7E) {
                    handleCsi(static_cast<char>(uc));
                    state_ = State::Normal;
                } else if (csi_buf_.size() < 64) {
                    csi_buf_.append(static_cast<char>(uc));
                } else {
                    state_ = State::Normal;
                }
                continue;
            }
            if (state_ == State::OscEsc) {
                finishOsc();
                state_ = State::Normal;
                continue;
            }
            if (state_ == State::Osc) {
                if (uc == '\a') {
                    finishOsc();
                    state_ = State::Normal;
                } else if (uc == 0x1b) {
                    state_ = State::OscEsc;
                } else if (osc_buf_.size() < 4096) {
                    osc_buf_.append(QString(qc).toUtf8());
                }
                continue;
            }

            if (uc == '\x1b') {
                state_ = State::Esc;
            } else if (uc == '\r') {
                cur_col_ = 0;
            } else if (uc == '\n') {
                if (++cur_row_ >= rows_) {
                    cur_row_ = rows_ - 1;
                    scrollUpOne();
                }
            } else if (uc == '\b') {
                cur_col_ = std::max(0, cur_col_ - 1);
            } else if (uc == '\t') {
                cur_col_ = std::min(cols_ - 1, ((cur_col_ / 8) + 1) * 8);
            } else if (uc >= 0x20) {
                putChar(qc);
            }
        }
    }

    enum class State { Normal, Esc, Csi, Osc, OscEsc };

    void finishOsc() {
        const int semi = osc_buf_.indexOf(';');
        if (semi < 0) return;
        const QByteArray code = osc_buf_.left(semi);
        if (code == "0" || code == "2") {
            emit titleChanged(QString::fromUtf8(osc_buf_.mid(semi + 1)));
        }
    }

    QString start_cwd_;
    QString exec_cmd_;
    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    QSocketNotifier* notifier_ = nullptr;

    qreal font_pt_ = 10.5;
    QFont term_font_;
    qreal cell_w_ = 8.0;
    qreal cell_h_ = 16.0;
    int cols_ = 80;
    int rows_ = 24;
    int cur_row_ = 0;
    int cur_col_ = 0;
    int scroll_offset_ = 0;
    Cell attr_;
    std::vector<std::vector<Cell>> grid_;
    std::vector<std::vector<Cell>> scrollback_;

    State state_ = State::Normal;
    QStringDecoder utf8_decoder_{QStringDecoder::Utf8};
    QByteArray csi_buf_;
    QByteArray osc_buf_;
};

class TerminalWindow final : public FinnWindow {
    Q_OBJECT
public:
    TerminalWindow(const QString& cwd, const QString& exec_cmd)
        : FinnWindow(QStringLiteral("Finn Terminal"), QStringLiteral("terminal")),
          initial_cwd_(cwd),
          initial_exec_(exec_cmd) {
        setMinimumSize(800, 500);
        resize(920, 560);
        setChromeMode(ChromeMode::FigmaSplit);

        setupSidebar();
        setupContent();
        createSession(initial_cwd_, initial_exec_);
    }

private:
    struct SessionEntry {
        QString title;
        TerminalWidget* widget = nullptr;
    };

    void setupSidebar() {
        sidebar_ = new QWidget(this);
        sidebar_->setFixedWidth(250);
        auto* side_root = new QVBoxLayout(sidebar_);
        side_root->setContentsMargins(12, 24, 12, 12);
        side_root->setSpacing(10);

        auto* hdr_lbl = new FinnLabel(
            QStringLiteral("Terminal"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, sidebar_);
        apply_header_16px_regular(hdr_lbl);
        side_root->addWidget(hdr_lbl);

        sidebar_search_ = new FinnSearchField(
            QStringLiteral("Search"), sidebar_);
        sidebar_search_->setAccessibleName(QStringLiteral("Search terminal sessions"));
        sidebar_search_->setFixedHeight(30);
        sidebar_search_->setPillMode(true);
        connect(sidebar_search_, &FinnSearchField::textChanged,
                this, [this](const QString&) { rebuildSidebarList(); });
        side_root->addWidget(sidebar_search_);

        auto* new_session_item = new FinnNavItem(
            QStringLiteral("plus"), QStringLiteral("New Session"), sidebar_);
        new_session_item->setAccessibleName(QStringLiteral("New Terminal Session"));
        connect(new_session_item, &QAbstractButton::clicked, this, [this]() {
            createSession(initial_cwd_, {});
        });
        side_root->addWidget(new_session_item);

        auto* side_scroll = new QScrollArea(sidebar_);
        side_scroll->setWidgetResizable(true);
        side_scroll->setFrameShape(QFrame::NoFrame);
        side_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        side_scroll->setAutoFillBackground(false);
        side_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
        side_scroll->viewport()->setAutoFillBackground(false);
        side_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

        auto* list_host = new QWidget(side_scroll);
        list_host->setAutoFillBackground(false);
        list_host->setStyleSheet(QStringLiteral("background: transparent;"));
        side_list_layout_ = new QVBoxLayout(list_host);
        side_list_layout_->setContentsMargins(0, 2, 0, 4);
        side_list_layout_->setSpacing(4);
        side_scroll->setWidget(list_host);
        side_root->addWidget(side_scroll, 1);

        setSidebarWidget(sidebar_);
    }

    void setupContent() {
        auto* content = new QWidget(this);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(12, 24, 12, 14);
        root->setSpacing(10);

        // Sleek top bar with 16px Regular shell title, session +, Clear, Copy, Paste
        // pills to the left of FinnWindowControls (96px right margin reserve)
        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(6);

        auto* toggle_side_btn = new FinnIconButton(
            QStringLiteral("sidebar-toggle"), 28, 16, content);
        toggle_side_btn->setToolTip(QStringLiteral("Toggle Sidebar"));
        toggle_side_btn->setAccessibleName(QStringLiteral("Toggle Sidebar"));
        connect(toggle_side_btn, &QAbstractButton::clicked, this, [this]() {
            setSidebarCollapsed(!isSidebarCollapsed());
        });

        shell_title_ = new FinnLabel(
            QStringLiteral("Session 1"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, content);
        apply_header_16px_regular(shell_title_);

        auto* new_tab_btn = new FinnButton(
            QStringLiteral("+"), FinnButton::Variant::Secondary, {}, content);
        new_tab_btn->setFixedSize(32, 28);
        new_tab_btn->setToolTip(QStringLiteral("New Terminal Session"));
        new_tab_btn->setAccessibleName(QStringLiteral("New Terminal Session"));

        auto* zoom_out = new FinnButton(
            QStringLiteral("A-"), FinnButton::Variant::Ghost, {}, content);
        zoom_out->setFixedSize(36, 28);
        zoom_out->setAccessibleName(QStringLiteral("Decrease font size"));

        auto* zoom_in = new FinnButton(
            QStringLiteral("A+"), FinnButton::Variant::Ghost, {}, content);
        zoom_in->setFixedSize(36, 28);
        zoom_in->setAccessibleName(QStringLiteral("Increase font size"));

        auto* copy_btn = new FinnButton(
            QStringLiteral("Copy"), FinnButton::Variant::Secondary, {}, content);
        copy_btn->setFixedHeight(28);
        copy_btn->setAccessibleName(QStringLiteral("Copy selection or screen"));

        auto* paste_btn = new FinnButton(
            QStringLiteral("Paste"), FinnButton::Variant::Secondary, {}, content);
        paste_btn->setFixedHeight(28);
        paste_btn->setAccessibleName(QStringLiteral("Paste from clipboard"));

        auto* clear_btn = new FinnButton(
            QStringLiteral("Clear"), FinnButton::Variant::Secondary, {}, content);
        clear_btn->setFixedHeight(28);
        clear_btn->setAccessibleName(QStringLiteral("Clear scrollback"));

        header_row->addWidget(toggle_side_btn);
        header_row->addSpacing(2);
        header_row->addWidget(shell_title_, 1);
        header_row->addWidget(new_tab_btn);
        header_row->addWidget(zoom_out);
        header_row->addWidget(zoom_in);
        header_row->addWidget(copy_btn);
        header_row->addWidget(paste_btn);
        header_row->addWidget(clear_btn);
        root->addLayout(header_row);

        stack_ = new QStackedWidget(content);
        finn::ui::install_stack_animator(stack_);
        root->addWidget(stack_, 1);

        setContentWidget(content);

        connect(new_tab_btn, &QAbstractButton::clicked, this, [this]() {
            createSession(initial_cwd_, {});
        });
        connect(zoom_in, &QAbstractButton::clicked, this, [this]() {
            if (auto* t = activeTerminal()) t->zoomIn();
        });
        connect(zoom_out, &QAbstractButton::clicked, this, [this]() {
            if (auto* t = activeTerminal()) t->zoomOut();
        });
        connect(copy_btn, &QAbstractButton::clicked, this, [this]() {
            if (auto* t = activeTerminal()) t->copySelectionOrScreen();
        });
        connect(paste_btn, &QAbstractButton::clicked, this, [this]() {
            if (auto* t = activeTerminal()) t->pasteClipboard();
        });
        connect(clear_btn, &QAbstractButton::clicked, this, [this]() {
            if (auto* t = activeTerminal()) t->clearScrollback();
        });
    }

    TerminalWidget* activeTerminal() const {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(sessions_.size())) {
            return nullptr;
        }
        return sessions_[static_cast<std::size_t>(current_idx_)].widget;
    }

    void createSession(const QString& cwd, const QString& exec_cmd) {
        const int num = ++session_counter_;
        const QString title = QStringLiteral("Session %1").arg(num);
        auto* term = new TerminalWidget(cwd, exec_cmd, stack_);
        stack_->addWidget(term);
        sessions_.push_back({title, term});
        const int idx = static_cast<int>(sessions_.size()) - 1;

        connect(term, &TerminalWidget::titleChanged, this,
                [this, term](const QString& osc_title) {
                    for (std::size_t i = 0; i < sessions_.size(); ++i) {
                        if (sessions_[i].widget == term) {
                            sessions_[i].title = osc_title.trimmed().isEmpty()
                                                     ? QStringLiteral("Session %1").arg(i + 1)
                                                     : osc_title.trimmed();
                            if (static_cast<int>(i) == current_idx_) {
                                const QString elided = shell_title_->fontMetrics().elidedText(
                                    sessions_[i].title, Qt::ElideMiddle, 220);
                                shell_title_->setText(elided);
                                shell_title_->setToolTip(sessions_[i].title);
                                setWindowTitleText(sessions_[i].title);
                            }
                            rebuildSidebarList();
                            break;
                        }
                    }
                });
        connect(term, &TerminalWidget::toastRequested, this,
                [this](const QString& msg) { showToast(msg); });
        connect(term, &TerminalWidget::processExited, this,
                [this, term](int) { closeSessionWidget(term); });

        switchToSession(idx);
    }

    void switchToSession(int idx) {
        if (idx < 0 || idx >= static_cast<int>(sessions_.size())) return;
        current_idx_ = idx;
        const auto& s = sessions_[static_cast<std::size_t>(idx)];
        stack_->setCurrentWidget(s.widget);
        const QString elided = shell_title_->fontMetrics().elidedText(
            s.title, Qt::ElideMiddle, 220);
        shell_title_->setText(elided);
        shell_title_->setToolTip(s.title);
        setWindowTitleText(s.title);
        rebuildSidebarList();
        s.widget->setFocus();
    }

    void closeSessionWidget(TerminalWidget* term) {
        for (std::size_t i = 0; i < sessions_.size(); ++i) {
            if (sessions_[i].widget == term) {
                stack_->removeWidget(term);
                term->deleteLater();
                sessions_.erase(sessions_.begin() + static_cast<qsizetype>(i));
                if (sessions_.empty()) {
                    close();
                    return;
                }
                if (current_idx_ >= static_cast<int>(sessions_.size())) {
                    current_idx_ = static_cast<int>(sessions_.size()) - 1;
                }
                switchToSession(current_idx_);
                return;
            }
        }
    }

    void rebuildSidebarList() {
        while (QLayoutItem* item = side_list_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        const QString q =
            sidebar_search_ ? sidebar_search_->text().trimmed() : QString();

        for (std::size_t i = 0; i < sessions_.size(); ++i) {
            const auto& s = sessions_[i];
            if (!q.isEmpty() && !s.title.contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            const bool active = (static_cast<int>(i) == current_idx_);
            auto* item = new FinnNavItem(QStringLiteral("terminal-square"), s.title, sidebar_);
            item->setBadgeColor(active ? finn::ui::nav_accent_light()
                                       : finn::ui::nav_neutral_light(),
                                active ? finn::ui::nav_accent_dark()
                                       : finn::ui::nav_neutral_dark());
            item->setSelected(active);
            const int target_idx = static_cast<int>(i);
            connect(item, &QAbstractButton::clicked, this,
                    [this, target_idx]() { switchToSession(target_idx); });
            side_list_layout_->addWidget(item);
        }

        // Quick commands share the neutral nav badge; the glyph names the tool.
        auto* hdr = new FinnLabel(
            QStringLiteral("Quick commands"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, sidebar_);
        hdr->setContentsMargins(6, 10, 6, 2);
        side_list_layout_->addWidget(hdr);

        struct QuickCmd {
            QString label;
            QString cmd;
            QString icon;
        };
        const QVector<QuickCmd> cmds = {
            {QStringLiteral("System Kernel"), QStringLiteral("uname -a"),
             QStringLiteral("info")},
            {QStringLiteral("Disk Usage"), QStringLiteral("df -h"),
             QStringLiteral("folder")},
            {QStringLiteral("Network Addresses"), QStringLiteral("ip -br addr"),
             QStringLiteral("internet")},
            {QStringLiteral("Systemd Units"), QStringLiteral("systemctl list-units --type=service --state=running"),
             QStringLiteral("settings-02")},
        };
        for (const auto& qc : cmds) {
            if (!q.isEmpty() &&
                !qc.label.contains(q, Qt::CaseInsensitive) &&
                !qc.cmd.contains(q, Qt::CaseInsensitive)) {
                continue;
            }
            auto* item = new FinnNavItem(qc.icon, qc.label, sidebar_);
            item->setBadgeColor(finn::ui::nav_neutral_light(),
                               finn::ui::nav_neutral_dark());
            item->setToolTip(qc.cmd);
            const QString cmd_str = qc.cmd;
            connect(item, &QAbstractButton::clicked, this, [this, cmd_str]() {
                if (auto* t = activeTerminal()) t->sendCommandLine(cmd_str);
            });
            side_list_layout_->addWidget(item);
        }

        side_list_layout_->addStretch(1);
    }

    QString initial_cwd_;
    QString initial_exec_;
    int session_counter_ = 0;
    int current_idx_ = -1;
    std::vector<SessionEntry> sessions_;

    QWidget* sidebar_ = nullptr;
    FinnSearchField* sidebar_search_ = nullptr;
    QVBoxLayout* side_list_layout_ = nullptr;
    FinnLabel* shell_title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
};

}  // namespace

#include "main.moc"

int main(int argc, char** argv) {
    QString cwd;
    QString exec_cmd;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-terminal 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-terminal [--cwd <dir>] [-e <cmd>]");
            return 0;
        }
        if (std::strcmp(argv[i], "--cwd") == 0 && i + 1 < argc) {
            cwd = QString::fromLocal8Bit(argv[++i]);
        } else if (std::strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            if (i + 2 == argc) {
                exec_cmd = QString::fromLocal8Bit(argv[i + 1]);
            } else {
                QStringList quoted;
                quoted.reserve(argc - (i + 1));
                for (int j = i + 1; j < argc; ++j) {
                    QString part = QString::fromLocal8Bit(argv[j]);
                    part.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
                    quoted.push_back(QLatin1Char('\'') + part + QLatin1Char('\''));
                }
                exec_cmd = quoted.join(QLatin1Char(' '));
            }
            break;
        }
    }

    FinnApplication app(argc, argv, QStringLiteral("terminal"),
                        QStringLiteral("Finn Terminal"));
    TerminalWindow win(cwd, exec_cmd);
    win.show();
    return app.exec();
}
