// Native POSIX PTY + VT100/ANSI QWidget terminal renderer for recovery-mode.
// Reuses the Finn Terminal PTY/VT100 architecture (apps/terminal/terminal_view.*)
// as a pure Qt6::Widgets surface so it works identically on linuxfb, eglfs,
// Wayland, and offscreen without QML/OpenGL dependencies.

#ifndef FINN_RECOVERY_TERMINAL_WIDGET_HPP
#define FINN_RECOVERY_TERMINAL_WIDGET_HPP

#include <QColor>
#include <QFont>
#include <QSocketNotifier>
#include <QString>
#include <QVector>
#include <QWidget>
#include <sys/types.h>

namespace finn::recovery {

class FinnTerminalWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FinnTerminalWidget(QWidget* parent = nullptr);
    ~FinnTerminalWidget() override;

    QString title() const { return title_; }
    bool running() const { return pty_fd_ >= 0; }
    int fontSize() const { return font_size_; }
    void setFontSize(int px);

    void startShell(const QString& workdir = {});
    void sendText(const QString& text);
    void sendControl(const QString& key);
    void clearScreen();

signals:
    void titleChanged(const QString& title);
    void runningChanged(bool running);
    void shellExited(int code);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct Cell {
        QChar ch = QLatin1Char(' ');
        QColor fg;
        QColor bg;
        bool bold = false;
        bool dim = false;
        bool underline = false;
        bool inverse = false;
    };

    void updateMetrics();
    void resizeGrid(int new_cols, int new_rows);
    void readFromPty();
    void feedBytes(const QByteArray& data);
    void putChar(QChar ch);
    void handleCsi(char final_ch, const QString& params, bool priv);
    void handleEsc(QChar ch);
    void newLine();
    void reverseIndex();
    void scrollUpRegion(int top, int bottom, int count, bool push_scrollback);
    void scrollDownRegion(int top, int bottom, int count);
    void enterAltScreen();
    void leaveAltScreen();
    void writePty(const QByteArray& bytes);
    QColor ansiColor(int idx, bool bright) const;
    QColor xterm256Color(int idx) const;

    int pty_fd_ = -1;
    pid_t child_pid_ = -1;
    QSocketNotifier* notifier_ = nullptr;

    QFont font_;
    int font_size_ = 13;
    int cell_w_ = 8;
    int cell_h_ = 18;
    int cols_ = 80;
    int rows_ = 24;
    int cur_x_ = 0;
    int cur_y_ = 0;
    int saved_x_ = 0;
    int saved_y_ = 0;
    int scroll_top_ = 0;
    int scroll_bottom_ = 23;
    bool cursor_visible_ = true;
    bool app_cursor_keys_ = false;
    bool alt_screen_ = false;
    bool wrap_pending_ = false;

    QColor cur_fg_;
    QColor cur_bg_;
    bool cur_bold_ = false;
    bool cur_dim_ = false;
    bool cur_underline_ = false;
    bool cur_inverse_ = false;

    enum class ParseState { Ground, Esc, Csi, Osc, EscCharset } state_ = ParseState::Ground;
    QString esc_buf_;
    QByteArray utf8_pending_;

    QVector<QVector<Cell>> grid_;
    QVector<QVector<Cell>> alt_saved_grid_;
    int alt_saved_x_ = 0;
    int alt_saved_y_ = 0;
    QVector<QVector<Cell>> scrollback_;
    int scroll_offset_ = 0;
    QString title_ = QStringLiteral("Finn Terminal");
};

}  // namespace finn::recovery

#endif  // FINN_RECOVERY_TERMINAL_WIDGET_HPP
