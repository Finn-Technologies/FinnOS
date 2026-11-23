// Native POSIX PTY + VT100/ANSI Qt Quick terminal renderer for finn-terminal.

#ifndef FINN_APPS_TERMINAL_VIEW_HPP
#define FINN_APPS_TERMINAL_VIEW_HPP

#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>
#include <QSocketNotifier>
#include <QString>
#include <QVector>
#include <sys/types.h>

namespace finn::apps {

class FinnTerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)

public:
    explicit FinnTerminalView(QQuickItem* parent = nullptr);
    ~FinnTerminalView() override;

    QString title() const { return title_; }
    bool running() const { return pty_fd_ >= 0; }
    int fontSize() const { return font_size_; }
    void setFontSize(int px);

    void paint(QPainter* painter) override;

    Q_INVOKABLE void startShell(const QString& workdir = {});
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void sendControl(const QString& key);
    Q_INVOKABLE void clearScreen();

signals:
    void titleChanged();
    void runningChanged();
    void fontSizeChanged();
    void shellExited(int code);

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Cell {
        QChar ch = QLatin1Char(' ');
        QColor fg;
        bool bold = false;
    };

    void updateMetrics();
    void resizeGrid(int new_cols, int new_rows);
    void readFromPty();
    void feedBytes(const QByteArray& data);
    void putChar(QChar ch);
    void handleCsi(char final_ch, const QString& params);
    void newLine();
    void scrollUp();
    void writePty(const QByteArray& bytes);
    QColor ansiColor(int idx, bool bright) const;

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

    QColor cur_fg_;
    bool cur_bold_ = false;
    enum class ParseState { Ground, Esc, Csi, Osc } state_ = ParseState::Ground;
    QString esc_buf_;

    QVector<QVector<Cell>> grid_;
    QVector<QVector<Cell>> scrollback_;
    int scroll_offset_ = 0;
    QString title_ = QStringLiteral("Shell");
};

}  // namespace finn::apps

#endif  // FINN_APPS_TERMINAL_VIEW_HPP
