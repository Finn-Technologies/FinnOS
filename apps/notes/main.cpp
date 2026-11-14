// Finn Notes: native markdown & text editor redesigned to match Figma Settings (20:64) 1:1.
// Uses ChromeMode::FigmaSplit, 250px translucent sidebar with 16px Regular header,
// r=100 "Search notes" pill, + New Note row (#007BFF badge), and open/saved notes
// as FinnNavItems with circular badges (#F59E0B active/modified, #5856D6 saved).

#include <FinnSDK/FinnSDK.hpp>

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

using namespace finn::ui;

QString rgba_css(const QColor& c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

QString scrollbar_qss(const FinnTokens& t) {
    const QColor thumb(t.ink.red(), t.ink.green(), t.ink.blue(), t.dark ? 68 : 52);
    const QColor hover(t.ink.red(), t.ink.green(), t.ink.blue(), t.dark ? 112 : 88);
    return QStringLiteral(
               "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
               "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 28px; }"
               "QScrollBar::handle:vertical:hover { background: %2; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
               "QScrollBar:horizontal { background: transparent; height: 8px; margin: 1px; }"
               "QScrollBar::handle:horizontal { background: %1; border-radius: 4px; min-width: 28px; }"
               "QScrollBar::handle:horizontal:hover { background: %2; }"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }")
        .arg(rgba_css(thumb), rgba_css(hover));
}

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

struct NoteDoc {
    QString path;
    QString title;
    QString text;
    bool dirty = false;
    bool markdown = true;
    int cursor_pos = 0;
};

QString notes_home_dir() {
    const QString docs = QDir::home().filePath(QStringLiteral("Documents/Notes"));
    QDir().mkpath(docs);
    return docs;
}

class NotesWindow final : public FinnWindow {
    Q_OBJECT
public:
    explicit NotesWindow(const QStringList& initial_files)
        : FinnWindow(QStringLiteral("Finn Notes"), QStringLiteral("notes")) {
        setMinimumSize(800, 500);
        resize(940, 600);
        setChromeMode(ChromeMode::FigmaSplit);

        setupSidebar();
        setupContent();

        new QShortcut(QKeySequence::New, this, [this]() { newTab(); });
        new QShortcut(QKeySequence::Open, this, [this]() { openDialog(); });
        new QShortcut(QKeySequence::Save, this, [this]() { saveCurrent(false); });
        new QShortcut(QKeySequence::SaveAs, this, [this]() { saveCurrent(true); });
        new QShortcut(QKeySequence::Close, this, [this]() { closeCurrentTab(); });
        new QShortcut(QKeySequence::Find, this, [this]() {
            find_btn_->setChecked(!find_bar_->isVisible());
            toggleFindBar(find_btn_->isChecked());
        });
        new QShortcut(QKeySequence(Qt::Key_Escape), this, [this]() {
            if (find_bar_->isVisible()) {
                find_btn_->setChecked(false);
                toggleFindBar(false);
            }
        });

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, [this](const FinnTokens&) {
                    applyEditorStyle();
                    syncPreview();
                });

        applyEditorStyle();

        for (const QString& f : initial_files) {
            openFile(f);
        }
        if (docs_.empty()) {
            newTab();
        } else {
            rebuildSidebarList();
        }
    }

protected:
    void closeEvent(QCloseEvent* e) override {
        syncCurrentDocFromEditor();
        for (std::size_t i = 0; i < docs_.size(); ++i) {
            if (!docs_[i].dirty) continue;
            switchToTab(static_cast<int>(i));
            const bool save_it = FinnDialog::confirm(
                this,
                QStringLiteral("Unsaved changes"),
                QStringLiteral("Save changes to \"%1\" before closing?").arg(docs_[i].title),
                QStringLiteral("Save Changes"));
            if (save_it && !saveCurrent(false)) {
                e->ignore();
                return;
            }
        }
        FinnWindow::closeEvent(e);
    }

private:
    void setupSidebar() {
        // Left 250px frosted sidebar matching Figma Settings (20:64)
        sidebar_ = new QWidget(this);
        sidebar_->setFixedWidth(250);
        auto* side_root = new QVBoxLayout(sidebar_);
        side_root->setContentsMargins(12, 24, 12, 12);
        side_root->setSpacing(10);

        auto* hdr_lbl = new FinnLabel(
            QStringLiteral("Notes"), FinnLabel::Role::Title,
            FinnLabel::Tone::Ink, sidebar_);
        apply_header_16px_regular(hdr_lbl);
        side_root->addWidget(hdr_lbl);

        sidebar_search_ = new FinnSearchField(QStringLiteral("Search notes"), sidebar_);
        sidebar_search_->setFixedHeight(30);
        sidebar_search_->setPillMode(true);
        connect(sidebar_search_, &FinnSearchField::textChanged,
                this, [this](const QString&) { rebuildSidebarList(); });
        side_root->addWidget(sidebar_search_);

        // Sleek + New Note row with #007BFF circular badge (edit-01)
        new_note_item_ = new FinnNavItem(
            QStringLiteral("edit-01"), QStringLiteral("+ New Note"), sidebar_);
        new_note_item_->setBadgeColor(QColor(0x00, 0x7B, 0xFF));
        connect(new_note_item_, &QAbstractButton::clicked, this, [this]() { newTab(); });
        side_root->addWidget(new_note_item_);

        side_scroll_ = new QScrollArea(sidebar_);
        side_scroll_->setWidgetResizable(true);
        side_scroll_->setFrameShape(QFrame::NoFrame);
        side_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        side_scroll_->setAutoFillBackground(false);
        side_scroll_->viewport()->setAutoFillBackground(false);

        auto* list_host = new QWidget(side_scroll_);
        list_host->setAutoFillBackground(false);
        notes_list_layout_ = new QVBoxLayout(list_host);
        notes_list_layout_->setContentsMargins(0, 2, 0, 4);
        notes_list_layout_->setSpacing(4);
        side_scroll_->setWidget(list_host);
        side_root->addWidget(side_scroll_, 1);

        setSidebarWidget(sidebar_);
    }

    void setupContent() {
        auto* content = new QWidget(this);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(12, 24, 12, 14);
        root->setSpacing(10);

        // Top header row (with 96px right margin for floating FinnWindowControls):
        // editable document title (16px Regular), subtle status caption, and
        // compact pill buttons (Save, Open..., Preview, Find).
        auto* header_row = new QHBoxLayout();
        header_row->setContentsMargins(0, 0, 96, 0);
        header_row->setSpacing(6);

        title_edit_ = new QLineEdit(content);
        title_edit_->setFrame(false);
        title_edit_->setPlaceholderText(QStringLiteral("Untitled note"));
        title_edit_->setMinimumWidth(140);
        connect(title_edit_, &QLineEdit::editingFinished, this, [this]() {
            if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return;
            const QString next = title_edit_->text().trimmed();
            if (next.isEmpty()) return;
            auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
            if (doc.title != next) {
                doc.title = next;
                doc.markdown = doc.title.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
                               doc.title.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive);
                doc.dirty = true;
                setWindowTitleText(QStringLiteral("%1 — Finn Notes").arg(doc.title));
                rebuildSidebarList();
                updateStatus();
            }
        });

        status_caption_ = new FinnLabel(
            QStringLiteral("Saved · 0 words"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, content);

        save_btn_ = new FinnButton(
            QStringLiteral("Save"), FinnButton::Variant::Primary, {}, content);
        save_btn_->setFixedHeight(28);

        auto* open_btn = new FinnButton(
            QStringLiteral("Open..."), FinnButton::Variant::Secondary, {}, content);
        open_btn->setFixedHeight(28);

        preview_btn_ = new FinnButton(
            QStringLiteral("Preview"), FinnButton::Variant::Secondary, {}, content);
        preview_btn_->setCheckable(true);
        preview_btn_->setFixedHeight(28);

        find_btn_ = new FinnButton(
            QStringLiteral("Find"), FinnButton::Variant::Secondary,
            QStringLiteral("search-01"), content);
        find_btn_->setCheckable(true);
        find_btn_->setFixedHeight(28);

        header_row->addWidget(title_edit_, 1);
        header_row->addWidget(status_caption_);
        header_row->addSpacing(4);
        header_row->addWidget(save_btn_);
        header_row->addWidget(open_btn);
        header_row->addWidget(preview_btn_);
        header_row->addWidget(find_btn_);
        root->addLayout(header_row);

        // Collapsible inline Find bar inside an r=6 card when Find is toggled
        find_bar_ = new FinnCard(kRadiusMd, false, content);
        find_bar_->hide();
        auto* find_row = new QHBoxLayout(find_bar_);
        find_row->setContentsMargins(10, 6, 10, 6);
        find_row->setSpacing(6);
        find_input_ = new FinnSearchField(QStringLiteral("Find in current note..."), find_bar_);
        find_input_->setFixedHeight(28);
        find_input_->setPillMode(true);
        auto* prev_btn = new FinnButton(
            QStringLiteral("Prev"), FinnButton::Variant::Secondary, {}, find_bar_);
        prev_btn->setFixedHeight(26);
        auto* next_btn = new FinnButton(
            QStringLiteral("Next"), FinnButton::Variant::Secondary, {}, find_bar_);
        next_btn->setFixedHeight(26);
        auto* mono_btn = new FinnButton(
            QStringLiteral("Mono"), FinnButton::Variant::Ghost, {}, find_bar_);
        mono_btn->setCheckable(true);
        mono_btn->setFixedHeight(26);
        auto* hide_find_btn = new FinnButton(
            QStringLiteral("Done"), FinnButton::Variant::Ghost, {}, find_bar_);
        hide_find_btn->setFixedHeight(26);

        find_row->addWidget(find_input_, 1);
        find_row->addWidget(prev_btn);
        find_row->addWidget(next_btn);
        find_row->addWidget(mono_btn);
        find_row->addWidget(hide_find_btn);
        root->addWidget(find_bar_);

        // Editor body inside a clean Settings-style r=6 (t.tile) card with generous 16px padding
        editor_card_ = new FinnCard(kRadiusMd, false, content);
        auto* card_lay = new QVBoxLayout(editor_card_);
        card_lay->setContentsMargins(16, 16, 16, 16);
        card_lay->setSpacing(8);

        editor_split_ = new QSplitter(Qt::Horizontal, editor_card_);
        editor_ = new QPlainTextEdit(editor_split_);
        editor_->setFrameShape(QFrame::NoFrame);
        editor_->setTabStopDistance(28.0);
        editor_->setPlaceholderText(QStringLiteral("Start writing in Markdown or plain text..."));

        preview_ = new QTextBrowser(editor_split_);
        preview_->setFrameShape(QFrame::NoFrame);
        preview_->setOpenExternalLinks(true);
        preview_->hide();
        editor_split_->setSizes({520, 380});
        card_lay->addWidget(editor_split_, 1);

        // Subtle footer inside the editor card
        auto* footer_row = new QHBoxLayout();
        footer_row->setContentsMargins(0, 4, 0, 0);
        footer_row->setSpacing(8);
        path_label_ = new FinnLabel(
            QStringLiteral("Unsaved note"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, editor_card_);
        stats_label_ = new FinnLabel(
            QStringLiteral("Ln 1, Col 1"),
            FinnLabel::Role::Caption, FinnLabel::Tone::Mute, editor_card_);
        footer_row->addWidget(path_label_, 1);
        footer_row->addWidget(stats_label_);
        card_lay->addLayout(footer_row);

        root->addWidget(editor_card_, 1);
        setContentWidget(content);

        connect(save_btn_, &QAbstractButton::clicked, this, [this]() { saveCurrent(false); });
        connect(open_btn, &QAbstractButton::clicked, this, [this]() { openDialog(); });
        connect(preview_btn_, &QAbstractButton::toggled, this, [this](bool on) {
            preview_visible_ = on;
            preview_->setVisible(preview_visible_);
            if (preview_visible_) syncPreview();
        });
        connect(find_btn_, &QAbstractButton::toggled, this, [this](bool on) {
            toggleFindBar(on);
        });
        connect(hide_find_btn, &QAbstractButton::clicked, this, [this]() {
            find_btn_->setChecked(false);
            toggleFindBar(false);
        });
        connect(mono_btn, &QAbstractButton::toggled, this, [this](bool on) {
            mono_mode_ = on;
            applyEditorStyle();
        });
        connect(next_btn, &QAbstractButton::clicked, this, [this]() { findMatch(false); });
        connect(prev_btn, &QAbstractButton::clicked, this, [this]() { findMatch(true); });
        connect(find_input_, &FinnSearchField::submitted, this,
                [this](const QString&) { findMatch(false); });

        connect(editor_, &QPlainTextEdit::textChanged, this, [this]() {
            if (loading_tab_ || current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size()))
                return;
            auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
            doc.text = editor_->toPlainText();
            if (!doc.dirty) {
                doc.dirty = true;
                rebuildSidebarList();
            }
            updateStatus();
            if (preview_visible_) syncPreview();
        });
        connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
            updateStatus();
        });
    }

    void applyEditorStyle() {
        const auto& t = FinnThemeManager::instance().tokens();

        QFont hdr_f = finn_font(12.0, QFont::Normal);
        hdr_f.setPixelSize(16);
        title_edit_->setFont(hdr_f);
        title_edit_->setStyleSheet(
            QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; padding: 0px; }")
                .arg(t.ink.name()));

        const QFont ed_f = mono_mode_
                               ? finn_mono_font(11.5, QFont::Normal)
                               : finn_font(11.5, QFont::Normal);
        editor_->setFont(ed_f);
        preview_->setFont(finn_font(11.0, QFont::Normal));

        const QString sel_css = rgba_css(t.tile_on);
        const QString sb_css = scrollbar_qss(t);

        editor_->setStyleSheet(
            QStringLiteral(
                "QPlainTextEdit { background: transparent; color: %1; border: none; "
                "padding: 0px; selection-background-color: %2; selection-color: %1; }%3")
                .arg(t.ink.name(), sel_css, sb_css));

        preview_->setStyleSheet(
            QStringLiteral(
                "QTextBrowser { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 6px; padding: 12px; selection-background-color: %4; "
                "selection-color: %2; }%5")
                .arg(t.surface_canvas.name(), t.ink.name(), rgba_css(t.hair), sel_css, sb_css));

        const QString doc_dim = t.dark ? t.dim.name() : QStringLiteral("#666B75");
        const QString doc_rule = t.dark ? QStringLiteral("#353B48") : QStringLiteral("#E5E7EB");
        preview_->document()->setDefaultStyleSheet(
            QStringLiteral(
                "body { color: %1; }"
                "a { color: %2; }"
                "code, pre { background-color: %3; color: %1; border-radius: 4px; }"
                "blockquote { border-left: 3px solid %2; padding-left: 10px; color: %4; }"
                "hr { border: 1px solid %5; }")
                .arg(t.ink.name(), t.accent.name(), t.surface_input.name(),
                     doc_dim, doc_rule));

        if (side_scroll_) {
            side_scroll_->setStyleSheet(
                QStringLiteral("QScrollArea { background: transparent; border: none; }%1")
                    .arg(sb_css));
        }
    }

    void rebuildSidebarList() {
        while (QLayoutItem* item = notes_list_layout_->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }

        const QString query =
            sidebar_search_ ? sidebar_search_->text().trimmed() : QString();
        const QColor amber_badge(0xF5, 0x9E, 0x0B);   // #F59E0B for active/modified
        const QColor indigo_badge(0x58, 0x56, 0xD6);  // #5856D6 for saved markdown/text notes

        // 1. Open documents in memory
        QStringList open_paths;
        for (std::size_t i = 0; i < docs_.size(); ++i) {
            const auto& d = docs_[i];
            if (!d.path.isEmpty()) open_paths.push_back(d.path);
            if (!query.isEmpty() &&
                !d.title.contains(query, Qt::CaseInsensitive) &&
                !d.text.contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            const bool is_active = (static_cast<int>(i) == current_idx_);
            auto* nav = new FinnNavItem(QStringLiteral("notes"), d.title, sidebar_);
            nav->setBadgeColor((is_active || d.dirty) ? amber_badge : indigo_badge);
            if (d.dirty) {
                nav->setBadgeText(QStringLiteral("•"));
            }
            nav->setSelected(is_active);
            const int tab_idx = static_cast<int>(i);
            connect(nav, &QAbstractButton::clicked, this,
                    [this, tab_idx]() { switchToTab(tab_idx); });
            notes_list_layout_->addWidget(nav);
        }

        // 2. Saved files in ~/Documents/Notes not currently open
        const QDir dir(notes_home_dir());
        const auto entries = dir.entryInfoList(
            QStringList{QStringLiteral("*.md"), QStringLiteral("*.txt"),
                        QStringLiteral("*.log"), QStringLiteral("*.json")},
            QDir::Files | QDir::Readable, QDir::Time);
        for (const QFileInfo& fi : entries) {
            const QString abs = fi.absoluteFilePath();
            if (open_paths.contains(abs)) continue;
            if (!query.isEmpty() &&
                !fi.fileName().contains(query, Qt::CaseInsensitive)) {
                continue;
            }
            auto* nav = new FinnNavItem(QStringLiteral("notes"), fi.fileName(), sidebar_);
            nav->setBadgeColor(indigo_badge);
            nav->setToolTip(abs);
            connect(nav, &QAbstractButton::clicked, this,
                    [this, abs]() { openFile(abs); });
            notes_list_layout_->addWidget(nav);
        }

        notes_list_layout_->addStretch(1);
    }

    void syncCurrentDocFromEditor() {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return;
        auto& cur = docs_[static_cast<std::size_t>(current_idx_)];
        cur.text = editor_->toPlainText();
        cur.cursor_pos = editor_->textCursor().position();
    }

    void switchToTab(int idx) {
        if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;
        if (current_idx_ != idx && current_idx_ >= 0) {
            syncCurrentDocFromEditor();
        }
        current_idx_ = idx;
        loading_tab_ = true;
        const auto& doc = docs_[static_cast<std::size_t>(idx)];
        title_edit_->setText(doc.title);
        editor_->setPlainText(doc.text);
        QTextCursor c = editor_->textCursor();
        c.setPosition(qBound(0, doc.cursor_pos, static_cast<int>(doc.text.size())));
        editor_->setTextCursor(c);
        loading_tab_ = false;
        setWindowTitleText(QStringLiteral("%1 — Finn Notes").arg(doc.title));
        rebuildSidebarList();
        updateStatus();
        if (preview_visible_) syncPreview();
    }

    void newTab() {
        syncCurrentDocFromEditor();
        NoteDoc doc;
        doc.title = QStringLiteral("Untitled %1.md").arg(untitled_counter_++);
        docs_.push_back(std::move(doc));
        const int idx = static_cast<int>(docs_.size()) - 1;
        current_idx_ = idx;
        switchToTab(idx);
        editor_->setFocus();
    }

    void openDialog() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open Note or Text File"), notes_home_dir(),
            QStringLiteral("Notes & Code (*.md *.txt *.json *.yaml *.yml *.cpp *.hpp *.py *.sh *.conf);;All Files (*)"));
        if (!path.isEmpty()) openFile(path);
    }

    void openFile(const QString& raw_path) {
        const QFileInfo fi(raw_path);
        if (!fi.exists() || !fi.isFile()) {
            showToast(QStringLiteral("File not found: %1").arg(raw_path), FinnToast::Severity::Warning);
            return;
        }
        const QString abs = fi.absoluteFilePath();
        for (std::size_t i = 0; i < docs_.size(); ++i) {
            if (docs_[i].path == abs) {
                switchToTab(static_cast<int>(i));
                return;
            }
        }
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            showToast(QStringLiteral("Unable to open: %1").arg(f.errorString()), FinnToast::Severity::Error);
            return;
        }
        const QString content = QString::fromUtf8(f.readAll());
        syncCurrentDocFromEditor();
        if (docs_.size() == 1 && docs_[0].path.isEmpty() && !docs_[0].dirty &&
            docs_[0].text.isEmpty()) {
            docs_.clear();
            current_idx_ = -1;
        }
        NoteDoc doc;
        doc.path = abs;
        doc.title = fi.fileName();
        doc.text = content;
        doc.dirty = false;
        doc.markdown = abs.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
                       abs.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive);
        docs_.push_back(std::move(doc));
        const int idx = static_cast<int>(docs_.size()) - 1;
        current_idx_ = idx;
        switchToTab(idx);
    }

    bool saveCurrent(bool force_save_as) {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return false;
        syncCurrentDocFromEditor();
        auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
        QString target = doc.path;
        if (force_save_as || target.isEmpty()) {
            const QString suggested = QDir(notes_home_dir()).filePath(doc.title);
            target = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save Note"), suggested,
                QStringLiteral("Markdown (*.md);;Text Files (*.txt);;All Files (*)"));
            if (target.isEmpty()) return false;
        }

        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            showToast(QStringLiteral("Save failed: %1").arg(out.errorString()), FinnToast::Severity::Error);
            return false;
        }
        const QByteArray bytes = doc.text.toUtf8();
        if (out.write(bytes) != bytes.size() || !out.commit()) {
            showToast(QStringLiteral("Save failed: %1").arg(out.errorString()), FinnToast::Severity::Error);
            return false;
        }

        const QFileInfo fi(target);
        doc.path = fi.absoluteFilePath();
        doc.title = fi.fileName();
        doc.dirty = false;
        doc.markdown = doc.title.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);
        title_edit_->setText(doc.title);
        setWindowTitleText(QStringLiteral("%1 — Finn Notes").arg(doc.title));
        rebuildSidebarList();
        updateStatus();
        showToast(QStringLiteral("Saved %1").arg(doc.title), FinnToast::Severity::Success);
        return true;
    }

    void closeCurrentTab() {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return;
        syncCurrentDocFromEditor();
        auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
        if (doc.dirty) {
            const bool save_it = FinnDialog::confirm(
                this,
                QStringLiteral("Unsaved note"),
                QStringLiteral("Save \"%1\" before closing this note?").arg(doc.title),
                QStringLiteral("Save"));
            if (save_it && !saveCurrent(false)) return;
        }
        docs_.erase(docs_.begin() + current_idx_);
        if (docs_.empty()) {
            current_idx_ = -1;
            newTab();
            return;
        }
        if (current_idx_ >= static_cast<int>(docs_.size())) {
            current_idx_ = static_cast<int>(docs_.size()) - 1;
        }
        switchToTab(current_idx_);
    }

    void toggleFindBar(bool show_bar) {
        find_bar_->setVisible(show_bar);
        if (show_bar) {
            find_input_->focusInput();
        } else {
            editor_->setFocus();
        }
    }

    void findMatch(bool backward) {
        const QString needle = find_input_->text();
        if (needle.isEmpty()) return;
        QTextDocument::FindFlags flags;
        if (backward) flags |= QTextDocument::FindBackward;
        if (!editor_->find(needle, flags)) {
            QTextCursor c = editor_->textCursor();
            c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
            editor_->setTextCursor(c);
            if (!editor_->find(needle, flags)) {
                showToast(QStringLiteral("No match for \"%1\"").arg(needle), FinnToast::Severity::Info);
            }
        }
    }

    void syncPreview() {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return;
        const auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
        if (doc.markdown) {
            preview_->setMarkdown(editor_->toPlainText());
        } else {
            preview_->setPlainText(editor_->toPlainText());
        }
    }

    void updateStatus() {
        if (current_idx_ < 0 || current_idx_ >= static_cast<int>(docs_.size())) return;
        const auto& doc = docs_[static_cast<std::size_t>(current_idx_)];
        path_label_->setText(doc.path.isEmpty()
                                 ? QStringLiteral("~/Documents/Notes/%1 (unsaved)").arg(doc.title)
                                 : doc.path);
        save_btn_->setEnabled(doc.dirty || doc.path.isEmpty());

        const QTextCursor c = editor_->textCursor();
        const int line = c.blockNumber() + 1;
        const int col = c.positionInBlock() + 1;
        const QString text = editor_->toPlainText();
        const qsizetype words =
            text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();

        status_caption_->setText(
            QStringLiteral("%1 · %2 words")
                .arg(doc.dirty ? QStringLiteral("Modified") : QStringLiteral("Saved"))
                .arg(words));
        stats_label_->setText(
            QStringLiteral("Ln %1, Col %2 · %3 chars")
                .arg(line)
                .arg(col)
                .arg(text.size()));
    }

    std::vector<NoteDoc> docs_;
    int current_idx_ = -1;
    int untitled_counter_ = 1;
    bool loading_tab_ = false;
    bool preview_visible_ = false;
    bool mono_mode_ = false;

    QWidget* sidebar_ = nullptr;
    FinnSearchField* sidebar_search_ = nullptr;
    FinnNavItem* new_note_item_ = nullptr;
    QScrollArea* side_scroll_ = nullptr;
    QVBoxLayout* notes_list_layout_ = nullptr;

    QLineEdit* title_edit_ = nullptr;
    FinnLabel* status_caption_ = nullptr;
    FinnButton* save_btn_ = nullptr;
    FinnButton* preview_btn_ = nullptr;
    FinnButton* find_btn_ = nullptr;

    FinnCard* find_bar_ = nullptr;
    FinnSearchField* find_input_ = nullptr;
    FinnCard* editor_card_ = nullptr;
    QSplitter* editor_split_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    FinnLabel* path_label_ = nullptr;
    FinnLabel* stats_label_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("finn-notes 0.2.0");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("Usage: finn-notes [file...]");
            return 0;
        }
    }
    FinnApplication app(argc, argv, QStringLiteral("notes"), QStringLiteral("Finn Notes"));
    const QStringList args = app.arguments().mid(1);
    NotesWindow win(args);
    win.show();
    return app.exec();
}

#include "main.moc"
