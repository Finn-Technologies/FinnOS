#include <FinnSDK/FinnSDK.hpp>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace {

class CounterWindow final : public finn::ui::FinnWindow {
public:
    CounterWindow() : FinnWindow(QStringLiteral("Counter"), QStringLiteral("native-apps")) {
        resize(720, 480);

        auto* sidebar = new finn::ui::FinnSidebar(196, this);
        sidebar->addSectionHeader(QStringLiteral("Workspace"));
        sidebar->addItem(QStringLiteral("overview"), QStringLiteral("home-09"),
                         QStringLiteral("Overview"));
        sidebar->addItem(QStringLiteral("about"), QStringLiteral("info"),
                         QStringLiteral("SDK Info"));
        sidebar->addStretch();
        setSidebarWidget(sidebar);

        auto* badge = new finn::ui::FinnBadge(QStringLiteral("FinnSDK Qt6"),
                                              finn::ui::FinnBadge::Tone::Accent, this);
        auto* header_holder = new QWidget(this);
        auto* header_row = new QHBoxLayout(header_holder);
        header_row->setContentsMargins(0, 0, 0, 0);
        header_row->addStretch(1);
        header_row->addWidget(badge);
        setHeaderWidget(header_holder);

        auto* content = new QWidget(this);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(28, 24, 28, 24);
        root->setSpacing(14);

        auto* heading = new finn::ui::FinnLabel(
            QStringLiteral("A native FinnOS Qt6 app"),
            finn::ui::FinnLabel::Role::TitleLarge,
            finn::ui::FinnLabel::Tone::Ink, content);
        auto* subtitle = new finn::ui::FinnLabel(
            QStringLiteral("Built with C++20, FinnUI tokens, and native Wayland CSD."),
            finn::ui::FinnLabel::Role::Body,
            finn::ui::FinnLabel::Tone::Dim, content);
        root->addWidget(heading);
        root->addWidget(subtitle);

        auto* card = new finn::ui::FinnCard(finn::ui::kRadiusLg, false, content);
        auto* card_col = new QVBoxLayout(card);
        card_col->setContentsMargins(18, 16, 18, 16);
        card_col->setSpacing(12);

        count_label_ = new finn::ui::FinnLabel(
            QStringLiteral("Clicked 0 times"),
            finn::ui::FinnLabel::Role::Headline,
            finn::ui::FinnLabel::Tone::Ink, card);
        progress_ = new finn::ui::FinnProgressBar(6, card);
        progress_->setValue(0.0);

        auto* actions = new QHBoxLayout();
        actions->setSpacing(8);
        auto* inc_btn = new finn::ui::FinnButton(
            QStringLiteral("Increment"), finn::ui::FinnButton::Variant::Primary,
            {}, card);
        auto* reset_btn = new finn::ui::FinnButton(
            QStringLiteral("Reset"), finn::ui::FinnButton::Variant::Secondary,
            QStringLiteral("refresh"), card);
        actions->addWidget(inc_btn);
        actions->addWidget(reset_btn);
        actions->addStretch(1);

        card_col->addWidget(count_label_);
        card_col->addWidget(progress_);
        card_col->addLayout(actions);
        root->addWidget(card);

        auto* dark_toggle = new finn::ui::FinnToggle(
            finn::ui::FinnThemeManager::instance().isDark(), content);
        auto* dark_row = new finn::ui::FinnSettingRow(
            QStringLiteral("Follow Dark Mode"),
            QStringLiteral("Live-reloads across all running FinnOS surfaces"),
            QStringLiteral("moon"), dark_toggle, content);
        root->addWidget(dark_row);
        root->addStretch(1);
        setContentWidget(content);

        QObject::connect(inc_btn, &QAbstractButton::clicked, this, [this]() {
            ++count_;
            count_label_->setText(QStringLiteral("Clicked %1 times").arg(count_));
            progress_->setValue(static_cast<double>(count_ % 11) / 10.0);
            if (count_ == 5) {
                showToast(QStringLiteral("Reached 5 clicks"),
                          finn::ui::FinnToast::Severity::Success);
            }
        });

        QObject::connect(reset_btn, &QAbstractButton::clicked, this, [this]() {
            count_ = 0;
            count_label_->setText(QStringLiteral("Clicked 0 times"));
            progress_->setValue(0.0);
        });

        QObject::connect(dark_toggle, &QAbstractButton::toggled, this,
                         [](bool on) {
                             finn::ui::FinnThemeManager::instance().setDark(on);
                         });

        QObject::connect(sidebar, &finn::ui::FinnSidebar::selected, this,
                         [heading, subtitle](const QString& id) {
                             if (id == QStringLiteral("about")) {
                                 heading->setText(QStringLiteral("FinnSDK Architecture"));
                                 subtitle->setText(QStringLiteral(
                                     "Native Qt6 QProxyStyle + QPlatformTheme + FinnWindow."));
                             } else {
                                 heading->setText(QStringLiteral("A native FinnOS Qt6 app"));
                                 subtitle->setText(QStringLiteral(
                                     "Built with C++20, FinnUI tokens, and native Wayland CSD."));
                             }
                         });
    }

private:
    int count_ = 0;
    finn::ui::FinnLabel* count_label_ = nullptr;
    finn::ui::FinnProgressBar* progress_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    finn::ui::FinnApplication app(argc, argv, QStringLiteral("counter"),
                                  QStringLiteral("Counter"));
    CounterWindow window;
    window.show();
    return app.exec();
}
