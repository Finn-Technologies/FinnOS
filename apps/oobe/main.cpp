// FinnOS First-Time Setup (finn-oobe).
// Full-screen minimalist onboarding built with FinnUI matching Figma First Time
// Setup ("hi", "How do we talk?") and the 6-ring 3D-to-slinky ribbon animation.

#include <FinnSDK/FinnSDK.hpp>

#include "lamp_widget.hpp"
#include "common/paths.hpp"
#include "shell/full_oobe_flow.hpp"
#include "shell/oobe_capability_probe.hpp"
#include "system/desktop_services.hpp"

#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <unistd.h>

namespace {

using namespace finn::ui;

QFont oobe_font(int pixel_size, QFont::Weight weight = QFont::Normal) {
    return finn_font_px(pixel_size, weight);
}

// Compute exact wrapped height for a fixed-width QLabel so switching from a
// 1-line English prompt to a 2-3 line translation never clips vertically.
void set_wrapped_label_text(QLabel* lbl, const QString& text) {
    if (!lbl) return;
    lbl->setText(text);
    const int w = lbl->width() > 0 ? lbl->width() : lbl->maximumWidth();
    if (w > 0 && w < QWIDGETSIZE_MAX) {
        const QFontMetrics fm(lbl->font());
        const QRect br = fm.boundingRect(
            0, 0, w, 1000,
            static_cast<int>(lbl->alignment()) | Qt::TextWordWrap, text);
        lbl->setMinimumHeight(std::max(fm.lineSpacing() + 6, br.height() + 10));
    }
    lbl->updateGeometry();
}

// Form factors share the shell's breakpoints and override variable so a
// phone/tablet capture is reproducible on the desktop (docs/design-language.md
// §1, src/shell/shell_layout.hpp). The Figma OOBE frames are 1280x832 desktop;
// phone and tablet are derived layouts, not a scaled desktop frame.
enum class OobeForm { Phone, Tablet, Desktop };

// `finn-sdk simulate --headless` exports FINN_SIM_WIDTH/HEIGHT and
// FINN_FORM_FACTOR. Prefer them over the physical screen so a simulated phone
// gets the phone layout even when captured from a desktop monitor.
int oobe_screen_width() {
    bool ok = false;
    const int simulated = qgetenv("FINN_SIM_WIDTH").toInt(&ok);
    if (ok && simulated > 0) return simulated;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const int w = screen->availableGeometry().width();
        if (w > 0) return w;
    }
    return 1280;  // the Figma OOBE frame width, and an explicit fallback
}

OobeForm oobe_form() {
    const QByteArray override = qgetenv("FINN_FORM_FACTOR").trimmed().toLower();
    if (override == "phone") return OobeForm::Phone;
    if (override == "tablet") return OobeForm::Tablet;
    if (override == "desktop") return OobeForm::Desktop;
    const int width = oobe_screen_width();
    if (width <= finn::ui::breakpoints::bp_phone_max) return OobeForm::Phone;
    if (width <= finn::ui::breakpoints::bp_tablet_max) return OobeForm::Tablet;
    return OobeForm::Desktop;
}

int oobe_gutter() {
    switch (oobe_form()) {
        case OobeForm::Phone: return 16;   // 16 dp page margin, 44 dp touch rows
        case OobeForm::Tablet: return 24;
        case OobeForm::Desktop: return 80; // Figma 36:53 page inset
    }
    return 80;
}

// Figma's content column is 350 px. On a narrow screen it must fit between
// the form-factor gutters; on desktop it stays exactly 350 px.
int oobe_content_width() {
    return qMax(240, qMin(350, oobe_screen_width() - 2 * oobe_gutter()));
}

// The flow model's step order is the product contract (connectivity and
// account before personalisation). The painted page order keeps the Figma
// "hi." -> language continuity, so these two tables are the only place the
// mapping is allowed to live.
using OobeStep = finn::shell::oobe::Step;

int ui_index_for_step(OobeStep step) {
    return static_cast<int>(step);
}

OobeStep step_for_ui_index(int index) {
    if (index < 0) return OobeStep::Welcome;
    if (index >= static_cast<int>(finn::shell::oobe::kStepCount)) return OobeStep::Finish;
    return static_cast<OobeStep>(index);
}

QString oobe_tr(const QString& key, const QString& locale) {
    struct Entry {
        const char* lang;
        const char* lang_prompt;
        const char* region_prompt;
        const char* kb_prompt;
        const char* wifi_prompt;
        const char* wifi_ethernet;
        const char* wifi_connected;
        const char* wifi_offline;
        const char* wifi_pwd_title;
        const char* wifi_connect_to;
        const char* cancel;
        const char* connect;
        const char* account_prompt;
        const char* full_name;
        const char* account_name;
        const char* computer_name;
        const char* password;
        const char* verify_password;
        const char* pw_mismatch;
        const char* appearance_prompt;
        const char* a11y_prompt;
        const char* a11y_sr;
        const char* a11y_hc;
        const char* a11y_rm;
        const char* a11y_sk;
        const char* privacy_prompt;
        const char* privacy_info;
        const char* privacy_loc;
        const char* privacy_diag;
        const char* privacy_crash;
        const char* privacy_ack;
        const char* complete_title;
        const char* update_title;
        const char* get_started;
    };
    static constexpr Entry kTable[] = {
        {"es",
         "¿Cómo hablamos?",
         "Un gran mundo. Pero un solo país. ¿Dónde?",
         "¿Cómo escribes?",
         "¿Dónde nos conectamos?",
         "Ethernet", "Conectado", "Continuar sin conexión", "Contraseña Wi-Fi", "Conectar a %1", "Cancelar", "Conectar",
         "¿Cómo te llamamos?",
         "Nombre completo", "Nombre de cuenta", "Nombre del equipo", "Contraseña", "Verificar contraseña", "Las contraseñas no coinciden",
         "¿Oscuro o claro? Tú eliges.",
         "¿Algo que lo haga más fácil?",
         "Lector de pantalla", "Alto contraste", "Reducir movimiento", "Teclas especiales",
         "¿Cuánto deberíamos recordar?",
         "FinnOS no recopila diagnósticos, ubicación ni informes de fallos. No hay ningún servicio de telemetría en esta instalación.",
         "Ubicación", "Diagnósticos", "Informes de fallos", "Continuar con todo desactivado",
         "Todo listo", "Actualización completada", "Empezar"},
        {"it",
         "Come parliamo?",
         "Un grande mondo. Ma un solo paese. Dove?",
         "Come scrivi?",
         "Dove ci connettiamo?",
         "Ethernet", "Connesso", "Continua offline", "Password Wi-Fi", "Connetti a %1", "Annulla", "Connetti",
         "Come ti chiamiamo?",
         "Nome completo", "Nome account", "Nome computer", "Password", "Verifica password", "Le password non corrispondono",
         "Scuro o chiaro? Scegli tu.",
         "Qualcosa per renderlo più facile?",
         "Screen reader", "Contrasto elevato", "Riduci movimento", "Tasti permanenti",
         "Quanto dovremmo ricordare?",
         "FinnOS non raccoglie diagnostica, posizione o segnalazioni di arresto anomalo. Nessun servizio di telemetria è presente in questa installazione.",
         "Posizione", "Diagnostica", "Segnalazioni di arresto", "Continua con tutto disattivato",
         "Tutto pronto", "Aggiornamento riuscito", "Inizia"},
        {"fr",
         "Comment parlons-nous ?",
         "Un grand monde. Mais un seul pays. Où ?",
         "Comment tapez-vous ?",
         "Où se connecter ?",
         "Ethernet", "Connecté", "Continuer hors ligne", "Mot de passe Wi-Fi", "Se connecter à %1", "Annuler", "Connecter",
         "Comment vous appelez-vous ?",
         "Nom complet", "Nom du compte", "Nom de l’ordinateur", "Mot de passe", "Confirmer le mot de passe", "Les mots de passe ne correspondent pas",
         "Sombre ou clair ? À vous de choisir.",
         "Quelque chose pour vous faciliter la vie ?",
         "Lecteur d’écran", "Contraste élevé", "Réduire les animations", "Touches rémanentes",
         "Que devons-nous retenir ?",
         "FinnOS ne collecte ni diagnostics, ni localisation, ni rapports de plantage. Aucun service de télémétrie n’est présent sur cette installation.",
         "Localisation", "Diagnostics", "Rapports de plantage", "Continuer avec tout désactivé",
         "Tout est prêt", "Mise à jour réussie", "Commencer"},
        {"ca",
         "Com parlem?",
         "Un món gran. Però un sol país. On?",
         "Com escrius?",
         "On ens connectem?",
         "Ethernet", "Connectat", "Continua fora de línia", "Contrasenya Wi-Fi", "Connecta a %1", "Cancel·la", "Connecta",
         "Com et diem?",
         "Nom complet", "Nom del compte", "Nom de l’ordinador", "Contrasenya", "Verifica la contrasenya", "Les contrasenyes no coincideixen",
         "Fosc o clar? Tu tries.",
         "Alguna cosa per fer-ho més fàcil?",
         "Lector de pantalla", "Alt contrast", "Redueix el moviment", "Tecles enganxoses",
         "Quant hauríem de recordar?",
         "FinnOS no recopila diagnòstics, ubicació ni informes d’errors. No hi ha cap servei de telemetria en aquesta instal·lació.",
         "Ubicació", "Diagnòstics", "Informes d’errors", "Continua amb tot desactivat",
         "Tot a punt", "Actualització completada", "Començar"},
        {"de",
         "Wie sprechen wir?",
         "Eine große Welt. Aber ein Land. Wo?",
         "Wie tippst du?",
         "Wo verbinden wir uns?",
         "Ethernet", "Verbunden", "Offline fortfahren", "WLAN-Passwort", "Mit %1 verbinden", "Abbrechen", "Verbinden",
         "Wie sollen wir dich nennen?",
         "Vollständiger Name", "Kontoname", "Computername", "Passwort", "Passwort bestätigen", "Passwörter stimmen nicht überein",
         "Dunkel oder hell? Deine Wahl.",
         "Was es dir leichter macht?",
         "Bildschirmleser", "Hoher Kontrast", "Bewegung reduzieren", "Einrastfunktion",
         "Wie viel sollen wir merken?",
         "FinnOS erfasst keine Diagnosedaten, Standorte oder Absturzberichte. Auf dieser Installation ist kein Telemetriedienst vorhanden.",
         "Standort", "Diagnose", "Absturzberichte", "Mit allem deaktiviert fortfahren",
         "Alles bereit", "Update erfolgreich", "Los geht’s"},
        {"af",
         "Hoe praat ons?",
         "’n Groot wêreld. Maar een land. Waar?",
         "Hoe tik jy?",
         "Waar koppel ons?",
         "Ethernet", "Gekoppel", "Gaan vanlyn voort", "Wi-Fi-wagwoord", "Koppel aan %1", "Kanselleer", "Koppel",
         "Wat moet ons jou noem?",
         "Volle naam", "Rekeningnaam", "Rekenaarnaam", "Wagwoord", "Bevestig wagwoord", "Wagwoorde stem nie ooreen nie",
         "Donker of lig? Jou keuse.",
         "Iets om dit makliker te maak?",
         "Skermleser", "Hoë kontras", "Verminder beweging", "Kleefsleutels",
         "Hoeveel moet ons onthou?",
         "FinnOS versamel geen diagnostiek, ligging of omvalverslae nie. Geen telemetriediens is op hierdie installasie teenwoordig nie.",
         "Ligging", "Diagnostiek", "Omvalverslae", "Gaan voort met alles af",
         "Alles gereed", "Opdatering suksesvol", "Begin"},
        {"zh_CN",
         "我们使用哪种语言？",
         "世界很大，但只需选一个国家。你在哪里？",
         "你使用哪种键盘布局？",
         "连接到哪里？",
         "以太网", "已连接", "离线继续", "Wi-Fi 密码", "连接到 %1", "取消", "连接",
         "我们怎么称呼你？",
         "全名", "账户名称", "电脑名称", "密码", "确认密码", "两次输入的密码不一致",
         "深色还是浅色？由你决定。",
         "需要帮你调得更顺手吗？",
         "屏幕阅读器", "高对比度", "减弱动态效果", "粘滞键",
         "我们需要记住多少信息？",
         "FinnOS 不会收集诊断、位置或崩溃报告。此安装不包含任何遥测服务。",
         "位置", "诊断", "崩溃报告", "保持全部关闭并继续",
         "一切就绪", "更新成功", "开始使用"},
        {"zh_TW",
         "我們使用哪種語言？",
         "世界很大，但只需選一個國家。你在哪裡？",
         "你使用哪種鍵盤配置？",
         "連線到哪裡？",
         "乙太網路", "已連線", "離線繼續", "Wi-Fi 密碼", "連線到 %1", "取消", "連線",
         "我們該怎麼稱呼你？",
         "全名", "帳戶名稱", "電腦名稱", "密碼", "確認密碼", "兩次輸入的密碼不一致",
         "深色還是淺色？由你決定。",
         "需要幫你調得更順手嗎？",
         "螢幕閱讀器", "高對比", "減少動態效果", "相黏鍵",
         "我們需要記住多少資訊？",
         "FinnOS 不會收集診斷、位置或當機報告。此安裝不包含任何遙測服務。",
         "位置", "診斷", "當機報告", "保持全部關閉並繼續",
         "一切就緒", "更新成功", "開始使用"},
        {"hi",
         "हम किस भाषा में बात करें?",
         "बड़ी दुनिया, लेकिन एक देश। कहाँ?",
         "आप कैसे टाइप करते हैं?",
         "हम कहाँ जुड़ें?",
         "ईथरनेट", "कनेक्टेड", "ऑफ़लाइन जारी रखें", "Wi-Fi पासवर्ड", "%1 से कनेक्ट करें", "रद्द करें", "कनेक्ट करें",
         "हम आपको क्या कहकर बुलाएँ?",
         "पूरा नाम", "खाता नाम", "कंप्यूटर नाम", "पासवर्ड", "पासवर्ड की पुष्टि करें", "पासवर्ड मेल नहीं खाते",
         "डार्क या लाइट? आपकी पसंद।",
         "इसे थोड़ा आसान बनाएँ?",
         "स्क्रीन रीडर", "उच्च कंट्रास्ट", "मोशन कम करें", "स्टिकी कीज़",
         "हमें कितना याद रखना चाहिए?",
         "FinnOS डायग्नोस्टिक्स, लोकेशन या क्रैश रिपोर्ट एकत्र नहीं करता। इस इंस्टॉलेशन में कोई टेलीमेट्री सेवा नहीं है।",
         "लोकेशन", "डायग्नोस्टिक्स", "क्रैश रिपोर्ट", "सब कुछ बंद रखकर जारी रखें",
         "सब तैयार है", "अपडेट सफल", "शुरू करें"},
        {"ko",
         "어떤 언어를 사용할까요?",
         "넓은 세상, 하나의 국가. 어디인가요?",
         "어떻게 입력하시나요?",
         "어디에 연결할까요?",
         "이더넷", "연결됨", "오프라인으로 계속", "Wi-Fi 암호", "%1에 연결", "취소", "연결",
         "어떻게 불러드릴까요?",
         "이름", "계정 이름", "컴퓨터 이름", "암호", "암호 확인", "암호가 일치하지 않습니다",
         "어둡게, 아니면 밝게? 선택해 보세요.",
         "조금 더 편하게 맞춰드릴까요?",
         "화면 낭독기", "고대비", "동작 줄이기", "고정 키",
         "얼마나 기억할까요?",
         "FinnOS는 진단, 위치 또는 충돌 보고서를 수집하지 않습니다. 이 설치에는 원격 측정 서비스가 없습니다.",
         "위치", "진단", "충돌 보고서", "모두 끈 상태로 계속",
         "모두 준비되었습니다", "업데이트 완료", "시작하기"},
        {"ja",
         "どの言語を使いますか？",
         "広い世界の中で、どの国ですか？",
         "どのキーボードを使いますか？",
         "どこに接続しますか？",
         "イーサネット", "接続済み", "オフラインで続行", "Wi-Fi パスワード", "%1 に接続", "キャンセル", "接続",
         "お名前を教えてください",
         "氏名", "アカウント名", "コンピュータ名", "パスワード", "パスワードの確認", "パスワードが一致しません",
         "ダークかライトか、お好みで。",
         "もっと使いやすくしますか？",
         "スクリーンリーダー", "ハイコントラスト", "視差効果を減らす", "固定キー機能",
         "どこまで記憶しますか？",
         "FinnOSは診断データ、位置情報、クラッシュレポートを収集しません。この環境にはテレメトリ機能は含まれていません。",
         "位置情報", "診断", "クラッシュレポート", "すべてオフのまま続行",
         "準備が整いました", "アップデート完了", "始める"},
        {"ar",
         "بأي لغة نتحدث؟",
         "عالم واسع، لكن دولة واحدة. أين؟",
         "كيف تكتب؟",
         "أين نتصل؟",
         "إيثرنت", "متصل", "المتابعة دون اتصال", "كلمة مرور Wi-Fi", "الاتصال بـ %1", "إلغاء", "اتصال",
         "ماذا نناديك؟",
         "الاسم الكامل", "اسم الحساب", "اسم الكمبيوتر", "كلمة المرور", "تأكيد كلمة المرور", "كلمتا المرور غير متطابقتين",
         "داكن أم فاتح؟ الخيار لك.",
         "هل نجعل الأمور أسهل لك؟",
         "قارئ الشاشة", "تباين عالٍ", "تقليل الحركة", "المفاتيح الثابتة",
         "كم يجب أن نتذكر؟",
         "لا يجمع FinnOS بيانات التشخيص أو الموقع أو تقارير الأعطال. لا توجد أي خدمة قياس عن بُعد في هذا التثبيت.",
         "الموقع", "التشخيص", "تقارير الأعطال", "المتابعة مع إيقاف كل شيء",
         "كل شيء جاهز", "تم التحديث بنجاح", "ابدأ الآن"},
        {"pt",
         "Como falamos?",
         "Um mundo grande. Mas um só país. Onde?",
         "Como escreves?",
         "Onde nos ligamos?",
         "Ethernet", "Ligado", "Continuar offline", "Palavra-passe Wi-Fi", "Ligar a %1", "Cancelar", "Ligar",
         "Como devemos chamar-te?",
         "Nome completo", "Nome da conta", "Nome do computador", "Palavra-passe", "Confirmar palavra-passe", "As palavras-passe não coincidem",
         "Escuro ou claro? Tu escolhes.",
         "Algo para tornar mais fácil?",
         "Leitor de ecrã", "Alto contraste", "Reduzir movimento", "Teclas presas",
         "Quanto devemos recordar?",
         "O FinnOS não recolhe diagnósticos, localização nem relatórios de falhas. Não existe qualquer serviço de telemetria nesta instalação.",
         "Localização", "Diagnósticos", "Relatórios de falhas", "Continuar com tudo desativado",
         "Tudo pronto", "Atualização concluída", "Começar"},
        {"nl",
         "Hoe spreken we?",
         "Een grote wereld. Maar één land. Waar?",
         "Hoe typ je?",
         "Waar verbinden we?",
         "Ethernet", "Verbonden", "Offline doorgaan", "Wi-Fi-wachtwoord", "Verbinden met %1", "Annuleren", "Verbinden",
         "Hoe mogen we je noemen?",
         "Volledige naam", "Accountnaam", "Computernaam", "Wachtwoord", "Wachtwoord bevestigen", "Wachtwoorden komen niet overeen",
         "Donker of licht? Aan jou de keus.",
         "Iets om het makkelijker te maken?",
         "Schermlezer", "Hoog contrast", "Beweging verminderen", "Plaktoetsen",
         "Hoeveel mogen we onthouden?",
         "FinnOS verzamelt geen diagnostiek, locatie of crashrapporten. Er is geen telemetrieservice aanwezig op deze installatie.",
         "Locatie", "Diagnostiek", "Crashrapporten", "Doorgaan met alles uit",
         "Alles klaar", "Update voltooid", "Aan de slag"},
        {"pl",
         "W jakim języku rozmawiamy?",
         "Wielki świat, ale jeden kraj. Gdzie?",
         "Jak piszesz?",
         "Gdzie się łączymy?",
         "Ethernet", "Połączono", "Kontynuuj offline", "Hasło Wi-Fi", "Połącz z %1", "Anuluj", "Połącz",
         "Jak mamy się do Ciebie zwracać?",
         "Imię i nazwisko", "Nazwa konta", "Nazwa komputera", "Hasło", "Potwierdź hasło", "Hasła nie są takie same",
         "Ciemny czy jasny? Twój wybór.",
         "Coś, co ułatwi korzystanie?",
         "Czytnik ekranu", "Wysoki kontrast", "Ogranicz ruch", "Klawisze trwałe",
         "Ile mamy zapamiętać?",
         "FinnOS nie zbiera danych diagnostycznych, lokalizacji ani raportów awarii. W tej instalacji nie ma żadnej usługi telemetrii.",
         "Lokalizacja", "Diagnostyka", "Raporty awarii", "Kontynuuj ze wszystkim wyłączonym",
         "Wszystko gotowe", "Aktualizacja zakończona", "Rozpocznij"},
        {"ru",
         "На каком языке мы говорим?",
         "Большой мир, но одна страна. Где вы?",
         "Какая у вас раскладка?",
         "Куда подключимся?",
         "Ethernet", "Подключено", "Продолжить без сети", "Пароль Wi-Fi", "Подключиться к %1", "Отмена", "Подключить",
         "Как к вам обращаться?",
         "Полное имя", "Имя учётной записи", "Имя компьютера", "Пароль", "Подтвердите пароль", "Пароли не совпадают",
         "Тёмная или светлая? Выбор за вами.",
         "Сделать управление удобнее?",
         "Экранный диктор", "Высокая контрастность", "Уменьшение движения", "Залипание клавиш",
         "Сколько нам запоминать?",
         "FinnOS не собирает диагностику, геолокацию или отчёты о сбоях. В этой системе нет служб телеметрии.",
         "Геолокация", "Диагностика", "Отчёты о сбоях", "Продолжить со всем отключённым",
         "Всё готово", "Обновление завершено", "Начать"},
        {"uk",
         "Якою мовою спілкуємось?",
         "Великий світ, але одна країна. Де ви?",
         "Як ви друкуєте?",
         "Де підключимося?",
         "Ethernet", "Підключено", "Продовжити офлайн", "Пароль Wi-Fi", "Підключитися до %1", "Скасувати", "Підключити",
         "Як до вас звертатися?",
         "Повне ім’я", "Ім’я облікового запису", "Назва комп’ютера", "Пароль", "Підтвердьте пароль", "Паролі не збігаються",
         "Темна чи світла? Обирайте.",
         "Зробити користування зручнішим?",
         "Екранний диктор", "Висока контрастність", "Зменшення руху", "Залипання клавіш",
         "Скільки нам запам’ятовувати?",
         "FinnOS не збирає діагностику, геолокацію чи звіти про збої. У цій системі немає жодної служби телеметрії.",
         "Геолокація", "Діагностика", "Звіти про збої", "Продовжити з усім вимкненим",
         "Усе готово", "Оновлення завершено", "Почати"},
        {"tr",
         "Hangi dilde konuşalım?",
         "Büyük bir dünya, ama tek bir ülke. Neresi?",
         "Nasıl yazıyorsunuz?",
         "Nereye bağlanalım?",
         "Ethernet", "Bağlandı", "Çevrimdışı devam et", "Wi-Fi Parolası", "%1 ağına bağlan", "İptal", "Bağlan",
         "Size nasıl hitap edelim?",
         "Ad Soyad", "Hesap Adı", "Bilgisayar Adı", "Parola", "Parolayı Doğrula", "Parolalar eşleşmiyor",
         "Koyu mu açık mı? Seçim sizin.",
         "Daha kolay hale getirelim mi?",
         "Ekran Okuyucu", "Yüksek Kontrast", "Hareketi Azalt", "Yapışkan Tuşlar",
         "Ne kadarını hatırlayalım?",
         "FinnOS tanılama, konum veya çökme raporu toplamaz. Bu kurulumda hiçbir telemetri hizmeti bulunmaz.",
         "Konum", "Tanılama", "Çökme raporları", "Her şey kapalı olarak devam et",
         "Her şey hazır", "Güncelleme başarılı", "Başla"},
    };

    const QString short_loc = locale.section(QLatin1Char('.'), 0, 0);
    const QString lang_only = short_loc.section(QLatin1Char('_'), 0, 0);
    const Entry* match = nullptr;
    for (const auto& e : kTable) {
        if (short_loc == QLatin1String(e.lang) || lang_only == QLatin1String(e.lang)) {
            match = &e;
            break;
        }
    }
    if (key == QLatin1String("lang_prompt"))
        return match ? QString::fromUtf8(match->lang_prompt) : QStringLiteral("How do we talk?");
    if (key == QLatin1String("region_prompt"))
        return match ? QString::fromUtf8(match->region_prompt) : QStringLiteral("A big world. But one country. Where?");
    if (key == QLatin1String("kb_prompt"))
        return match ? QString::fromUtf8(match->kb_prompt) : QStringLiteral("How do you type?");
    if (key == QLatin1String("wifi_prompt"))
        return match ? QString::fromUtf8(match->wifi_prompt) : QStringLiteral("Where do we connect?");
    if (key == QLatin1String("wifi_ethernet"))
        return match ? QString::fromUtf8(match->wifi_ethernet) : QStringLiteral("Ethernet");
    if (key == QLatin1String("wifi_connected"))
        return match ? QString::fromUtf8(match->wifi_connected) : QStringLiteral("Connected");
    if (key == QLatin1String("wifi_offline"))
        return match ? QString::fromUtf8(match->wifi_offline) : QStringLiteral("Continue offline");
    if (key == QLatin1String("wifi_pwd_title"))
        return match ? QString::fromUtf8(match->wifi_pwd_title) : QStringLiteral("Wi-Fi Password");
    if (key == QLatin1String("wifi_connect_to"))
        return match ? QString::fromUtf8(match->wifi_connect_to) : QStringLiteral("Connect to %1");
    if (key == QLatin1String("cancel"))
        return match ? QString::fromUtf8(match->cancel) : QStringLiteral("Cancel");
    if (key == QLatin1String("connect"))
        return match ? QString::fromUtf8(match->connect) : QStringLiteral("Connect");
    if (key == QLatin1String("account_prompt"))
        return match ? QString::fromUtf8(match->account_prompt) : QStringLiteral("What should we call you?");
    if (key == QLatin1String("full_name"))
        return match ? QString::fromUtf8(match->full_name) : QStringLiteral("Full Name");
    if (key == QLatin1String("account_name"))
        return match ? QString::fromUtf8(match->account_name) : QStringLiteral("Account Name");
    if (key == QLatin1String("computer_name"))
        return match ? QString::fromUtf8(match->computer_name) : QStringLiteral("Computer Name");
    if (key == QLatin1String("password"))
        return match ? QString::fromUtf8(match->password) : QStringLiteral("Password");
    if (key == QLatin1String("verify_password"))
        return match ? QString::fromUtf8(match->verify_password) : QStringLiteral("Verify Password");
    if (key == QLatin1String("pw_mismatch"))
        return match ? QString::fromUtf8(match->pw_mismatch) : QStringLiteral("Passwords don\u2019t match");
    if (key == QLatin1String("appearance_prompt"))
        return match ? QString::fromUtf8(match->appearance_prompt) : QStringLiteral("Dark or light? Your choice.");
    if (key == QLatin1String("a11y_prompt"))
        return match ? QString::fromUtf8(match->a11y_prompt) : QStringLiteral("Need a hand?");
    if (key == QLatin1String("a11y_sr"))
        return match ? QString::fromUtf8(match->a11y_sr) : QStringLiteral("Screen Reader");
    if (key == QLatin1String("a11y_hc"))
        return match ? QString::fromUtf8(match->a11y_hc) : QStringLiteral("High Contrast");
    if (key == QLatin1String("a11y_rm"))
        return match ? QString::fromUtf8(match->a11y_rm) : QStringLiteral("Reduce Motion");
    if (key == QLatin1String("a11y_sk"))
        return match ? QString::fromUtf8(match->a11y_sk) : QStringLiteral("Sticky Keys");
    if (key == QLatin1String("privacy_prompt"))
        return match ? QString::fromUtf8(match->privacy_prompt) : QStringLiteral("How much should we remember?");
    if (key == QLatin1String("privacy_info"))
        return match ? QString::fromUtf8(match->privacy_info)
                     : QStringLiteral("FinnOS does not collect diagnostics, location or crash reports. No telemetry service is present on this installation.");
    if (key == QLatin1String("privacy_loc"))
        return match ? QString::fromUtf8(match->privacy_loc) : QStringLiteral("Location");
    if (key == QLatin1String("privacy_diag"))
        return match ? QString::fromUtf8(match->privacy_diag) : QStringLiteral("Diagnostics");
    if (key == QLatin1String("privacy_crash"))
        return match ? QString::fromUtf8(match->privacy_crash) : QStringLiteral("Crash reports");
    if (key == QLatin1String("privacy_ack"))
        return match ? QString::fromUtf8(match->privacy_ack) : QStringLiteral("Continue with everything off");
    if (key == QLatin1String("complete_title"))
        return match ? QString::fromUtf8(match->complete_title) : QStringLiteral("You\u2019re all set");
    if (key == QLatin1String("update_title"))
        return match ? QString::fromUtf8(match->update_title) : QStringLiteral("Update successful");
    if (key == QLatin1String("get_started"))
        return match ? QString::fromUtf8(match->get_started) : QStringLiteral("Get started");
    return key;
}

// Top Branding lockup matching Figma 36:45 (20x20 four-shape mark + 15px gap + "FinnOS" 20px).
class OobeBrandingHeader final : public QWidget {
    Q_OBJECT

public:
    explicit OobeBrandingHeader(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(112, 24);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const QColor ink = t.ink;
        FinnThemeManager::instance().paintIcon(
            &p, QRectF(0.0, 2.0, 20.0, 20.0),
            t.dark ? QStringLiteral("finn-logo-light") : QStringLiteral("finn-logo-dark"),
            ink);

        p.setFont(oobe_font(20, QFont::Normal));
        p.setPen(ink);
        p.drawText(QRectF(35.0, 0.0, width() - 35.0, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FinnOS"));
    }

private:
    qreal opacity_ = 1.0;
};

// Circular 56x56 chevron navigation button matching Figma 36:48 / 36:66 / 36:54.
class OobeCircleNavButton final : public QAbstractButton {
    Q_OBJECT

public:
    OobeCircleNavButton(bool is_back, bool filled_by_default,
                        QWidget* parent = nullptr)
        : QAbstractButton(parent),
          is_back_(is_back),
          filled_default_(filled_by_default) {
        setFixedSize(56, 56);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(is_back ? QStringLiteral("Back") : QStringLiteral("Next"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_p = isDown() ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            press_t_ += (target_p - press_t_) * 0.36;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_p - press_t_) < 0.01) {
                hover_t_ = target_h;
                press_t_ = target_p;
                anim_timer_.stop();
            }
            update();
        });
    }

    void setRevealOpacity(qreal opacity) {
        opacity_ = std::clamp(opacity, 0.0, 1.0);
        setAttribute(Qt::WA_TransparentForMouseEvents, opacity_ < 0.25);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (opacity_ <= 0.005) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(opacity_);

        const qreal micro_scale = 1.0 + 0.045 * hover_t_ - 0.095 * press_t_;
        const QPointF center(width() * 0.5, height() * 0.5);
        p.translate(center);
        p.scale(micro_scale, micro_scale);
        p.translate(-center);

        const QRectF circle_r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool en = isEnabled();

        QColor fill = Qt::transparent;
        if (filled_default_) {
            fill = t.tile;
            if (press_t_ > 0.02) {
                const QColor down_c = t.surface_pressed;
                fill = QColor(
                    qRound(fill.red() + (down_c.red() - fill.red()) * press_t_),
                    qRound(fill.green() + (down_c.green() - fill.green()) * press_t_),
                    qRound(fill.blue() + (down_c.blue() - fill.blue()) * press_t_));
            } else if (hover_t_ > 0.02) {
                const QColor hov_c = t.surface_hover;
                fill = QColor(
                    qRound(fill.red() + (hov_c.red() - fill.red()) * hover_t_),
                    qRound(fill.green() + (hov_c.green() - fill.green()) * hover_t_),
                    qRound(fill.blue() + (hov_c.blue() - fill.blue()) * hover_t_));
            }
        } else if (en) {
            const qreal a = hover_t_ * (t.dark ? 22.0 : 195.0) + press_t_ * (t.dark ? 16.0 : 40.0);
            if (a > 0.5) {
                fill = QColor(t.fill.red(), t.fill.green(), t.fill.blue(),
                              std::min(255, qRound(a)));
            }
        }

        if (fill.alpha() > 0) {
            p.setPen(t.high_contrast ? QPen(t.ink, 2.0) : Qt::NoPen);
            p.setBrush(fill);
            p.drawEllipse(circle_r);
        } else if (t.high_contrast && en) {
            p.setPen(QPen(t.ink, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle_r);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle_r.adjusted(1.0, 1.0, -1.0, -1.0));
        }

        // Exact chevron vector from Figma 36:46 (36x36 box inside 56x56 at offset 10,10):
        // M13 29 C13 29 24 20.8987 24 18 C24 15.1011 13 7 13 7 (stroke-width 2.75)
        QColor stroke_col = t.ink;
        if (!en) stroke_col.setAlpha(72);

        p.save();
        p.translate(10.0, 10.0);
        if (is_back_) {
            p.translate(36.0, 0.0);
            p.scale(-1.0, 1.0);
        }
        QPainterPath chev;
        chev.moveTo(13.0, 29.0);
        chev.cubicTo(13.0, 29.0, 24.0, 20.8987, 24.0, 18.0);
        chev.cubicTo(24.0, 15.1011, 13.0, 7.0, 13.0, 7.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(stroke_col, 2.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chev);
        p.restore();
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void mousePressEvent(QMouseEvent* e) override {
        QAbstractButton::mousePressEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        QAbstractButton::mouseReleaseEvent(e);
        if (!anim_timer_.isActive()) anim_timer_.start();
    }

private:
    bool is_back_ = false;
    bool filled_default_ = false;
    bool hovered_ = false;
    qreal hover_t_ = 0.0;
    qreal press_t_ = 0.0;
    qreal opacity_ = 1.0;
    QTimer anim_timer_;
};

// Pill button matching the video's "Get started" action pill on the final screen.
class OobeGetStartedPill final : public QAbstractButton {
    Q_OBJECT

public:
    explicit OobeGetStartedPill(const QString& label, QWidget* parent = nullptr)
        : QAbstractButton(parent) {
        setText(label);
        setAccessibleName(label);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(142, 48);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = t.tile;
        if (isDown()) {
            bg = t.surface_pressed;
        } else if (hovered_) {
            bg = t.surface_hover;
        }
        p.setBrush(bg);
        p.setPen(QPen(hasFocus() ? t.accent : t.hair, hasFocus() ? 2.0 : 1.0));
        p.drawRoundedRect(r, 24.0, 24.0);

        p.setFont(oobe_font(15, QFont::Medium));
        p.setPen(t.ink);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

private:
    bool hovered_ = false;
};

// Selectable 350x37 row matching Figma 37:98 ("Language" item in "How do we talk?").
// Selected row draws a solid white #FFFFFF pill (cornerRadius 10); unselected is transparent.
class OobeOptionRow final : public QWidget {
    Q_OBJECT

public:
    OobeOptionRow(const QString& label, const QString& detail = {},
                  bool selected = false, QWidget* parent = nullptr)
        : QWidget(parent),
          label_(label),
          detail_(detail),
          selected_(selected) {
        setFixedSize(oobe_content_width(), 37);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(detail.isEmpty() ? label : QStringLiteral("%1, %2").arg(label, detail));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        sel_t_ = selected_ ? 1.0 : 0.0;
        anim_timer_.setInterval(16);
        connect(&anim_timer_, &QTimer::timeout, this, [this]() {
            const qreal target_h = hovered_ ? 1.0 : 0.0;
            const qreal target_s = selected_ ? 1.0 : 0.0;
            hover_t_ += (target_h - hover_t_) * 0.28;
            sel_t_ += (target_s - sel_t_) * 0.24;
            if (std::abs(target_h - hover_t_) < 0.01 &&
                std::abs(target_s - sel_t_) < 0.01) {
                hover_t_ = target_h;
                sel_t_ = target_s;
                anim_timer_.stop();
            }
            update();
        });
    }

    void setSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        if (FinnThemeManager::instance().reducedMotion()) {
            sel_t_ = selected_ ? 1.0 : 0.0;
            update();
        } else if (!anim_timer_.isActive()) {
            anim_timer_.start();
        }
    }

    bool isSelected() const { return selected_; }
    QString label() const { return label_; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(0.0, 0.0, width(), height());
        const qreal base_a = t.dark
            ? (hover_t_ * 14.0 + sel_t_ * 28.0 + (pressed_ ? 12.0 : 0.0))
            : (hover_t_ * 110.0 + sel_t_ * 255.0 + (pressed_ ? 50.0 : 0.0));
        if (base_a > 0.5) {
            const QColor pill_base = t.dark ? t.tile_on : t.tile;
            const QColor bg(pill_base.red(), pill_base.green(), pill_base.blue(),
                            std::min(255, qRound(t.dark ? base_a * 8.0 : base_a)));
            p.setPen(t.high_contrast && selected_ ? QPen(t.ink, 2.0) : Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(t.high_contrast && selected_ ? r.adjusted(1.0, 1.0, -1.0, -1.0) : r,
                              10.0, 10.0);
        }
        if (hasFocus()) {
            p.setPen(QPen(t.accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), 10.0, 10.0);
        }

        const QColor ink = t.ink;
        p.setFont(oobe_font(18, QFont::Normal));
        p.setPen(ink);
        const QRectF text_r(8.0, 0.0, width() - 16.0, height());
        p.drawText(text_r, Qt::AlignLeft | Qt::AlignVCenter, label_);

        if (!detail_.isEmpty()) {
            p.setFont(oobe_font(13, QFont::Normal));
            p.setPen(t.dim);
            p.drawText(QRectF(8.0, 0.0, width() - 16.0, height()),
                       Qt::AlignRight | Qt::AlignVCenter, detail_);
        }
    }

    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        pressed_ = false;
        if (!anim_timer_.isActive()) anim_timer_.start();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        } else {
            QWidget::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            update();
            if (rect().contains(event->pos())) emit clicked();
        } else {
            QWidget::mouseReleaseEvent(event);
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            emit clicked();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QString label_;
    QString detail_;
    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    qreal hover_t_ = 0.0;
    qreal sel_t_ = 0.0;
    QTimer anim_timer_;
};

// Minimalist 350px text input row matching Figma 37:98 pill geometry.
class OobeInputField final : public QWidget {
    Q_OBJECT

public:
    OobeInputField(const QString& placeholder, bool password = false,
                   QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 42);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 4, 12, 4);
        lay->setSpacing(8);

        edit_ = new QLineEdit(this);
        edit_->setFrame(false);
        edit_->setPlaceholderText(placeholder);
        edit_->setAccessibleName(placeholder);
        edit_->setFont(oobe_font(17, QFont::Normal));
        if (password) edit_->setEchoMode(QLineEdit::Password);
        edit_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        lay->addWidget(edit_, 1);

        connect(edit_, &QLineEdit::textChanged, this, [this]() { update(); });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeInputField::syncColors);
        syncColors();
    }

    QLineEdit* lineEdit() const { return edit_; }
    QString text() const { return edit_->text(); }
    void setText(const QString& t) { edit_->setText(t); }
    void setPlaceholderText(const QString& p) {
        edit_->setPlaceholderText(p);
        edit_->setAccessibleName(p);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(edit_->hasFocus()
                     ? QPen(t.accent, t.high_contrast ? 2.5 : 1.5)
                     : QPen(t.hair, t.high_contrast ? 2.0 : 1.0));
        p.setBrush(t.surface_input);
        p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 10.0, 10.0);
    }

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = edit_->palette();
        pal.setColor(QPalette::Text, t.ink);
        pal.setColor(QPalette::PlaceholderText, t.mute);
        edit_->setPalette(pal);
        update();
    }

    QLineEdit* edit_ = nullptr;
};

// Minimalist 350px toggle row matching Figma 37:98 typography and spacing.
class OobeToggleRow final : public QWidget {
    Q_OBJECT

public:
    OobeToggleRow(const QString& label, bool initial_checked,
                  QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(oobe_content_width(), 39);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);

        label_ = new QLabel(label, this);
        label_->setFont(oobe_font(18, QFont::Normal));
        lay->addWidget(label_, 1);

        toggle_ = new FinnToggle(initial_checked, this);
        toggle_->setAccessibleName(label);
        toggle_->setFocusPolicy(Qt::TabFocus);
        lay->addWidget(toggle_);

        connect(toggle_, &QAbstractButton::toggled, this,
                &OobeToggleRow::toggled);
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &OobeToggleRow::syncColors);
        syncColors();
    }

    FinnToggle* toggle() const { return toggle_; }
    void setLabel(const QString& text) {
        if (label_) label_->setText(text);
        if (toggle_) toggle_->setAccessibleName(text);
    }

signals:
    void toggled(bool checked);

private:
    void syncColors() {
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette pal = label_->palette();
        pal.setColor(QPalette::WindowText, t.ink);
        label_->setPalette(pal);
        update();
    }

    QLabel* label_ = nullptr;
    FinnToggle* toggle_ = nullptr;
};

// Circular gradient swatch for picking the Figma-based PFP color variant.
class OobePfpSwatch final : public QAbstractButton {
    Q_OBJECT

public:
    OobePfpSwatch(const AvatarPreset& preset, QWidget* parent = nullptr)
        : QAbstractButton(parent), preset_(preset) {
        setFixedSize(32, 32);
        setFocusPolicy(Qt::TabFocus);
        setAccessibleName(QStringLiteral("%1 avatar color").arg(preset.name));
        setCursor(Qt::PointingHandCursor);
        setToolTip(preset.name);
        setCheckable(true);
    }

    const QString& presetId() const { return preset_.id; }

protected:
    void enterEvent(QEnterEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outer = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        if (isChecked() || hasFocus()) {
            const QColor ring = hasFocus() ? t.accent : t.ink;
            p.setPen(QPen(ring, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        } else if (hovered_) {
            p.setPen(QPen(t.hair, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        }

        const qreal inset = isChecked() ? 5.0 : 4.0;
        const QRectF dot = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        QLinearGradient grad(dot.topLeft(), dot.bottomLeft());
        grad.setColorAt(0.0, preset_.top_color);
        grad.setColorAt(1.0, preset_.bottom_color);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(dot);
    }

private:
    AvatarPreset preset_;
    bool hovered_ = false;
};

// Progressive bottom blur overlay matching Figma 37:96 (LAYER_BLUR PROGRESSIVE,
// startOffset y=0.607 to 1.0, radius 20 -> CSS blur bands 2.15px..8.60px).
QImage box_blur_image(const QImage& src, int radius) {
    if (radius <= 0 || src.isNull()) return src;
    QImage a = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage b(a.size(), QImage::Format_ARGB32_Premultiplied);
    const int w = a.width();
    const int h = a.height();
    const int d = 2 * radius + 1;

    for (int pass = 0; pass < 2; ++pass) {
        // Horizontal pass
        for (int y = 0; y < h; ++y) {
            const QRgb* in_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            QRgb* out_row = reinterpret_cast<QRgb*>(b.scanLine(y));
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = in_row[std::clamp(k, 0, w - 1)];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int x = 0; x < w; ++x) {
                out_row[x] = qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = in_row[std::clamp(x - radius, 0, w - 1)];
                const QRgb add = in_row[std::clamp(x + radius + 1, 0, w - 1)];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
        // Vertical pass
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, sa = 0;
            for (int k = -radius; k <= radius; ++k) {
                const QRgb px = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(k, 0, h - 1)))[x];
                sr += qRed(px);
                sg += qGreen(px);
                sb += qBlue(px);
                sa += qAlpha(px);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(a.scanLine(y))[x] =
                    qRgba(sr / d, sg / d, sb / d, sa / d);
                const QRgb rem = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y - radius, 0, h - 1)))[x];
                const QRgb add = reinterpret_cast<const QRgb*>(
                    b.constScanLine(std::clamp(y + radius + 1, 0, h - 1)))[x];
                sr += qRed(add) - qRed(rem);
                sg += qGreen(add) - qGreen(rem);
                sb += qBlue(add) - qBlue(rem);
                sa += qAlpha(add) - qAlpha(rem);
            }
        }
    }
    return a;
}

class OobeProgressiveList final : public QWidget {
    Q_OBJECT

public:
    explicit OobeProgressiveList(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(oobe_content_width() + 6);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);

        scroll_ = new QScrollArea(this);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->viewport()->setAutoFillBackground(false);
        scroll_->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        scroll_->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

        host_ = new QWidget(scroll_);
        host_->setAutoFillBackground(false);
        host_->setAttribute(Qt::WA_TranslucentBackground, true);
        list_lay_ = new QVBoxLayout(host_);
        list_lay_->setContentsMargins(0, 0, 6, 80);
        list_lay_->setSpacing(10);
        scroll_->setWidget(host_);

        connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged,
                this, qOverload<>(&QWidget::update));
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    QVBoxLayout* listLayout() const { return list_lay_; }
    QWidget* hostWidget() const { return host_; }
    QScrollBar* verticalScrollBar() const { return scroll_ ? scroll_->verticalScrollBar() : nullptr; }
    int scrollOffset() const { return scroll_ && scroll_->verticalScrollBar() ? scroll_->verticalScrollBar()->value() : 0; }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        scroll_->setGeometry(rect());
    }

    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        return QWidget::eventFilter(watched, event);
    }

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* list_lay_ = nullptr;
};

// Overlay widget placed over the bottom of OobeProgressiveList to apply the
// progressive blur & fade from y=60.7% to 100% without blocking mouse events.
// Caches the 3-pass software blur and invalidates only on geometry, scroll, theme,
// or explicit list-content changes (M19).
class ProgressiveBlurOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit ProgressiveBlurOverlay(OobeProgressiveList* target_list, QWidget* parent = nullptr)
        : QWidget(parent), target_(target_list) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        if (target_ && target_->verticalScrollBar()) {
            connect(target_->verticalScrollBar(), &QScrollBar::valueChanged,
                    this, &ProgressiveBlurOverlay::invalidateCache);
        }
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, &ProgressiveBlurOverlay::invalidateCache);
    }

    void invalidateCache() {
        cache_dirty_ = true;
        update();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        cache_dirty_ = true;
    }

    void paintEvent(QPaintEvent*) override {
        if (!target_ || height() < 40 || in_capture_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        const QColor bg = t.bone;

        const int blur_top = qRound(height() * 0.607);
        const int blur_h = height() - blur_top;
        if (blur_h <= 8) return;

        const int scroll_y = target_->scrollOffset();
        if (cache_dirty_ || cached_comp_.isNull() || cached_size_ != size() ||
            cached_dark_ != t.dark || cached_scroll_ != scroll_y) {
            in_capture_ = true;
            QImage snap(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            snap.fill(bg);
            target_->render(&snap, QPoint(0, 0), QRegion(0, blur_top, width(), blur_h),
                            QWidget::DrawChildren);
            in_capture_ = false;

            const QImage b1 = box_blur_image(snap, 2);
            const QImage b2 = box_blur_image(b1, 4);
            const QImage b3 = box_blur_image(b2, 6);

            const int band = blur_h / 4;
            const int feather = std::max(12, blur_h / 5);

            QImage comp(width(), blur_h, QImage::Format_ARGB32_Premultiplied);
            comp.fill(Qt::transparent);
            {
                QPainter cp(&comp);
                cp.setRenderHint(QPainter::Antialiasing, true);
                cp.drawImage(0, 0, b1, 0, 0, width(), band * 2);
                cp.drawImage(0, band, b2, 0, band, width(), band * 2);
                cp.drawImage(0, band * 2, b3, 0, band * 2, width(), blur_h - band * 2);

                // Feather top entry of the blur region so there is no hard horizontal step.
                QLinearGradient mask(0, 0, 0, feather);
                mask.setColorAt(0.0, QColor(0, 0, 0, 0));
                mask.setColorAt(1.0, QColor(0, 0, 0, 255));
                cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cp.fillRect(QRect(0, 0, width(), feather), mask);
            }
            cached_comp_ = std::move(comp);
            cached_size_ = size();
            cached_dark_ = t.dark;
            cached_scroll_ = scroll_y;
            cache_dirty_ = false;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawImage(0, blur_top, cached_comp_);

        // Smooth progressive surface fade all the way to opaque bg at bottom edge.
        QLinearGradient fade(0, blur_top, 0, height());
        QColor c0 = bg;
        c0.setAlpha(0);
        QColor c1 = bg;
        c1.setAlpha(110);
        QColor c2 = bg;
        c2.setAlpha(255);
        fade.setColorAt(0.0, c0);
        fade.setColorAt(0.55, c1);
        fade.setColorAt(1.0, c2);
        p.fillRect(QRect(0, blur_top, width(), blur_h), fade);
    }

private:
    OobeProgressiveList* target_ = nullptr;
    bool in_capture_ = false;
    bool cache_dirty_ = true;
    QImage cached_comp_;
    QSize cached_size_;
    bool cached_dark_ = false;
    int cached_scroll_ = -1;
};

// Seamless transition overlay when advancing from Step 0 ("hi.") to Step 1
// ("How do we talk?"): continues from the end of "hi." into the same upright
// oval spring as the intro, draws forward across the whole screen while the
// camera pans, fades into Step 1, and reveals "How do we talk?" left-to-right
// with a glowing leading edge.
// Shared feathered left-to-right neon text reveal + soft bloom halo, used by both
// OobeHiToLangOverlay (Step 0 -> 1) and OobePageEntranceOverlay (Steps 1..6).
struct NeonTextReveal {
    QImage crisp_img;
    QImage glow_img;
    int glow_pad = 32;

    static QRectF labelTextRect(QLabel* lbl, QWidget* root) {
        if (!lbl || !root) return {};
        const QPoint tl = lbl->mapTo(root, QPoint(0, 0));
        const QFontMetricsF fm(lbl->font());
        const int flags = Qt::AlignCenter | (lbl->wordWrap() ? Qt::TextWordWrap : 0);
        const QRectF br = fm.boundingRect(
            QRectF(0.0, 0.0, std::max(1, lbl->width()), 400.0), flags, lbl->text());
        const double tx = tl.x() + (lbl->width() - br.width()) * 0.5;
        const double ty = tl.y() + (lbl->height() - br.height()) * 0.5;
        return QRectF(tx, ty, br.width(), br.height());
    }

    void ensurePrepared(QLabel* lbl, const QRectF& target_rect,
                        const QColor& text_col, bool dark) {
        if (!crisp_img.isNull() || !lbl || target_rect.width() <= 4.0) return;
        constexpr int kPad = 32;
        const int gw = static_cast<int>(std::ceil(target_rect.width())) + kPad * 2;
        const int gh = static_cast<int>(std::ceil(target_rect.height())) + kPad * 2;
        const QRectF text_box(kPad, kPad, target_rect.width(), target_rect.height());
        const int flags = Qt::AlignCenter | (lbl->wordWrap() ? Qt::TextWordWrap : 0);

        crisp_img = QImage(gw, gh, QImage::Format_ARGB32_Premultiplied);
        crisp_img.fill(Qt::transparent);
        {
            QPainter cp(&crisp_img);
            cp.setRenderHint(QPainter::Antialiasing, true);
            cp.setRenderHint(QPainter::TextAntialiasing, true);
            cp.setFont(lbl->font());
            cp.setPen(text_col);
            cp.drawText(text_box, flags, lbl->text());
        }

        QImage raw_glow(gw, gh, QImage::Format_ARGB32_Premultiplied);
        raw_glow.fill(Qt::transparent);
        {
            QPainter gp(&raw_glow);
            gp.setRenderHint(QPainter::Antialiasing, true);
            gp.setRenderHint(QPainter::TextAntialiasing, true);
            gp.setFont(lbl->font());
            gp.setPen(dark ? QColor(255, 255, 255, 255) : QColor(255, 255, 255, 240));
            gp.drawText(text_box, flags, lbl->text());
            if (!dark) {
                gp.setPen(QColor(110, 150, 255, 130));
                gp.drawText(text_box, flags, lbl->text());
            }
        }
        glow_img = box_blur_image(box_blur_image(raw_glow, 9), 5);
        glow_pad = kPad;
    }

    void paint(QPainter& p, const QRectF& target_rect, const QColor& text_col,
               const QColor& blue, double reveal_u, double neon_u,
               double bloom_alpha, double text_opacity) const {
        if (crisp_img.isNull() || (reveal_u <= 0.001 && bloom_alpha <= 0.01)) return;
        const QColor coral(0xFF, 0x57, 0x22);
        const QColor amber(0xFF, 0x95, 0x00);
        const QColor violet(0x8B, 0x5C, 0xF6);
        const int gw = crisp_img.width();
        const int gh = crisp_img.height();

        QImage tinted_text(gw, gh, QImage::Format_ARGB32_Premultiplied);
        tinted_text.fill(Qt::transparent);
        {
            QPainter tp(&tinted_text);
            tp.drawImage(0, 0, crisp_img);
            const double wave_head = glow_pad + (target_rect.width() + 150.0) * neon_u;
            QLinearGradient text_grad(wave_head - 140.0, 0.0, wave_head + 35.0, 0.0);
            text_grad.setColorAt(0.00, text_col);
            text_grad.setColorAt(0.28, violet);
            text_grad.setColorAt(0.54, coral);
            text_grad.setColorAt(0.76, amber);
            text_grad.setColorAt(0.92, blue);
            text_grad.setColorAt(1.00, text_col);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(0, 0, gw, gh, text_grad);
        }

        QImage comp(gw, gh, QImage::Format_ARGB32_Premultiplied);
        comp.fill(Qt::transparent);
        {
            QPainter cp(&comp);
            if (bloom_alpha > 0.01) {
                cp.setOpacity(bloom_alpha * 0.70);
                cp.drawImage(0, 0, glow_img);
            }
            if (text_opacity > 0.005) {
                cp.setOpacity(text_opacity);
                cp.drawImage(0, 0, tinted_text);
            }
            if (reveal_u < 1.0) {
                const double sweep_x = -12.0 + (gw + 48.0) * reveal_u;
                QLinearGradient feather(sweep_x - 52.0, 0.0, sweep_x + 12.0, 0.0);
                feather.setColorAt(0.0, QColor(255, 255, 255, 255));
                feather.setColorAt(0.65, QColor(255, 255, 255, 160));
                feather.setColorAt(1.0, QColor(255, 255, 255, 0));
                cp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cp.setOpacity(1.0);
                cp.fillRect(0, 0, gw, gh, feather);
            }
        }
        p.drawImage(QPointF(target_rect.left() - glow_pad,
                            target_rect.top() - glow_pad),
                    comp);
    }
};

// Buttery sequential fade + subtle optical blur transition for OOBE steps.
// Outgoing page accelerates softly away with a whisper of blur and fades out
// completely before the incoming page decelerates into place on a long quintic
// ease-out tail (zero overlap, zero sudden stop).
class OobeSlideTransitionOverlay final : public QWidget {
public:
    OobeSlideTransitionOverlay(QWidget* parent, QWidget* stack_widget,
                               QPixmap old_snap, QPixmap new_snap, bool forward)
        : QWidget(parent),
          stack_widget_(stack_widget),
          old_snap_(std::move(old_snap)),
          new_snap_(std::move(new_snap)),
          forward_(forward) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (stack_widget_) {
            setGeometry(stack_widget_->geometry());
            stack_widget_->setVisible(false);
        } else if (parent) {
            setGeometry(parent->rect());
        }
        old_blur_ = makeWhisperBlur(old_snap_);
        new_blur_ = makeWhisperBlur(new_snap_);
        elapsed_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(8);
        connect(timer_, &QTimer::timeout, this, [this]() {
            if (stack_widget_ && geometry() != stack_widget_->geometry()) {
                setGeometry(stack_widget_->geometry());
            }
            if (elapsed_.nsecsElapsed() / 1e9 >= kDurationSec) {
                finish();
                return;
            }
            update();
        });
        timer_->start();
        show();
        raise();
    }

    ~OobeSlideTransitionOverlay() override {
        if (!cancelled_ && stack_widget_) {
            stack_widget_->setVisible(true);
        }
    }

    QPixmap grabCurrentFrame() {
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(width() * dpr), qRound(height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        render(&pix);
        return pix;
    }

    void cancel() {
        cancelled_ = true;
        if (timer_) timer_->stop();
        hide();
        deleteLater();
    }

    void finish() {
        if (timer_) timer_->stop();
        if (stack_widget_) {
            stack_widget_->setVisible(true);
            stack_widget_->update();
        }
        hide();
        deleteLater();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double t = std::clamp((elapsed_.nsecsElapsed() / 1e9) / kDurationSec, 0.0, 1.0);
        constexpr qreal kDrift = 36.0;
        const qreal dir = forward_ ? 1.0 : -1.0;

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        if (t < 0.40) {
            // Phase 1: smooth cubic acceleration out + whisper of blur
            const double u = std::clamp(t / 0.40, 0.0, 1.0);
            const double ease_in = u * u * (2.0 - u) * u;  // starts at 0 vel, accelerates smoothly
            const double alpha = 1.0 - (u * u * (3.0 - 2.0 * u));
            const double blur_mix = u * u;
            const QPointF off(-dir * kDrift * ease_in, 0.0);

            const double sharp_a = alpha * (1.0 - blur_mix);
            const double blur_a = alpha * blur_mix;
            if (sharp_a > 0.002) {
                p.setOpacity(sharp_a);
                p.drawPixmap(off, old_snap_);
            }
            if (blur_a > 0.002 && !old_blur_.isNull()) {
                p.setOpacity(blur_a);
                p.drawPixmap(QRectF(off, QSizeF(size())), old_blur_, QRectF(old_blur_.rect()));
            }
        } else if (t >= 0.44) {
            // Phase 2: long quartic/quintic ease-out deceleration into place (zero sudden stop)
            const double u = std::clamp((t - 0.44) / 0.56, 0.0, 1.0);
            const double inv = 1.0 - u;
            const double ease_out = 1.0 - inv * inv * inv * inv;  // silky long settle tail
            const double alpha = 1.0 - inv * inv * inv;
            const double blur_mix = inv * inv;                    // subtle blur clears early
            const QPointF off(dir * kDrift * (1.0 - ease_out), 0.0);

            const double blur_a = alpha * blur_mix;
            const double sharp_a = alpha * (1.0 - blur_mix);
            if (blur_a > 0.002 && !new_blur_.isNull()) {
                p.setOpacity(blur_a);
                p.drawPixmap(QRectF(off, QSizeF(size())), new_blur_, QRectF(new_blur_.rect()));
            }
            if (sharp_a > 0.002) {
                p.setOpacity(sharp_a);
                p.drawPixmap(off, new_snap_);
            }
        }
        p.setOpacity(1.0);
    }

private:
    static QPixmap makeWhisperBlur(const QPixmap& src) {
        if (src.isNull()) return {};
        QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QSize half(std::max(1, img.width() / 2), std::max(1, img.height() / 2));
        QImage small = img.scaled(half, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Gentle optical blur (radius 2) — just enough softness without smudging
        QImage blurred = box_blur_image(small, 2);
        return QPixmap::fromImage(blurred);
    }

    static constexpr double kDurationSec = 0.52;
    QPointer<QWidget> stack_widget_;
    QPixmap old_snap_;
    QPixmap new_snap_;
    QPixmap old_blur_;
    QPixmap new_blur_;
    bool forward_ = true;
    bool cancelled_ = false;
    QElapsedTimer elapsed_;
    QTimer* timer_ = nullptr;
};

class OobeHiToLangOverlay : public QWidget {
public:
    OobeHiToLangOverlay(QWidget* parent, QLabel* target_prompt)
        : QWidget(parent), target_prompt_(target_prompt) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        if (parent) {
            setGeometry(parent->rect());
        }
        elapsed_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(16);
        connect(timer_, &QTimer::timeout, this, [this]() {
            if (parentWidget() && size() != parentWidget()->size()) {
                setGeometry(parentWidget()->rect());
            }
            const double t = elapsed_.elapsed() / 1000.0;
            if (t >= 3.55) {
                timer_->stop();
                hide();
                deleteLater();
                return;
            }
            update();
        });
        timer_->start();
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double t = elapsed_.elapsed() / 1000.0;
        const int w = width();
        const int h = height();
        if (w <= 0 || h <= 0) return;

        constexpr double kPi = 3.14159265358979323846;
        const auto& tok = FinnThemeManager::instance().tokens();
        const QColor bg = tok.bone;
        const QColor ink = tok.ink;
        const QColor text_col = tok.ink;
        const QColor coral(0xFF, 0x57, 0x22);
        const QColor amber(0xFF, 0x95, 0x00);
        const QColor violet(0x8B, 0x5C, 0xF6);
        const QColor blue = tok.accent;

        auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
        auto smooth01 = [&](double a, double b) {
            const double u = clamp01((t - a) / std::max(0.001, b - a));
            return u * u * (3.0 - 2.0 * u);
        };
        auto ease_out_cubic = [](double u) {
            const double inv = 1.0 - std::clamp(u, 0.0, 1.0);
            return 1.0 - inv * inv * inv;
        };

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 1. Keep Step 1 hidden behind opaque bg while:
        //    0.00..2.48s : "hi." -> spring (same pace as paintHiIntro) -> brief pause as POV
        //                  puts spring on the left side -> uncoils at target_rect.left()
        //    2.40..2.98s : ONLY "How do we talk?" text animates in with feathered bloom
        //    3.00..3.45s : veil fades out so the rest of Step 1 appears underneath
        const double veil_fade = smooth01(3.00, 3.45);
        const int veil_alpha = static_cast<int>(std::lround(255.0 * (1.0 - veil_fade)));
        if (veil_alpha > 0) {
            QColor veil = bg;
            veil.setAlpha(veil_alpha);
            p.fillRect(rect(), veil);
        }

        // Determine target text rect ("How do we talk?") in overlay coordinates.
        QRectF target_rect(w * 0.25 - 95.0, h * 0.50 - 16.0, 190.0, 32.0);
        if (target_prompt_ && parentWidget()) {
            target_rect = NeonTextReveal::labelTextRect(target_prompt_, parentWidget());
        }

        // 2. Smooth feathered left-to-right glowing text reveal ("How do we talk?")
        //    seamlessly ignited as the spring tip reaches target_rect.left() (2.26..2.94s),
        //    then veil fades out (2.96..3.42s) to reveal the rest of Step 1.
        if (t >= 2.20 && t < 3.52 && target_prompt_) {
            text_reveal_.ensurePrepared(target_prompt_, target_rect, text_col, tok.dark);

            const double reveal_raw = clamp01((t - 2.26) / (2.94 - 2.26));
            const double reveal_u = ease_out_cubic(reveal_raw);
            const double neon_u = clamp01((t - 2.26) / (3.26 - 2.26));
            const double bloom_in = smooth01(2.26, 2.52);
            const double bloom_out = 1.0 - smooth01(2.96, 3.45);
            const double bloom_alpha = bloom_in * bloom_out;

            text_reveal_.paint(p, target_rect, text_col, blue, reveal_u, neon_u,
                               bloom_alpha, veil_alpha / 255.0);
        }

        // 3. Exact "hi." stroke continuing forward into the vertically-centered
        //    full-screen upright oval spring at the exact same pace as paintHiIntro,
        //    staying visible at target_rect.left() until "How do we talk?" is
        //    fully shown (2.94s), then fading out cleanly (2.88..3.08s).
        if (t <= 3.12) {
            struct Pt { double x; double y; };
            static constexpr Pt kHiMain[] = {
                {-8.0, 20.5},  {-3.5, 20.8},  {0.8, 20.6},   {3.5, 19.2},   {6.8, 15.8},
                {9.8, 11.4},   {12.2, 6.8},   {13.6, 3.4},   {13.9, 1.4},   {12.8, 0.7},
                {11.1, 1.3},   {9.3, 3.6},    {7.4, 7.5},    {5.6, 12.6},   {4.1, 18.2},
                {2.9, 23.6},   {2.2, 27.4},   {2.4, 29.0},   {3.4, 28.2},   {5.2, 25.4},
                {7.8, 21.6},   {10.6, 17.8},  {13.4, 15.0},  {15.8, 13.8},  {17.6, 14.3},
                {18.4, 16.2},  {18.1, 19.4},  {17.1, 23.2},  {16.3, 26.6},  {16.5, 28.6},
                {17.8, 29.3},  {19.8, 28.4},  {22.2, 25.8},  {24.6, 22.0},  {26.8, 17.6},
                {28.3, 14.4},  {28.8, 13.5},  {28.4, 15.2},  {27.3, 18.8},  {26.1, 23.0},
                {25.4, 26.5},  {25.9, 28.4},  {27.4, 29.0},  {29.8, 28.0},  {32.4, 25.6},
                {34.8, 22.6},  {36.6, 20.4},
            };
            static constexpr Pt kHiIDot[] = {
                {30.3, 6.9}, {31.6, 7.5}, {32.1, 8.6}, {31.5, 9.7}, {30.3, 10.2},
                {29.0, 9.7}, {28.4, 8.6}, {29.0, 7.5}, {30.3, 6.9},
            };
            static constexpr Pt kHiPeriod[] = {
                {37.9, 25.9}, {39.2, 26.5}, {39.7, 27.8}, {39.1, 29.1}, {37.9, 29.7},
                {36.7, 29.1}, {36.1, 27.8}, {36.7, 26.5}, {37.9, 25.9},
            };
            constexpr int kMainCount = static_cast<int>(sizeof(kHiMain) / sizeof(kHiMain[0]));

            const double cx = w * 0.5;
            const double cy = h * 0.499;
            const double hi_ox = cx - 41.0 * 0.5;
            const double hi_oy = cy - 31.0 * 0.5;
            const double r_max = std::min(w, h) * 0.165;

            const double coil_start_x = hi_ox + kHiMain[kMainCount - 1].x;
            const double coil_start_y = hi_oy + kHiMain[kMainCount - 1].y;
            const double coil_world_span = w * 1.85;
            const double coil_world_end = coil_start_x + coil_world_span;
            const double cam_x_final = coil_world_end - (target_rect.left() - 2.0);

            // Continuous spring motion at the exact same pace as paintHiIntro (no mid pause).
            const double p_stroke = clamp01(t / 2.38);
            const double head_u =
                p_stroke * (0.82 + 0.18 * (3.0 * p_stroke - 2.0 * p_stroke * p_stroke));
            const double cam_x = cam_x_final * smooth01(0.15, 2.38);
            const double ox = hi_ox - cam_x;

            const double dots_alpha = 1.0 - smooth01(0.28, 0.72);
            if (dots_alpha > 0.01) {
                p.save();
                QColor dc = ink;
                dc.setAlpha(static_cast<int>(255 * dots_alpha));
                p.setPen(Qt::NoPen);
                p.setBrush(dc);
                for (const auto* arr : {kHiIDot, kHiPeriod}) {
                    QPainterPath dp;
                    dp.moveTo(ox + arr[0].x, hi_oy + arr[0].y);
                    for (int i = 1; i < 9; ++i) {
                        dp.lineTo(ox + arr[i].x, hi_oy + arr[i].y);
                    }
                    dp.closeSubpath();
                    p.drawPath(dp);
                }
                p.restore();
            }

            constexpr double kHiSplit = 0.14;
            const double head_s = kHiSplit + (1.0 - kHiSplit) * head_u;
            const double tail_s = std::min(head_s - 0.012, smooth01(0.85, 3.06));

            if (head_s > tail_s + 0.002) {
                constexpr double kLoops = 12.5;
                const double pitch = coil_world_span / kLoops;
                const double loop_rx = pitch * 0.40;
                const double loop_ry = r_max * 0.90;

                auto sample_coil = [&](double u) -> QPointF {
                    const double eu = clamp01(u / 0.10);
                    const double entry_amp = eu * eu * (3.0 - 2.0 * eu);
                    const double xu = clamp01((u - 0.86) / 0.14);
                    const double exit_amp = 1.0 - xu * xu * (3.0 - 2.0 * xu);
                    const double amp = entry_amp * exit_amp;
                    const double rx = loop_rx * amp;
                    const double ry = loop_ry * amp;
                    const double wave = std::sin(2.0 * kPi * 2.0 * u + t * 2.1) *
                                        (r_max * 0.16) * clamp01(u / 0.08) * (1.0 - u);
                    const double base_x = coil_start_x + coil_world_span * u - cam_x;
                    const double mid_y =
                        coil_start_y + ((cy + r_max - loop_ry) - coil_start_y)
                                           * clamp01(u / 0.14);
                    const double end_t = clamp01((u - 0.76) / 0.24);
                    const double end_ease = end_t * end_t * (3.0 - 2.0 * end_t);
                    const double center_y =
                        mid_y + (target_rect.center().y() - mid_y) * end_ease + wave;
                    const double angle = 2.0 * kPi * kLoops * u;
                    return QPointF(base_x + rx * std::sin(angle),
                                   center_y + ry * std::cos(angle));
                };

                QPainterPath ribbon_path;
                bool started = false;
                QPointF tail_pt(ox + kHiMain[0].x, hi_oy + kHiMain[0].y);
                QPointF nib_pt = tail_pt;

                if (tail_s < kHiSplit) {
                    const double hi_start_u = clamp01(tail_s / kHiSplit);
                    const int min_idx = std::clamp(
                        static_cast<int>(std::floor(hi_start_u * (kMainCount - 1))),
                        0, kMainCount - 2);
                    const double frac = hi_start_u * (kMainCount - 1) - min_idx;
                    tail_pt = QPointF(
                        ox + kHiMain[min_idx].x + (kHiMain[min_idx + 1].x - kHiMain[min_idx].x) * frac,
                        hi_oy + kHiMain[min_idx].y + (kHiMain[min_idx + 1].y - kHiMain[min_idx].y) * frac);
                    ribbon_path.moveTo(tail_pt);
                    started = true;
                    for (int idx = min_idx + 1; idx < kMainCount - 1; ++idx) {
                        const QPointF p0(ox + kHiMain[idx].x, hi_oy + kHiMain[idx].y);
                        const QPointF p1(ox + kHiMain[idx + 1].x, hi_oy + kHiMain[idx + 1].y);
                        ribbon_path.quadTo(p0, (p0 + p1) * 0.5);
                    }
                    ribbon_path.lineTo(QPointF(ox + kHiMain[kMainCount - 1].x,
                                               hi_oy + kHiMain[kMainCount - 1].y));
                }

                if (head_s > kHiSplit) {
                    const double coil_start_u =
                        tail_s <= kHiSplit ? 0.0 : clamp01((tail_s - kHiSplit) / (1.0 - kHiSplit));
                    const double coil_end_u = clamp01((head_s - kHiSplit) / (1.0 - kHiSplit));
                    if (tail_s >= kHiSplit) {
                        tail_pt = sample_coil(coil_start_u);
                    }
                    constexpr int kCoilSamples = 420;
                    for (int i = 0; i <= kCoilSamples; ++i) {
                        const double u =
                            coil_start_u + (coil_end_u - coil_start_u) * (static_cast<double>(i) / kCoilSamples);
                        const QPointF pt = sample_coil(u);
                        if (!started) {
                            ribbon_path.moveTo(pt);
                            started = true;
                        } else {
                            ribbon_path.lineTo(pt);
                        }
                        nib_pt = pt;
                    }
                }

                const double grad_x0 = std::min(tail_pt.x(), nib_pt.x() - 220.0);
                const double grad_x1 = std::max(nib_pt.x() + 18.0, grad_x0 + 40.0);
                QLinearGradient ribbon_grad(QPointF(grad_x0, cy), QPointF(grad_x1, cy));
                ribbon_grad.setColorAt(0.00, ink);
                ribbon_grad.setColorAt(0.45, ink);
                ribbon_grad.setColorAt(0.68, violet);
                ribbon_grad.setColorAt(0.82, coral);
                ribbon_grad.setColorAt(0.92, amber);
                ribbon_grad.setColorAt(1.00, blue);

                const double ribbon_alpha = 1.0 - smooth01(2.88, 3.08);
                p.save();
                p.setOpacity(ribbon_alpha);
                p.setPen(QPen(QBrush(ribbon_grad), 3.2,
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(ribbon_path);
                p.restore();
            }
        }
    }

private:
    QLabel* target_prompt_ = nullptr;
    NeonTextReveal text_reveal_;
    QElapsedTimer elapsed_;
    QTimer* timer_ = nullptr;
};

class OobeWindow final : public FinnLayerWindow {
    Q_OBJECT

public:
    explicit OobeWindow(bool update_mode = false, double fixed_phase = -1.0,
                        finn::shell::oobe::CapabilitySnapshot caps = {})
        : FinnLayerWindow(Layer::Overlay, QStringLiteral("finn-oobe")),
          flow_(std::move(caps)),
          update_mode_(update_mode),
          fixed_phase_(fixed_phase) {
        setAnchors(AnchorTop | AnchorBottom | AnchorLeft | AnchorRight);
        setExclusiveZone(-1);
        setKeyboardMode(KeyboardMode::Exclusive);
        setAccessibleName(QStringLiteral("FinnOS First-Time Setup"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        stack_ = new FinnPageStack(this);
        // OobeSlideTransitionOverlay owns the step fade; disable FinnPageStack's
        // internal crossfade so the previous page never flashes at transition end.
        stack_->setAnimatedTransitions(false);
        stack_->addWidget(buildHiPage());             // 0 -> Welcome
        stack_->addWidget(buildLanguagePage());       // 1 -> Language
        stack_->addWidget(buildRegionTimezonePage()); // 2 -> RegionTimezone
        stack_->addWidget(buildKeyboardPage());       // 3 -> Keyboard
        stack_->addWidget(buildWifiPage());           // 4 -> Connectivity
        stack_->addWidget(buildAccountPage());        // 5 -> Account (+ Hostname)
        stack_->addWidget(buildAppearancePage());     // 6 -> Theme
        stack_->addWidget(buildAccessibilityPage());  // 7 -> Accessibility
        stack_->addWidget(buildPrivacyPage());        // 8 -> Privacy
        stack_->addWidget(buildCompletePage());       // 9 -> Finish
        root->addWidget(stack_, 1);


        // Shared Back/Next circular navigation bar pinned across steps 1..5 so
        // arrows stay completely stationary during page slide transitions.
        shared_nav_box_ = new QWidget(this);
        shared_nav_box_->setFixedSize(132, 56);
        auto* nav_row = new QHBoxLayout(shared_nav_box_);
        nav_row->setContentsMargins(0, 0, 0, 0);
        nav_row->setSpacing(20);
        auto* back_btn = new OobeCircleNavButton(true, false, shared_nav_box_);
        shared_next_btn_ = new OobeCircleNavButton(false, false, shared_nav_box_);
        connect(back_btn, &QAbstractButton::clicked, this, &OobeWindow::previousStep);
        connect(shared_next_btn_, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        nav_row->addWidget(back_btn);
        nav_row->addWidget(shared_next_btn_);
        shared_nav_box_->setVisible(false);
        shared_nav_box_->raise();

        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
        if (qApp) qApp->installEventFilter(this);
        connect(&finn::sys::FinnDesktopServices::instance(),
                &finn::sys::FinnDesktopServices::stateUpdated,
                this, &OobeWindow::syncNetwork);
        finn::sys::FinnDesktopServices::instance().refreshAll();

        setStep(update_mode_ ? 9 : 0);
        if (selected_locale_ != QLatin1String("en_GB.UTF-8")) {
            applyLanguageSelection(selected_language_, selected_locale_);
        }
        positionSharedNav();
    }

    void openStep(int idx) {
        if (idx >= 0 && idx < stack_->count()) setStep(idx);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        FinnLayerWindow::resizeEvent(event);
        positionSharedNav();
    }

    void paintEvent(QPaintEvent*) override {
        const auto& t = FinnThemeManager::instance().tokens();
        QPainter p(this);
        p.fillRect(rect(), t.bone);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QWidget* fw = focusWidget();
            if (qobject_cast<QAbstractButton*>(fw) ||
                qobject_cast<OobeOptionRow*>(fw)) {
                FinnLayerWindow::keyPressEvent(event);
                return;
            }
            advanceStep();
            return;
        }
        FinnLayerWindow::keyPressEvent(event);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (sticky_keys_active_ && !sticky_replaying_ && ev &&
            (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease)) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            const int k = ke->key();
            Qt::KeyboardModifier mod = Qt::NoModifier;
            if (k == Qt::Key_Shift) mod = Qt::ShiftModifier;
            else if (k == Qt::Key_Control) mod = Qt::ControlModifier;
            else if (k == Qt::Key_Alt) mod = Qt::AltModifier;
            else if (k == Qt::Key_Meta) mod = Qt::MetaModifier;

            if (mod != Qt::NoModifier) {
                if (ev->type() == QEvent::KeyPress && !ke->isAutoRepeat()) {
                    sticky_latched_mods_ ^= mod;
                }
            } else if (ev->type() == QEvent::KeyPress &&
                       sticky_latched_mods_ != Qt::NoModifier &&
                       (ke->modifiers() & sticky_latched_mods_) != sticky_latched_mods_) {
                const Qt::KeyboardModifiers combined = ke->modifiers() | sticky_latched_mods_;
                QString txt = ke->text();
                if ((sticky_latched_mods_ & Qt::ShiftModifier) && !txt.isEmpty()) {
                    txt = txt.toUpper();
                }
                sticky_latched_mods_ = Qt::NoModifier;
                QKeyEvent synth(QEvent::KeyPress, ke->key(), combined, txt,
                                ke->isAutoRepeat(), ke->count());
                sticky_replaying_ = true;
                QCoreApplication::sendEvent(obj, &synth);
                sticky_replaying_ = false;
                return true;
            }
        }
        return FinnLayerWindow::eventFilter(obj, ev);
    }

private:
    // Helper that builds the Figma 36:53 split two-column layout:
    //   - Left prompt ("How do we talk?") at x=225..320, y=408 (24px Fixel Display Regular)
    //   - Right 350px column at x=805, y=195
    //   - Centered Back/Next 56x56 circular chevron buttons at x=574, y=631 (itemSpacing 20)
    QWidget* createSplitStepPage(const QString& icon_name,
                                 const QString& prompt_text,
                                 const QString& sub_caption,
                                 QWidget* right_column,
                                 QLabel** out_sub_label = nullptr,
                                 bool flush_right_top = false,
                                 QLabel** out_prompt_label = nullptr) {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        const OobeForm form = oobe_form();
        const int gutter = oobe_gutter();
        auto* main_col = new QVBoxLayout(page);
        // The shared Back/Next bar floats 145 px above the bottom on desktop;
        // on phone it sits in a reserved 132 dp strip so it never covers the
        // last list row.
        main_col->setContentsMargins(gutter, form == OobeForm::Phone ? gutter : 0,
                                     gutter, form == OobeForm::Phone ? 132 : 0);
        main_col->setSpacing(0);

        QBoxLayout* content_row = form == OobeForm::Phone
            ? static_cast<QBoxLayout*>(new QVBoxLayout())
            : static_cast<QBoxLayout*>(new QHBoxLayout());
        content_row->setContentsMargins(0, 0, 0, 0);
        content_row->setSpacing(form == OobeForm::Phone ? 12
                                : form == OobeForm::Tablet ? 16 : 32);

        // Left column: 46x46 icon + 40px gap + conversational question centered
        // vertically (Figma 36:53). On phone it is a stacked header instead of a
        // side column; on tablet it keeps the split but drops the desktop inset.
        auto* left_host = new QWidget(page);
        auto* left_col = new QVBoxLayout(left_host);
        left_col->setContentsMargins(form == OobeForm::Desktop ? 80 : 0, 0,
                                     form == OobeForm::Desktop ? 20 : 0, 0);
        left_col->setSpacing(0);
        if (form != OobeForm::Phone) left_col->addStretch(49);

        auto* icon_lbl = new QLabel(left_host);
        icon_lbl->setFixedSize(46, 46);
        icon_lbl->setAlignment(Qt::AlignCenter);
        left_col->addWidget(icon_lbl, 0, Qt::AlignHCenter);
        left_col->addSpacing(form == OobeForm::Phone ? 12
                             : form == OobeForm::Tablet ? 24 : 40);

        auto* prompt_lbl = new QLabel(left_host);
        prompt_lbl->setFont(oobe_font(form == OobeForm::Phone ? 20 : 24,
                                      QFont::Normal));
        prompt_lbl->setAlignment(Qt::AlignCenter);
        prompt_lbl->setWordWrap(true);
        prompt_lbl->setFixedWidth(form == OobeForm::Desktop ? 380 : qMin(300, oobe_content_width()));
        prompt_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        set_wrapped_label_text(prompt_lbl, prompt_text);
        if (out_prompt_label) *out_prompt_label = prompt_lbl;
        left_col->addWidget(prompt_lbl, 0, Qt::AlignHCenter);

        QLabel* sub_lbl = nullptr;
        if (!sub_caption.isEmpty() || out_sub_label != nullptr) {
            left_col->addSpacing(10);
            sub_lbl = new QLabel(sub_caption, left_host);
            sub_lbl->setFont(oobe_font(14, QFont::Normal));
            sub_lbl->setAlignment(Qt::AlignCenter);
            sub_lbl->setWordWrap(true);
            sub_lbl->setFixedWidth(qMin(280, oobe_content_width()));
            sub_lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
            sub_lbl->setVisible(!sub_caption.isEmpty());
            left_col->addWidget(sub_lbl, 0, Qt::AlignHCenter);
            if (out_sub_label) *out_sub_label = sub_lbl;
        }
        if (form != OobeForm::Phone) left_col->addStretch(51);

        auto sync_left_colors = [icon_lbl, icon_name, prompt_lbl, sub_lbl]() {
            const auto& t = FinnThemeManager::instance().tokens();
            const QColor ink = t.ink;
            const qreal dpr = icon_lbl->devicePixelRatioF();
            icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
                icon_name, QSize(46, 46), ink, dpr));

            QPalette p1 = prompt_lbl->palette();
            p1.setColor(QPalette::WindowText, ink);
            prompt_lbl->setPalette(p1);
            if (sub_lbl) {
                QPalette p2 = sub_lbl->palette();
                p2.setColor(QPalette::WindowText, t.dim);
                sub_lbl->setPalette(p2);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_left_colors);
        sync_left_colors();

        // Right column: Figma's x=805, y=195 on desktop; a centered card on
        // tablet and a full-width stacked list on phone.
        auto* right_host = new QWidget(page);
        auto* right_col = new QVBoxLayout(right_host);
        const int right_top = form == OobeForm::Desktop && !flush_right_top ? 195 : 0;
        right_col->setContentsMargins(0, right_top,
                                      form == OobeForm::Desktop ? 45 : 0, 0);
        right_col->setSpacing(0);
        right_col->addWidget(right_column, 1,
                             form == OobeForm::Desktop ? Qt::AlignRight
                                                       : Qt::AlignHCenter);

        if (form == OobeForm::Phone) {
            content_row->addWidget(left_host, 0, Qt::AlignHCenter);
            content_row->addWidget(right_host, 1);
        } else {
            content_row->addWidget(left_host, 1);
            content_row->addSpacing(form == OobeForm::Tablet ? 16 : 132);
            content_row->addWidget(right_host, 1, Qt::AlignRight);
        }

        main_col->addLayout(content_row, 1);
        return page;
    }

    // Step 0: Figma 36:3 ("hi") — continuous rounding arcs that draw into "hi.",
    // and only after that reveal the top Branding (36:45) and Next circle button (36:48)
    // in the exact Figma positions.
    QWidget* buildHiPage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);

        hi_ribbon_ = new FinnRibbonAnimation(FinnRibbonAnimation::Mode::HiIntro, page);
        if (fixed_phase_ >= 0.0) hi_ribbon_->setPhaseTime(fixed_phase_);
        grid->addWidget(hi_ribbon_, 0, 0);

        auto* chrome_layer = new QWidget(page);
        chrome_layer->setAttribute(Qt::WA_NoSystemBackground, true);
        auto* col = new QVBoxLayout(chrome_layer);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);

        // Top stretch so Branding sits at y=180/832 (~21.6% from top)
        col->addStretch(216);

        hi_branding_ = new OobeBrandingHeader(chrome_layer);
        col->addWidget(hi_branding_, 0, Qt::AlignHCenter);

        // Middle stretch across the center where "hi." is drawn at y=50%
        col->addStretch(518);

        hi_next_btn_ = new OobeCircleNavButton(false, true, chrome_layer);
        connect(hi_next_btn_, &QAbstractButton::clicked, this, &OobeWindow::advanceStep);
        col->addWidget(hi_next_btn_, 0, Qt::AlignHCenter);

        // Lock first-page arrow to the exact same bottom inset as positionSharedNav()
        // so the navigation arrows never jump vertically between steps at any resolution.
        col->addSpacing(oobe_form() == OobeForm::Phone ? 24 : 145);

        grid->addWidget(chrome_layer, 0, 0);

        auto sync_reveal = [this]() {
            if (!hi_ribbon_) return;
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
        };
        connect(hi_ribbon_, &FinnRibbonAnimation::frameAdvanced, page,
                [sync_reveal](double) { sync_reveal(); });
        sync_reveal();

        return page;
    }

    // Step 1: Figma 36:53 ("How do we talk?") with world languages + progressive blur
    QWidget* buildLanguagePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);

        // Keep prog_list and blur_ov stacked inside list_wrap
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        struct LangEntry {
            const char* name;
            const char* locale;
        };
        static constexpr LangEntry kLanguages[] = {
            {"English (UK)", "en_GB.UTF-8"},
            {"Spanish", "es_ES.UTF-8"},
            {"Italian", "it_IT.UTF-8"},
            {"French", "fr_FR.UTF-8"},
            {"Catal\u00E0", "ca_ES.UTF-8"},
            {"German", "de_DE.UTF-8"},
            {"Afrikaans", "af_ZA.UTF-8"},
            {"Chinese (Simplified)", "zh_CN.UTF-8"},
            {"Hindi", "hi_IN.UTF-8"},
            {"Korean", "ko_KR.UTF-8"},
            {"Japanese", "ja_JP.UTF-8"},
            {"Arabic", "ar_SA.UTF-8"},
            {"Chinese (Traditional)", "zh_TW.UTF-8"},
            {"Portuguese", "pt_PT.UTF-8"},
            {"English (US)", "en_US.UTF-8"},
            {"Portuguese (Brazil)", "pt_BR.UTF-8"},
            {"Dutch", "nl_NL.UTF-8"},
            {"Polish", "pl_PL.UTF-8"},
            {"Russian", "ru_RU.UTF-8"},
            {"Ukrainian", "uk_UA.UTF-8"},
            {"Turkish", "tr_TR.UTF-8"},
            {"Swedish", "sv_SE.UTF-8"},
            {"Norwegian", "nb_NO.UTF-8"},
            {"Danish", "da_DK.UTF-8"},
            {"Finnish", "fi_FI.UTF-8"},
            {"Greek", "el_GR.UTF-8"},
            {"Czech", "cs_CZ.UTF-8"},
            {"Slovak", "sk_SK.UTF-8"},
            {"Hungarian", "hu_HU.UTF-8"},
            {"Romanian", "ro_RO.UTF-8"},
            {"Bulgarian", "bg_BG.UTF-8"},
            {"Croatian", "hr_HR.UTF-8"},
            {"Serbian", "sr_RS.UTF-8"},
            {"Slovenian", "sl_SI.UTF-8"},
            {"Estonian", "et_EE.UTF-8"},
            {"Latvian", "lv_LV.UTF-8"},
            {"Lithuanian", "lt_LT.UTF-8"},
            {"Icelandic", "is_IS.UTF-8"},
            {"Irish", "ga_IE.UTF-8"},
            {"Welsh", "cy_GB.UTF-8"},
            {"Basque", "eu_ES.UTF-8"},
            {"Galician", "gl_ES.UTF-8"},
            {"Hebrew", "he_IL.UTF-8"},
            {"Persian", "fa_IR.UTF-8"},
            {"Urdu", "ur_PK.UTF-8"},
            {"Bengali", "bn_BD.UTF-8"},
            {"Punjabi", "pa_IN.UTF-8"},
            {"Tamil", "ta_IN.UTF-8"},
            {"Telugu", "te_IN.UTF-8"},
            {"Marathi", "mr_IN.UTF-8"},
            {"Gujarati", "gu_IN.UTF-8"},
            {"Vietnamese", "vi_VN.UTF-8"},
            {"Thai", "th_TH.UTF-8"},
            {"Indonesian", "id_ID.UTF-8"},
            {"Malay", "ms_MY.UTF-8"},
            {"Filipino", "fil_PH.UTF-8"},
            {"Swahili", "sw_KE.UTF-8"},
            {"Amharic", "am_ET.UTF-8"},
        };

        // Pre-select saved locale if present in ~/.config/finn/locale.conf
        QString initial_locale = QStringLiteral("en_GB.UTF-8");
        {
            QFile lf(QDir::homePath() + QStringLiteral("/.config/finn/locale.conf"));
            if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString content = QString::fromUtf8(lf.readAll());
                for (const QString& line : content.split(QLatin1Char('\n'))) {
                    if (line.startsWith(QStringLiteral("LANG="))) {
                        const QString val = line.mid(5).trimmed();
                        if (!val.isEmpty()) initial_locale = val;
                    }
                }
            }
        }

        int initial_idx = 0;
        constexpr int kLangCount = static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));

        // The flow model validates locale/region against a real catalogue.
        // These are exactly the languages this page can select and the
        // host's real timezones, so no invented value can pass the gate.
        {
            finn::shell::oobe::LocaleCatalog catalog;
            for (int i = 0; i < kLangCount; ++i) {
                const QString loc = QString::fromLatin1(kLanguages[i].locale);
                catalog.locales.push_back(loc.toStdString());
                // Must match syncFlowFromUi(): "en_GB.UTF-8" -> "GB".
                catalog.regions.push_back(
                    loc.section(QLatin1Char('_'), -1)
                       .section(QLatin1Char('.'), 0, 0).toStdString());
            }
            for (const QString z : QTimeZone::availableTimeZoneIds()) {
                catalog.timezones.push_back(z.toStdString());
            }
            for (const char* kb : {"us", "gb", "de", "fr", "es", "it", "pt", "br",
                                   "nl", "se", "no", "dk", "fi", "ch", "pl", "cz",
                                   "tr", "ru", "ua", "jp", "kr", "cn", "latam", "ca"}) {
                catalog.keyboard_layouts.emplace_back(kb);
            }
            flow_.setCatalog(std::move(catalog));
        }
        for (int i = 0; i < kLangCount; ++i) {
            if (QString::fromLatin1(kLanguages[i].locale) == initial_locale) {
                initial_idx = i;
                break;
            }
        }
        selected_language_ = QString::fromUtf8(kLanguages[initial_idx].name);
        selected_locale_ = QString::fromLatin1(kLanguages[initial_idx].locale);

        language_rows_.clear();
        for (int i = 0; i < kLangCount; ++i) {
            const QString lang_name = QString::fromUtf8(kLanguages[i].name);
            const QString locale_code = QString::fromLatin1(kLanguages[i].locale);
            auto* row = new OobeOptionRow(lang_name, {}, i == initial_idx,
                                          prog_list->hostWidget());
            language_rows_.push_back(row);
            connect(row, &OobeOptionRow::clicked, this,
                    [this, i, lang_name, locale_code, blur_ov]() {
                        for (int j = 0; j < language_rows_.size(); ++j) {
                            language_rows_[j]->setSelected(j == i);
                        }
                        applyLanguageSelection(lang_name, locale_code);
                        blur_ov->invalidateCache();
                    });
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        if (selected_locale_ != QLatin1String("en_GB.UTF-8")) {
            applyLanguageSelection(selected_language_, selected_locale_);
        }

        return createSplitStepPage(QStringLiteral("language-skill"),
                                   oobe_tr(QStringLiteral("lang_prompt"), selected_locale_),
                                   {}, list_wrap, nullptr, false, &lang_prompt_lbl_);
    }

    // Step 2: Figma 70:3 ("A big world. But one country. Where?")
    // Pure country list matching Figma 1:1 (no search bar, no timezone suffix,
    // no bottom footer). Selecting a country sets both region and default timezone.
    QWidget* buildRegionTimezonePage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        region_prog_list_ = new OobeProgressiveList(list_wrap);
        region_blur_ov_ = new ProgressiveBlurOverlay(region_prog_list_, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(region_prog_list_, 0, 0);
        stack_lay->addWidget(region_blur_ov_, 0, 0);

        struct RegionItem {
            const char* code;
            const char* name;
            const char* default_tz;
        };
        static constexpr RegionItem kRegions[] = {
            {"IT", "Italy", "Europe/Rome"},
            {"GB", "United Kingdom", "Europe/London"},
            {"FR", "France", "Europe/Paris"},
            {"CH", "Switzerland", "Europe/Zurich"},
            {"DE", "Germany", "Europe/Berlin"},
            {"ES", "Spain", "Europe/Madrid"},
            {"PT", "Portugal", "Europe/Lisbon"},
            {"US", "United States", "America/New_York"},
            {"IN", "India", "Asia/Kolkata"},
            {"KR", "Korea", "Asia/Seoul"},
            {"JP", "Japan", "Asia/Tokyo"},
            {"CN", "China", "Asia/Shanghai"},
            {"ID", "Indonesia", "Asia/Jakarta"},
            {"PH", "Philippines", "Asia/Manila"},
            {"CA", "Canada", "America/Toronto"},
            {"NL", "Netherlands", "Europe/Amsterdam"},
            {"SE", "Sweden", "Europe/Stockholm"},
            {"NO", "Norway", "Europe/Oslo"},
            {"DK", "Denmark", "Europe/Copenhagen"},
            {"FI", "Finland", "Europe/Helsinki"},
            {"PL", "Poland", "Europe/Warsaw"},
            {"BR", "Brazil", "America/Sao_Paulo"},
            {"TW", "Taiwan", "Asia/Taipei"},
            {"AU", "Australia", "Australia/Sydney"},
            {"UA", "Ukraine", "Europe/Kyiv"},
            {"TR", "Turkey", "Europe/Istanbul"},
            {"SA", "Saudi Arabia", "Asia/Riyadh"},
            {"ZA", "South Africa", "Africa/Johannesburg"},
        };

        selected_region_ = selected_locale_.section(QLatin1Char('_'), -1)
                                           .section(QLatin1Char('.'), 0, 0);
        if (selected_region_.isEmpty()) selected_region_ = QStringLiteral("GB");
        selected_timezone_ = QString::fromLatin1(QTimeZone::systemTimeZoneId());
        if (selected_timezone_.isEmpty()) selected_timezone_ = QStringLiteral("UTC");

        // Ensure all UI region codes are present in the model's region catalog.
        {
            auto cat = flow_.data().catalog;
            for (const auto& r : kRegions) {
                const std::string rc(r.code);
                if (std::find(cat.regions.begin(), cat.regions.end(), rc) == cat.regions.end()) {
                    cat.regions.push_back(rc);
                }
            }
            if (std::find(cat.timezones.begin(), cat.timezones.end(), "UTC") == cat.timezones.end()) {
                cat.timezones.emplace_back("UTC");
            }
            if (std::find(cat.timezones.begin(), cat.timezones.end(),
                          selected_timezone_.toStdString()) == cat.timezones.end()) {
                selected_timezone_ = QString::fromStdString(cat.timezones.front());
            }
            flow_.setCatalog(std::move(cat));
        }

        region_rows_.clear();
        constexpr int kRegionCount = static_cast<int>(sizeof(kRegions) / sizeof(kRegions[0]));
        int initial_idx = 0;
        for (int i = 0; i < kRegionCount; ++i) {
            if (QString::fromLatin1(kRegions[i].code) == selected_region_) {
                initial_idx = i;
                break;
            }
        }
        selected_region_ = QString::fromLatin1(kRegions[initial_idx].code);
        {
            const std::string def_tz(kRegions[initial_idx].default_tz);
            const auto& tzs = flow_.data().catalog.timezones;
            if (std::find(tzs.begin(), tzs.end(), def_tz) != tzs.end()) {
                selected_timezone_ = QString::fromStdString(def_tz);
            }
        }

        for (int i = 0; i < kRegionCount; ++i) {
            const QString rcode = QString::fromLatin1(kRegions[i].code);
            const QString rname = QString::fromUtf8(kRegions[i].name);
            const QString rtz = QString::fromLatin1(kRegions[i].default_tz);
            auto* row = new OobeOptionRow(rname, {}, i == initial_idx,
                                          region_prog_list_->hostWidget());
            region_rows_.push_back(row);
            connect(row, &OobeOptionRow::clicked, this, [this, i, rcode, rtz]() {
                for (int j = 0; j < region_rows_.size(); ++j) {
                    region_rows_[j]->setSelected(j == i);
                }
                selected_region_ = rcode;
                const auto& tzs = flow_.data().catalog.timezones;
                if (std::find(tzs.begin(), tzs.end(), rtz.toStdString()) != tzs.end()) {
                    selected_timezone_ = rtz;
                }
                if (region_blur_ov_) region_blur_ov_->invalidateCache();
                refreshFlowGate();
            });
            region_prog_list_->listLayout()->addWidget(row);
        }
        region_prog_list_->listLayout()->addStretch(1);

        return createSplitStepPage(QStringLiteral("globe"),
                                   oobe_tr(QStringLiteral("region_prompt"), selected_locale_),
                                   {},
                                   list_wrap, nullptr, false, &region_prompt_lbl_);
    }

    // Step 3: Keyboard layout selection matching the Figma 36:53 / 70:3 list style.
    QWidget* buildKeyboardPage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        struct KbEntry {
            const char* label;
            const char* layout;
            const char* variant;
        };
        static constexpr KbEntry kKeyboards[] = {
            {"English (US)", "us", ""},
            {"English (UK)", "gb", ""},
            {"English (US International)", "us", "intl"},
            {"English (Dvorak)", "us", "dvorak"},
            {"English (Colemak)", "us", "colemak"},
            {"German (QWERTZ)", "de", ""},
            {"French (AZERTY)", "fr", ""},
            {"Spanish", "es", ""},
            {"Latin American", "latam", ""},
            {"Italian", "it", ""},
            {"Portuguese (Portugal)", "pt", ""},
            {"Portuguese (Brazil ABNT2)", "br", ""},
            {"Dutch", "nl", ""},
            {"Swedish", "se", ""},
            {"Norwegian", "no", ""},
            {"Danish", "dk", ""},
            {"Finnish", "fi", ""},
            {"Swiss German", "ch", ""},
            {"Polish", "pl", ""},
            {"Czech", "cz", ""},
            {"Turkish", "tr", ""},
            {"Ukrainian", "ua", ""},
            {"Russian", "ru", ""},
            {"Japanese", "jp", ""},
            {"Korean", "kr", ""},
            {"Canadian Multilingual", "ca", ""},
        };

        selected_kb_layout_ = FinnThemeManager::instance().keyboardLayout();
        if (selected_kb_layout_.isEmpty()) selected_kb_layout_ = QStringLiteral("us");
        selected_kb_variant_ = FinnThemeManager::instance().keyboardVariant();

        auto* prog_list = new OobeProgressiveList(list_wrap);
        auto* blur_ov = new ProgressiveBlurOverlay(prog_list, list_wrap);
        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(prog_list, 0, 0);
        stack_lay->addWidget(blur_ov, 0, 0);

        kb_rows_.clear();
        constexpr int kKbCount = static_cast<int>(sizeof(kKeyboards) / sizeof(kKeyboards[0]));
        for (int i = 0; i < kKbCount; ++i) {
            const QString lbl = QString::fromUtf8(kKeyboards[i].label);
            const QString lay_code = QString::fromLatin1(kKeyboards[i].layout);
            const QString var_code = QString::fromLatin1(kKeyboards[i].variant);
            const bool is_sel = (selected_kb_layout_ == lay_code && selected_kb_variant_ == var_code);
            auto* row = new OobeOptionRow(lbl, {}, is_sel, prog_list->hostWidget());
            kb_rows_.push_back(row);
            connect(row, &OobeOptionRow::clicked, this,
                    [this, i, lay_code, var_code, blur_ov]() {
                        for (int j = 0; j < kb_rows_.size(); ++j) {
                            kb_rows_[j]->setSelected(j == i);
                        }
                        selected_kb_layout_ = lay_code;
                        selected_kb_variant_ = var_code;
                        FinnThemeManager::instance().setKeyboardLayout(lay_code, var_code);
                        blur_ov->invalidateCache();
                        refreshFlowGate();
                    });
            prog_list->listLayout()->addWidget(row);
        }
        prog_list->listLayout()->addStretch(1);

        return createSplitStepPage(QStringLiteral("keyboard"),
                                   oobe_tr(QStringLiteral("kb_prompt"), selected_locale_),
                                   {},
                                   list_wrap, nullptr, false, &kb_prompt_lbl_);
    }

    // Step 4: Wi-Fi / Network in the same Figma 36:53 split layout
    QWidget* buildWifiPage() {
        auto* list_wrap = new QWidget();
        list_wrap->setFixedWidth(oobe_content_width() + 6);
        list_wrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        list_wrap->setAttribute(Qt::WA_TranslucentBackground, true);

        wifi_prog_list_ = new OobeProgressiveList(list_wrap);
        wifi_blur_ov_ = new ProgressiveBlurOverlay(wifi_prog_list_, list_wrap);

        auto* stack_lay = new QGridLayout(list_wrap);
        stack_lay->setContentsMargins(0, 0, 0, 0);
        stack_lay->addWidget(wifi_prog_list_, 0, 0);
        stack_lay->addWidget(wifi_blur_ov_, 0, 0);

        return createSplitStepPage(QStringLiteral("wifi-02"),
                                   oobe_tr(QStringLiteral("wifi_prompt"), selected_locale_),
                                   {},
                                   list_wrap, nullptr, false, &wifi_prompt_lbl_);
    }

    // Step 3: Account setup with Figma PFP color picker + account fields
    QWidget* buildAccountPage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(0);

        // Live Figma PFP preview + color variant swatches
        pfp_preview_lbl_ = new QLabel(col_host);
        pfp_preview_lbl_->setFixedSize(72, 72);
        pfp_preview_lbl_->setAlignment(Qt::AlignCenter);
        col->addWidget(pfp_preview_lbl_, 0, Qt::AlignHCenter);
        col->addSpacing(14);

        auto* swatch_row = new QWidget(col_host);
        auto* swatch_lay = new QHBoxLayout(swatch_row);
        swatch_lay->setContentsMargins(0, 0, 0, 0);
        swatch_lay->setSpacing(6);

        pfp_swatches_.clear();
        for (const auto& preset : FinnThemeManager::avatarPresets()) {
            auto* sw = new OobePfpSwatch(preset, swatch_row);
            connect(sw, &QAbstractButton::clicked, this, [id = preset.id]() {
                FinnThemeManager::instance().setAvatarColor(id);
            });
            pfp_swatches_.push_back(sw);
            swatch_lay->addWidget(sw);
        }
        col->addWidget(swatch_row, 0, Qt::AlignHCenter);
        col->addSpacing(18);

        auto sync_pfp = [this]() {
            auto& tm = FinnThemeManager::instance();
            const QString current_id = tm.avatarColor();
            if (pfp_preview_lbl_) {
                pfp_preview_lbl_->setPixmap(
                    tm.avatarPixmap(QSize(72, 72), pfp_preview_lbl_->devicePixelRatioF()));
            }
            for (auto* sw : pfp_swatches_) {
                if (sw) sw->setChecked(sw->presetId() == current_id);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                col_host, sync_pfp);
        sync_pfp();

        fullname_edit_ = new OobeInputField(oobe_tr(QStringLiteral("full_name"), selected_locale_), false, col_host);
        username_edit_ = new OobeInputField(oobe_tr(QStringLiteral("account_name"), selected_locale_), false, col_host);
        const QString default_user = QString::fromLocal8Bit(qgetenv("USER")).trimmed();
        const QString safe_default_user =
            (!default_user.isEmpty() &&
             finn::shell::oobe::is_valid_posix_username(default_user.toStdString()))
                ? default_user
                : QStringLiteral("finn");
        username_edit_->setText(safe_default_user);
        hostname_edit_ = new OobeInputField(oobe_tr(QStringLiteral("computer_name"), selected_locale_), false, col_host);
        hostname_edit_->setText(QStringLiteral("finnos"));
        pw_edit_ = new OobeInputField(oobe_tr(QStringLiteral("password"), selected_locale_), true, col_host);
        pw_verify_edit_ = new OobeInputField(oobe_tr(QStringLiteral("verify_password"), selected_locale_), true, col_host);

        connect(fullname_edit_->lineEdit(), &QLineEdit::textChanged, this, [this](const QString& txt) {
            if (!hostname_manually_edited_ && hostname_edit_) {
                const QString suggested = QString::fromStdString(
                    finn::shell::oobe::suggest_hostname_from_name(txt.toStdString()));
                QSignalBlocker b(hostname_edit_->lineEdit());
                hostname_edit_->setText(suggested);
            }
            refreshFlowGate();
        });
        connect(username_edit_->lineEdit(), &QLineEdit::textChanged, this, [this]() {
            refreshFlowGate();
        });
        connect(hostname_edit_->lineEdit(), &QLineEdit::textEdited, this, [this]() {
            hostname_manually_edited_ = true;
            refreshFlowGate();
        });

        pw_hint_lbl_ = new QLabel(oobe_tr(QStringLiteral("pw_mismatch"), selected_locale_), col_host);
        pw_hint_lbl_->setFont(oobe_font(12, QFont::Normal));
        pw_hint_lbl_->setContentsMargins(6, 0, 0, 0);
        pw_hint_lbl_->setVisible(false);

        auto sync_pw_hint = [this]() {
            if (!pw_hint_lbl_ || !pw_edit_ || !pw_verify_edit_) return;
            const QString p1 = pw_edit_->text();
            const QString p2 = pw_verify_edit_->text();
            const bool mismatch = !p2.isEmpty() && p1 != p2;
            pw_hint_lbl_->setVisible(mismatch);
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = pw_hint_lbl_->palette();
            pal.setColor(QPalette::WindowText, t.danger);
            pw_hint_lbl_->setPalette(pal);
        };
        connect(pw_edit_->lineEdit(), &QLineEdit::textChanged, col_host, [this, sync_pw_hint]() {
            sync_pw_hint();
            refreshFlowGate();
        });
        connect(pw_verify_edit_->lineEdit(), &QLineEdit::textChanged, col_host, [this, sync_pw_hint]() {
            sync_pw_hint();
            refreshFlowGate();
        });
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                col_host, sync_pw_hint);

        col->addWidget(fullname_edit_);
        col->addSpacing(8);
        col->addWidget(username_edit_);
        col->addSpacing(8);
        col->addWidget(hostname_edit_);
        col->addSpacing(8);
        col->addWidget(pw_edit_);
        col->addSpacing(8);
        col->addWidget(pw_verify_edit_);
        col->addSpacing(6);
        col->addWidget(pw_hint_lbl_);
        col->addStretch(1);

        return createSplitStepPage(QStringLiteral("user"),
                                   oobe_tr(QStringLiteral("account_prompt"), selected_locale_), {},
                                   col_host, nullptr, false, &account_prompt_lbl_);
    }

    // Step 4: Appearance page with interactive 3D pendant lamp filling the entire right half,
    // wire anchored at the ceiling (y=0), and Back/Next navigation buttons overlaid on top.
    QWidget* buildAppearancePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* stack_grid = new QGridLayout(page);
        stack_grid->setContentsMargins(0, 0, 0, 0);
        stack_grid->setSpacing(0);

        // Base layer: left half prompt + right half full-height 3D lamp area
        auto* base_layer = new QWidget(page);
        base_layer->setAutoFillBackground(false);
        auto* halves = new QHBoxLayout(base_layer);
        halves->setContentsMargins(0, 0, 0, 0);
        halves->setSpacing(0);

        auto* left_host = new QWidget(base_layer);
        auto* left_col = new QVBoxLayout(left_host);
        left_col->setContentsMargins(80, 0, 20, 0);
        left_col->setSpacing(0);
        left_col->addStretch(49);

        auto* icon_lbl = new QLabel(left_host);
        icon_lbl->setFixedSize(46, 46);
        icon_lbl->setAlignment(Qt::AlignCenter);
        left_col->addWidget(icon_lbl, 0, Qt::AlignHCenter);
        left_col->addSpacing(40);

        appearance_prompt_lbl_ = new QLabel(left_host);
        appearance_prompt_lbl_->setFont(oobe_font(24, QFont::Normal));
        appearance_prompt_lbl_->setAlignment(Qt::AlignCenter);
        appearance_prompt_lbl_->setWordWrap(true);
        appearance_prompt_lbl_->setFixedWidth(380);
        appearance_prompt_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        set_wrapped_label_text(appearance_prompt_lbl_,
                               oobe_tr(QStringLiteral("appearance_prompt"), selected_locale_));
        left_col->addWidget(appearance_prompt_lbl_, 0, Qt::AlignHCenter);
        left_col->addStretch(51);

        auto sync_left_colors = [icon_lbl, this]() {
            const auto& t = FinnThemeManager::instance().tokens();
            const QColor ink = t.ink;
            const qreal dpr = icon_lbl->devicePixelRatioF();
            icon_lbl->setPixmap(FinnThemeManager::instance().iconPixmap(
                QStringLiteral("oobe-appearance"), QSize(46, 46), ink, dpr));
            if (appearance_prompt_lbl_) {
                QPalette p1 = appearance_prompt_lbl_->palette();
                p1.setColor(QPalette::WindowText, ink);
                appearance_prompt_lbl_->setPalette(p1);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_left_colors);
        sync_left_colors();

        auto* lamp = new finn::oobe::OobeLamp3DWidget(page);
        selected_theme_ = FinnThemeManager::instance().isDark()
            ? finn::shell::oobe::ThemeChoice::Dark
            : finn::shell::oobe::ThemeChoice::Light;

        connect(lamp, &finn::oobe::OobeLamp3DWidget::themeToggled, this,
                [this](bool is_dark) {
                    selected_theme_ = is_dark ? finn::shell::oobe::ThemeChoice::Dark
                                              : finn::shell::oobe::ThemeChoice::Light;
                    refreshFlowGate();
                });

        halves->addWidget(left_host, 1);
        halves->addStretch(1);
        stack_grid->addWidget(base_layer, 0, 0);
        stack_grid->addWidget(lamp, 0, 0);
        return page;
    }

    // Step 7: Accessibility toggles in the same Figma 36:53 split layout
    QWidget* buildAccessibilityPage() {
        auto* col_host = new QWidget();
        col_host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(col_host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(10);

        auto& tm = FinnThemeManager::instance();
        const auto s = finn::sys::load_merged_settings();
        sticky_keys_active_ = s.sticky_keys;

        const QString orca_bin = QStandardPaths::findExecutable(QStringLiteral("orca"));
        const QString spd_bin = QStandardPaths::findExecutable(QStringLiteral("spd-say"));
        const QString espeak_bin = QStandardPaths::findExecutable(QStringLiteral("espeak-ng"));
        const bool has_sr_backend = !orca_bin.isEmpty() || !spd_bin.isEmpty() || !espeak_bin.isEmpty();

        auto* sr = a11y_sr_ = new OobeToggleRow(
            oobe_tr(QStringLiteral("a11y_sr"), selected_locale_),
            has_sr_backend && s.screen_reader, col_host);
        if (!has_sr_backend) {
            sr->setEnabled(false);
        } else {
            connect(sr, &OobeToggleRow::toggled, this, [this, orca_bin, spd_bin, espeak_bin](bool on) {
                FinnThemeManager::instance().setScreenReader(on);
                if (on) {
                    if (!orca_bin.isEmpty()) {
                        QProcess::startDetached(orca_bin, {QStringLiteral("--replace")});
                    } else if (!spd_bin.isEmpty()) {
                        QProcess::startDetached(spd_bin, {oobe_tr(QStringLiteral("a11y_sr"), selected_locale_)});
                    } else if (!espeak_bin.isEmpty()) {
                        QProcess::startDetached(espeak_bin, {oobe_tr(QStringLiteral("a11y_sr"), selected_locale_)});
                    }
                } else if (!orca_bin.isEmpty()) {
                    QProcess::startDetached(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("orca")});
                }
            });
        }
        col->addWidget(sr);

        auto* hc = a11y_hc_ = new OobeToggleRow(
            oobe_tr(QStringLiteral("a11y_hc"), selected_locale_),
            tm.tokens().high_contrast, col_host);
        connect(hc, &OobeToggleRow::toggled, this, [this](bool on) {
            FinnThemeManager::instance().setHighContrast(on);
            update();
        });
        col->addWidget(hc);

        auto* rm = a11y_rm_ = new OobeToggleRow(
            oobe_tr(QStringLiteral("a11y_rm"), selected_locale_),
            tm.tokens().reduced_motion, col_host);
        connect(rm, &OobeToggleRow::toggled, this, [](bool on) {
            FinnThemeManager::instance().setReducedMotion(on);
        });
        col->addWidget(rm);

        auto* sk = a11y_sk_ = new OobeToggleRow(
            oobe_tr(QStringLiteral("a11y_sk"), selected_locale_),
            s.sticky_keys, col_host);
        connect(sk, &OobeToggleRow::toggled, this, [this](bool on) {
            sticky_keys_active_ = on;
            sticky_latched_mods_ = Qt::NoModifier;
            FinnThemeManager::instance().setStickyKeys(on);
        });
        col->addWidget(sk);

        connect(&tm, &FinnThemeManager::themeChanged, this,
                [this, sr, hc, rm, sk, has_sr_backend]() {
                    const auto& st = FinnThemeManager::instance().settings();
                    sticky_keys_active_ = st.sticky_keys;
                    QSignalBlocker b1(sr->toggle());
                    QSignalBlocker b2(hc->toggle());
                    QSignalBlocker b3(rm->toggle());
                    QSignalBlocker b4(sk->toggle());
                    sr->toggle()->setChecked(has_sr_backend && st.screen_reader);
                    hc->toggle()->setChecked(st.high_contrast);
                    rm->toggle()->setChecked(st.reduced_motion);
                    sk->toggle()->setChecked(st.sticky_keys);
                });

        col->addStretch(1);
        return createSplitStepPage(QStringLiteral("accessibility"),
                                   oobe_tr(QStringLiteral("a11y_prompt"), selected_locale_), {},
                                   col_host, nullptr, false, &a11y_prompt_lbl_);
    }

    // Step 8: Explicit privacy acknowledgement. FinnOS has no telemetry,
    // location or crash-upload service, so those rows are shown disabled.
    QWidget* buildPrivacyPage() {
        auto* host = new QWidget();
        host->setFixedWidth(oobe_content_width() + 6);
        auto* col = new QVBoxLayout(host);
        col->setContentsMargins(0, 0, 6, 0);
        col->setSpacing(12);

        privacy_info_lbl_ = new QLabel(host);
        privacy_info_lbl_->setWordWrap(true);
        privacy_info_lbl_->setFont(oobe_font(14, QFont::Normal));
        privacy_info_lbl_->setFixedWidth(oobe_content_width());
        privacy_info_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        set_wrapped_label_text(privacy_info_lbl_,
                               oobe_tr(QStringLiteral("privacy_info"), selected_locale_));
        auto sync_info_col = [this]() {
            if (!privacy_info_lbl_) return;
            const auto& t = FinnThemeManager::instance().tokens();
            QPalette pal = privacy_info_lbl_->palette();
            pal.setColor(QPalette::WindowText, t.dim);
            privacy_info_lbl_->setPalette(pal);
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                host, sync_info_col);
        sync_info_col();
        col->addWidget(privacy_info_lbl_);

        privacy_loc_row_ = new OobeToggleRow(oobe_tr(QStringLiteral("privacy_loc"), selected_locale_), false, host);
        privacy_diag_row_ = new OobeToggleRow(oobe_tr(QStringLiteral("privacy_diag"), selected_locale_), false, host);
        privacy_crash_row_ = new OobeToggleRow(oobe_tr(QStringLiteral("privacy_crash"), selected_locale_), false, host);
        for (auto* row : {privacy_loc_row_, privacy_diag_row_, privacy_crash_row_}) {
            row->setEnabled(false);
            col->addWidget(row);
        }

        privacy_ack_row_ = new OobeToggleRow(
            oobe_tr(QStringLiteral("privacy_ack"), selected_locale_), false, host);
        connect(privacy_ack_row_, &OobeToggleRow::toggled, this,
                [this]() { refreshFlowGate(); });
        col->addWidget(privacy_ack_row_);
        col->addStretch(1);

        return createSplitStepPage(QStringLiteral("cog"),
                                   oobe_tr(QStringLiteral("privacy_prompt"), selected_locale_), {},
                                   host, nullptr, false, &privacy_prompt_lbl_);
    }

    // Step 9: Plain, calm Completion / Update Successful Screen matching OOBE's minimal design
    QWidget* buildCompletePage() {
        auto* page = new QWidget();
        page->setAutoFillBackground(false);

        auto* col = new QVBoxLayout(page);
        col->setContentsMargins(oobe_gutter(), 0, oobe_gutter(), 0);
        col->setSpacing(0);

        col->addStretch(45);

        complete_icon_lbl_ = new QLabel(page);
        complete_icon_lbl_->setFixedSize(56, 56);
        complete_icon_lbl_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_icon_lbl_, 0, Qt::AlignHCenter);

        col->addSpacing(28);

        complete_title_ = new QLabel(
            update_mode_ ? oobe_tr(QStringLiteral("update_title"), selected_locale_)
                         : oobe_tr(QStringLiteral("complete_title"), selected_locale_),
            page);
        complete_title_->setFont(oobe_font(26, QFont::Medium));
        complete_title_->setAlignment(Qt::AlignCenter);
        col->addWidget(complete_title_, 0, Qt::AlignHCenter);

        col->addSpacing(10);

        complete_sub_lbl_ = new QLabel(
            update_mode_ ? QStringLiteral("FinnOS is up to date.")
                         : QStringLiteral("FinnOS is ready for you."),
            page);
        complete_sub_lbl_->setFont(oobe_font(14, QFont::Normal));
        complete_sub_lbl_->setAlignment(Qt::AlignCenter);
        complete_sub_lbl_->setWordWrap(true);
        complete_sub_lbl_->setFixedWidth(qMin(360, oobe_content_width()));
        col->addWidget(complete_sub_lbl_, 0, Qt::AlignHCenter);

        auto sync_complete_colors = [this]() {
            const auto& t = FinnThemeManager::instance().tokens();
            if (complete_icon_lbl_) {
                complete_icon_lbl_->setPixmap(FinnThemeManager::instance().iconPixmap(
                    QStringLiteral("boot-mark"), QSize(56, 56), t.ink,
                    complete_icon_lbl_->devicePixelRatioF()));
            }
            if (complete_title_) {
                QPalette pal = complete_title_->palette();
                pal.setColor(QPalette::WindowText, t.ink);
                complete_title_->setPalette(pal);
            }
            if (complete_sub_lbl_) {
                QPalette pal = complete_sub_lbl_->palette();
                pal.setColor(QPalette::WindowText, t.dim);
                complete_sub_lbl_->setPalette(pal);
            }
            if (complete_status_lbl_) {
                QPalette spal = complete_status_lbl_->palette();
                spal.setColor(QPalette::WindowText,
                              complete_has_error_ ? t.danger : t.dim);
                complete_status_lbl_->setPalette(spal);
            }
        };
        connect(&FinnThemeManager::instance(), &FinnThemeManager::themeChanged,
                page, sync_complete_colors);

        col->addSpacing(32);

        start_btn_ = new OobeGetStartedPill(
            oobe_tr(QStringLiteral("get_started"), selected_locale_), page);
        connect(start_btn_, &QAbstractButton::clicked, this,
                &OobeWindow::finishSetup);
        col->addWidget(start_btn_, 0, Qt::AlignHCenter);

        col->addSpacing(14);
        complete_status_lbl_ = new QLabel(QString(), page);
        complete_status_lbl_->setFont(oobe_font(13, QFont::Normal));
        complete_status_lbl_->setAlignment(Qt::AlignCenter);
        complete_status_lbl_->setWordWrap(true);
        complete_status_lbl_->setFixedWidth(460);
        complete_status_lbl_->setVisible(false);
        col->addWidget(complete_status_lbl_, 0, Qt::AlignHCenter);

        sync_complete_colors();

        col->addStretch(55);
        return page;
    }

    QString promptWifiPassphrase(const QString& ssid) {
        const auto& t = FinnThemeManager::instance().tokens();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Wi-Fi Password"));
        dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dlg.setModal(true);
        dlg.setAccessibleName(QStringLiteral("Wi-Fi password prompt for %1").arg(ssid));

        auto* root = new QVBoxLayout(&dlg);
        root->setContentsMargins(20, 18, 20, 18);
        root->setSpacing(12);

        auto* title = new FinnLabel(
            oobe_tr(QStringLiteral("wifi_connect_to"), selected_locale_).arg(ssid),
            FinnLabel::Role::Headline, FinnLabel::Tone::Ink, &dlg);
        root->addWidget(title);

        auto* field = new OobeInputField(oobe_tr(QStringLiteral("wifi_pwd_title"), selected_locale_), true, &dlg);
        field->lineEdit()->setAccessibleName(oobe_tr(QStringLiteral("wifi_connect_to"), selected_locale_).arg(ssid));
        root->addWidget(field);

        auto* btn_row = new QHBoxLayout();
        btn_row->setSpacing(8);
        btn_row->addStretch(1);
        auto* cancel_btn = new FinnButton(
            oobe_tr(QStringLiteral("cancel"), selected_locale_), FinnButton::Variant::Secondary, {}, &dlg);
        auto* join_btn = new FinnButton(
            oobe_tr(QStringLiteral("connect"), selected_locale_), FinnButton::Variant::Primary, {}, &dlg);
        cancel_btn->setAccessibleName(QStringLiteral("Cancel Wi-Fi connection"));
        join_btn->setAccessibleName(QStringLiteral("Connect to %1").arg(ssid));
        btn_row->addWidget(cancel_btn);
        btn_row->addWidget(join_btn);
        root->addLayout(btn_row);

        QPalette dpal = dlg.palette();
        dpal.setColor(QPalette::Window, t.tile);
        dpal.setColor(QPalette::WindowText, t.ink);
        dlg.setPalette(dpal);
        dlg.setAutoFillBackground(true);

        connect(cancel_btn, &QAbstractButton::clicked, &dlg, &QDialog::reject);
        connect(join_btn, &QAbstractButton::clicked, &dlg, &QDialog::accept);
        connect(field->lineEdit(), &QLineEdit::returnPressed, &dlg, &QDialog::accept);
        field->lineEdit()->setFocus();

        return (dlg.exec() == QDialog::Accepted) ? field->text() : QString();
    }

    void syncNetwork() {
        if (!wifi_prog_list_) return;
        const auto& net = finn::sys::FinnDesktopServices::instance().network();

        auto* lay = wifi_prog_list_->listLayout();
        while (QLayoutItem* item = lay->takeAt(0)) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }

        if (net.wired_connected) {
            auto* eth_row = new OobeOptionRow(
                oobe_tr(QStringLiteral("wifi_ethernet"), selected_locale_), net.ipv4_summary, true,
                wifi_prog_list_->hostWidget());
            lay->addWidget(eth_row);
        }

        for (const auto& w : net.networks) {
            const QString detail =
                w.active ? oobe_tr(QStringLiteral("wifi_connected"), selected_locale_)
                         : QStringLiteral("%1%").arg(w.signal);
            auto* row = new OobeOptionRow(w.ssid, detail, w.active,
                                          wifi_prog_list_->hostWidget());
            connect(row, &OobeOptionRow::clicked, this, [this, w]() {
                if (w.active) return;
                QString pwd;
                if (w.secured) {
                    pwd = promptWifiPassphrase(w.ssid);
                    if (pwd.isEmpty()) return;
                }
                finn::sys::FinnDesktopServices::instance().connectWifi(w.ssid, pwd);
            });
            lay->addWidget(row);
        }

        auto* offline_row = new OobeOptionRow(
            oobe_tr(QStringLiteral("wifi_offline"), selected_locale_), {},
            continue_offline_selected_ || (!net.wired_connected && net.active_wifi_ssid.isEmpty()),
            wifi_prog_list_->hostWidget());
        connect(offline_row, &OobeOptionRow::clicked, this, [this]() {
            continue_offline_selected_ = true;
            refreshFlowGate();
            advanceStep();
        });
        lay->addWidget(offline_row);
        lay->addStretch(1);

        if (wifi_blur_ov_) {
            const int row_count = (net.wired_connected ? 1 : 0) +
                                  static_cast<int>(net.networks.size()) + 1;
            wifi_blur_ov_->setVisible(row_count > 6);
            wifi_blur_ov_->invalidateCache();
        }
    }

    // Feed the model only from measurements or explicit user actions. A
    // setter that refuses inconsistent state leaves the model untouched, so
    // a stale "connected"/"created" can never survive a changed capability.
    void syncFlowFromUi() {
        using namespace finn::shell::oobe;
        const OobeStep step = step_for_ui_index(stack_->currentIndex());
        switch (step) {
            case OobeStep::Connectivity: {
                ConnectivityState st;
                const auto caps = flow_.capabilities();
                const auto& net = finn::sys::FinnDesktopServices::instance().network();
                if (caps.network_manager.state == Availability::Available &&
                    !net.active_wifi_ssid.isEmpty()) {
                    st.attempt = NetworkAttempt::Connected;
                    st.ssid = net.active_wifi_ssid.toStdString();
                    st.detail = "Wi-Fi link reported by the network service";
                } else if (caps.network_manager.state == Availability::Available &&
                           net.wired_connected) {
                    st.attempt = NetworkAttempt::Connected;
                    st.ssid = "Wired network";
                    st.detail = "Ethernet link reported by the network service";
                } else if (continue_offline_selected_) {
                    st.attempt = caps.network_manager.state == Availability::Unavailable
                        ? NetworkAttempt::Unavailable : NetworkAttempt::Failed;
                    st.continue_offline = true;
                    st.detail = caps.network_manager.state == Availability::Unavailable
                        ? caps.network_manager.detail
                        : "No active network was reported by the network service";
                } else {
                    st.attempt = NetworkAttempt::NotAttempted;
                    st.detail = "no connection has been reported";
                }
                flow_.setConnectivity(std::move(st));
                break;
            }
            case OobeStep::Language: {
                flow_.setLanguage(selected_locale_.toStdString());
                break;
            }
            case OobeStep::RegionTimezone: {
                RegionState st;
                st.locale = selected_locale_.toStdString();
                st.region = selected_region_.toStdString();
                st.timezone = selected_timezone_.toStdString();
                flow_.setRegion(std::move(st));
                break;
            }
            case OobeStep::Keyboard: {
                KeyboardState kb;
                kb.layout = selected_kb_layout_.toStdString();
                kb.variant = selected_kb_variant_.toStdString();
                flow_.setKeyboard(std::move(kb));
                break;
            }
            case OobeStep::Account: {
                AccountState st;
                const QString name = fullname_edit_ ? fullname_edit_->text().trimmed()
                                                    : QString();
                const QString user = username_edit_ ? username_edit_->text().trimmed()
                                                    : QStringLiteral("finn");
                const QString host = hostname_edit_ ? hostname_edit_->text().trimmed().toLower()
                                                    : QStringLiteral("finnos");
                const QString p1 = pw_edit_ ? pw_edit_->text() : QString();
                const QString p2 = pw_verify_edit_ ? pw_verify_edit_->text() : QString();
                st.display_name = name.toStdString();
                st.username = user.toStdString();
                st.hostname = host.toStdString();
                st.passwords_match = (p1.isEmpty() && p2.isEmpty()) || (p1 == p2);
                if (!name.isEmpty()) {
                    st.outcome = AccountOutcome::LocalOnly;
                    st.detail = "local profile; account is provisioned at Finish";
                }
                flow_.setAccount(std::move(st));
                break;
            }
            case OobeStep::Privacy:
                flow_.setPrivacy(PrivacyState{false, false, false,
                                              privacy_ack_row_ && privacy_ack_row_->toggle()->isChecked()});
                break;
            case OobeStep::Accessibility: {
                AccessibilityState st;
                const auto tri = [](OobeToggleRow* row) {
                    return !row ? TriState::Unset
                                : (row->toggle()->isChecked() ? TriState::On
                                                              : TriState::Off);
                };
                st.reduced_motion = tri(a11y_rm_);
                st.high_contrast = tri(a11y_hc_);
                st.screen_reader = tri(a11y_sr_);
                if (st.screen_reader == TriState::On &&
                    flow_.capabilities().accessibility_bus.state != Availability::Available) {
                    st.screen_reader = TriState::Unset;  // cannot claim it
                }
                flow_.setAccessibility(st);
                break;
            }
            case OobeStep::Theme:
                flow_.setTheme(ThemeState{selected_theme_});
                break;
            default:
                break;
        }
    }

    void refreshFlowGate() {
        syncFlowFromUi();
        const auto v = flow_.validate(step_for_ui_index(stack_->currentIndex()));
        if (shared_next_btn_) shared_next_btn_->setEnabled(v.can_advance);
    }

    void previousStep() {
        if (flow_.back()) {
            setStep(ui_index_for_step(flow_.current()));
            refreshFlowGate();
        }
    }

    void advanceStep() {
        const int cur = stack_->currentIndex();
        if (cur == 5 && pw_edit_ && pw_verify_edit_) {
            const QString p1 = pw_edit_->text();
            const QString p2 = pw_verify_edit_->text();
            if ((!p1.isEmpty() || !p2.isEmpty()) && p1 != p2) {
                if (pw_hint_lbl_) pw_hint_lbl_->setVisible(true);
                pw_verify_edit_->lineEdit()->setFocus();
                return;
            }
        }
        syncFlowFromUi();
        if (!flow_.advance()) {
            refreshFlowGate();
            return;
        }
        setStep(ui_index_for_step(flow_.current()));
        refreshFlowGate();
    }

    void positionSharedNav() {
        if (!shared_nav_box_) return;
        const int x = (width() - shared_nav_box_->width()) / 2;
        // Desktop keeps Figma's y=631 on an 832 px frame (145 px from the
        // bottom). Phone moves the pair into the bottom safe-area strip.
        const int bottom = oobe_form() == OobeForm::Phone ? 24 : 145;
        const int y = height() - bottom - shared_nav_box_->height();
        shared_nav_box_->move(x, y);
    }

    QPixmap grabTransparentStack() const {
        if (!stack_ || stack_->size().isEmpty()) return {};
        const qreal dpr = devicePixelRatioF();
        QPixmap pix(qRound(stack_->width() * dpr), qRound(stack_->height() * dpr));
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        const bool was_visible = stack_->isVisible();
        if (!was_visible) stack_->setVisible(true);
        stack_->render(&pix, QPoint(), QRegion(), QWidget::DrawChildren);
        if (!was_visible) stack_->setVisible(false);
        return pix;
    }

    void setStep(int idx, bool animate_slide = true) {
        if (shared_nav_box_) {
            shared_nav_box_->setVisible(idx >= 1 && idx <= 8);
            shared_nav_box_->raise();
        }
        if (hi_lang_overlay_ && idx != 1) {
            hi_lang_overlay_->hide();
            hi_lang_overlay_->deleteLater();
            hi_lang_overlay_.clear();
        }
        const int prev = stack_->currentIndex();
        if (prev == idx) return;

        const bool is_hi_lang_pair = (prev == 0 && idx == 1) || (prev == 1 && idx == 0);
        const bool do_slide = animate_slide && !is_hi_lang_pair && prev >= 0 &&
                              fixed_phase_ < 0.0 &&
                              !FinnThemeManager::instance().tokens().reduced_motion;

        QPixmap old_snap;
        if (do_slide) {
            if (slide_overlay_) {
                old_snap = slide_overlay_->grabCurrentFrame();
                slide_overlay_->cancel();
                slide_overlay_.clear();
            } else {
                old_snap = grabTransparentStack();
            }
        } else if (slide_overlay_) {
            slide_overlay_->finish();
            slide_overlay_.clear();
        }

        stack_->setCurrentIndex(idx);
        if (shared_nav_box_) {
            shared_nav_box_->setVisible(idx >= 1 && idx <= 8);
            shared_nav_box_->raise();
        }
        if (prev == 0 && idx == 1 && animate_slide && fixed_phase_ < 0.0 &&
            !FinnThemeManager::instance().tokens().reduced_motion) {
            hi_lang_overlay_ = new OobeHiToLangOverlay(this, lang_prompt_lbl_);
            if (shared_nav_box_) shared_nav_box_->raise();
        }
        if (idx == 0 && hi_ribbon_) {
            if (fixed_phase_ < 0.0) hi_ribbon_->restart();
            const qreal rev = hi_ribbon_->uiRevealProgress();
            if (hi_branding_) hi_branding_->setRevealOpacity(rev);
            if (hi_next_btn_) hi_next_btn_->setRevealOpacity(rev);
            return;
        }
        if (do_slide && !old_snap.isNull()) {
            stack_->widget(idx)->resize(stack_->size());
            if (stack_->widget(idx)->layout()) {
                stack_->widget(idx)->layout()->activate();
            }
            const QPixmap new_snap = grabTransparentStack();
            slide_overlay_ = new OobeSlideTransitionOverlay(
                this, stack_, std::move(old_snap), new_snap, idx > prev);
            if (shared_nav_box_) shared_nav_box_->raise();
        }
        if (idx >= 1 && idx <= 8) refreshFlowGate();
    }

    void applyLanguageSelection(const QString& lang_name, const QString& locale_code) {
        selected_language_ = lang_name;
        selected_locale_ = locale_code;

        const QString short_loc = locale_code.section(QLatin1Char('.'), 0, 0);
        QLocale::setDefault(QLocale(short_loc));
        qputenv("LANG", locale_code.toUtf8());
        qputenv("LC_ALL", locale_code.toUtf8());

        set_wrapped_label_text(lang_prompt_lbl_, oobe_tr(QStringLiteral("lang_prompt"), locale_code));
        set_wrapped_label_text(region_prompt_lbl_, oobe_tr(QStringLiteral("region_prompt"), locale_code));
        set_wrapped_label_text(kb_prompt_lbl_, oobe_tr(QStringLiteral("kb_prompt"), locale_code));
        set_wrapped_label_text(wifi_prompt_lbl_, oobe_tr(QStringLiteral("wifi_prompt"), locale_code));
        set_wrapped_label_text(account_prompt_lbl_, oobe_tr(QStringLiteral("account_prompt"), locale_code));
        if (fullname_edit_)
            fullname_edit_->setPlaceholderText(oobe_tr(QStringLiteral("full_name"), locale_code));
        if (username_edit_)
            username_edit_->setPlaceholderText(oobe_tr(QStringLiteral("account_name"), locale_code));
        if (hostname_edit_)
            hostname_edit_->setPlaceholderText(oobe_tr(QStringLiteral("computer_name"), locale_code));
        if (pw_edit_)
            pw_edit_->setPlaceholderText(oobe_tr(QStringLiteral("password"), locale_code));
        if (pw_verify_edit_)
            pw_verify_edit_->setPlaceholderText(oobe_tr(QStringLiteral("verify_password"), locale_code));
        if (pw_hint_lbl_)
            pw_hint_lbl_->setText(oobe_tr(QStringLiteral("pw_mismatch"), locale_code));
        set_wrapped_label_text(appearance_prompt_lbl_, oobe_tr(QStringLiteral("appearance_prompt"), locale_code));
        set_wrapped_label_text(a11y_prompt_lbl_, oobe_tr(QStringLiteral("a11y_prompt"), locale_code));
        if (a11y_sr_)
            a11y_sr_->setLabel(oobe_tr(QStringLiteral("a11y_sr"), locale_code));
        if (a11y_hc_)
            a11y_hc_->setLabel(oobe_tr(QStringLiteral("a11y_hc"), locale_code));
        if (a11y_rm_)
            a11y_rm_->setLabel(oobe_tr(QStringLiteral("a11y_rm"), locale_code));
        if (a11y_sk_)
            a11y_sk_->setLabel(oobe_tr(QStringLiteral("a11y_sk"), locale_code));
        set_wrapped_label_text(privacy_prompt_lbl_, oobe_tr(QStringLiteral("privacy_prompt"), locale_code));
        set_wrapped_label_text(privacy_info_lbl_, oobe_tr(QStringLiteral("privacy_info"), locale_code));
        if (privacy_loc_row_)
            privacy_loc_row_->setLabel(oobe_tr(QStringLiteral("privacy_loc"), locale_code));
        if (privacy_diag_row_)
            privacy_diag_row_->setLabel(oobe_tr(QStringLiteral("privacy_diag"), locale_code));
        if (privacy_crash_row_)
            privacy_crash_row_->setLabel(oobe_tr(QStringLiteral("privacy_crash"), locale_code));
        if (privacy_ack_row_)
            privacy_ack_row_->setLabel(oobe_tr(QStringLiteral("privacy_ack"), locale_code));
        if (complete_title_)
            complete_title_->setText(oobe_tr(update_mode_ ? QStringLiteral("update_title")
                                                          : QStringLiteral("complete_title"),
                                             locale_code));
        if (start_btn_)
            start_btn_->setText(oobe_tr(QStringLiteral("get_started"), locale_code));
        syncNetwork();

        const QString conf_path =
            QDir::homePath() + QStringLiteral("/.config/finn/locale.conf");
        QDir().mkpath(QFileInfo(conf_path).absolutePath());
        QFile lf(conf_path);
        if (lf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray body =
                QStringLiteral("LANG=%1\nLC_ALL=%1\nLANGUAGE=%2\n")
                    .arg(locale_code, short_loc)
                    .toUtf8();
            lf.write(body);
        }
    }

    void writeCompleteMarkerAndClose() {
        const QString marker =
            QDir::homePath() + QLatin1Char('/') +
            QString::fromLatin1(finn::kOobeCompleteRel);
        QDir().mkpath(QFileInfo(marker).absolutePath());
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("done\n");
        }
        // The headless flow's audit is written as-is. It lists the real
        // capability outcomes and never promotes Unknown/Unavailable to
        // success. The completion marker above is the only boot-visible fact.
        const QString audit_path = QDir::homePath() +
            QStringLiteral("/.local/state/finn/oobe-audit");
        QDir().mkpath(QFileInfo(audit_path).absolutePath());
        QFile af(audit_path);
        if (af.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream out(&af);
            for (const std::string& entry : flow_.audit()) {
                out << QString::fromStdString(entry) << '\n';
            }
            out << "summary|" << QString::fromStdString(flow_.report().summary) << '\n';
        }
        close();
    }

    void setCompleteStatus(const QString& msg, bool is_error) {
        complete_has_error_ = is_error;
        if (!complete_status_lbl_) return;
        const auto& t = FinnThemeManager::instance().tokens();
        QPalette spal = complete_status_lbl_->palette();
        spal.setColor(QPalette::WindowText, is_error ? t.danger : t.dim);
        complete_status_lbl_->setPalette(spal);
        complete_status_lbl_->setText(msg);
        complete_status_lbl_->setVisible(!msg.isEmpty());
    }

    void finishSetup() {
        if (finish_in_progress_) return;
        if (update_mode_) {
            close();
            return;
        }

        const QString full_name =
            fullname_edit_ ? fullname_edit_->text().trimmed() : QString();
        const QString env_user = QString::fromLocal8Bit(qgetenv("USER")).trimmed();
        const QString raw_user =
            username_edit_ ? username_edit_->text().trimmed() : env_user;
        const QString target_user = raw_user.isEmpty() ? QStringLiteral("finn") : raw_user;
        const QString raw_host =
            hostname_edit_ ? hostname_edit_->text().trimmed().toLower() : QString();
        const QString target_host = raw_host.isEmpty() ? QStringLiteral("finnos") : raw_host;
        const QString pw1 = pw_edit_ ? pw_edit_->text() : QString();
        const QString pw2 = pw_verify_edit_ ? pw_verify_edit_->text() : QString();

        if ((!pw1.isEmpty() || !pw2.isEmpty()) && pw1 != pw2) {
            setStep(5);
            if (pw_hint_lbl_) {
                pw_hint_lbl_->setText(QStringLiteral("Passwords don\u2019t match"));
                pw_hint_lbl_->setVisible(true);
            }
            if (pw_verify_edit_) pw_verify_edit_->lineEdit()->setFocus();
            return;
        }
        if (!finn::shell::oobe::is_valid_posix_username(target_user.toStdString())) {
            setStep(5);
            if (pw_hint_lbl_) {
                pw_hint_lbl_->setText(
                    QStringLiteral("Account name must match [a-z_][a-z0-9_-]*"));
                pw_hint_lbl_->setVisible(true);
            }
            if (username_edit_) username_edit_->lineEdit()->setFocus();
            return;
        }
        if (!finn::shell::oobe::is_valid_rfc1123_hostname(target_host.toStdString())) {
            setStep(5);
            if (pw_hint_lbl_) {
                pw_hint_lbl_->setText(
                    QStringLiteral("Computer name must use lowercase letters, digits, and hyphens"));
                pw_hint_lbl_->setVisible(true);
            }
            if (hostname_edit_) hostname_edit_->lineEdit()->setFocus();
            return;
        }

        applyLanguageSelection(selected_language_, selected_locale_);
        FinnThemeManager::instance().setKeyboardLayout(selected_kb_layout_, selected_kb_variant_);

        const QString account_conf =
            QDir::homePath() + QStringLiteral("/.config/finn/account.conf");
        QDir().mkpath(QFileInfo(account_conf).absolutePath());
        {
            QFile af(account_conf);
            if (af.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                const QString body =
                    QStringLiteral("username=%1\nfull_name=%2\nhostname=%3\navatar_color=%4\nlocale=%5\nregion=%6\ntimezone=%7\nkb_layout=%8\nkb_variant=%9\n")
                        .arg(target_user, full_name, target_host,
                             FinnThemeManager::instance().avatarColor(),
                             selected_locale_, selected_region_, selected_timezone_,
                             selected_kb_layout_, selected_kb_variant_);
                af.write(body.toUtf8());
            }
        }

        const QString helper_path = QString::fromLatin1(finn::kOobeApplyHelperPath);
        const bool is_installed_os = QFileInfo::exists(QString::fromLatin1(finn::kInstalledPath));
        const bool system_already_provisioned =
            QFileInfo::exists(QString::fromLatin1(finn::kSystemOobeCompletePath));
        const bool local_only_env = qEnvironmentVariableIntValue("FINN_OOBE_LOCAL_ONLY") == 1;

        if ((!is_installed_os && local_only_env) || system_already_provisioned) {
            writeCompleteMarkerAndClose();
            return;
        }

        // Never use QFileInfo::exists("/etc/sudoers.d/finn-oobe") to decide whether
        // sudo is allowed: /etc/sudoers.d is 0750 root:root on Linux so unprivileged
        // stat() always returns false. Probe sudo -n directly instead.
        auto sudo_can_run_helper = [&helper_path](const QString& sudo_bin) -> bool {
            if (sudo_bin.isEmpty()) return false;
            QProcess probe;
            probe.start(sudo_bin, {QStringLiteral("-n"), QStringLiteral("-l"), helper_path});
            if (!probe.waitForFinished(2500)) {
                probe.kill();
                return false;
            }
            return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
        };

        QString runner_bin;
        QStringList runner_args;
        if (QFileInfo::exists(helper_path)) {
            const QString sudo_bin = QStandardPaths::findExecutable(QStringLiteral("sudo"));
            const QString pkexec_bin = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
            if (::geteuid() == 0) {
                runner_bin = helper_path;
            } else if (sudo_can_run_helper(sudo_bin)) {
                runner_bin = sudo_bin;
                runner_args = {QStringLiteral("-n"), helper_path};
            } else if (!pkexec_bin.isEmpty()) {
                runner_bin = pkexec_bin;
                runner_args = {helper_path};
            }
        }

        if (runner_bin.isEmpty()) {
            if (!is_installed_os) {
                // Uninstalled developer host preview: save local config cleanly.
                writeCompleteMarkerAndClose();
                return;
            }
            setCompleteStatus(
                QStringLiteral("First-boot provisioning helper (%1) is missing or lacks authorization.").arg(helper_path),
                true);
            return;
        }

        const QByteArray stdin_payload =
            QStringLiteral("locale=%1\nregion=%2\ntimezone=%3\nkb_layout=%4\nkb_variant=%5\nhostname=%6\nusername=%7\nfull_name=%8\npassword=%9\ndark=%10\navatar_color=%11\n")
                .arg(selected_locale_, selected_region_, selected_timezone_,
                     selected_kb_layout_, selected_kb_variant_, target_host,
                     target_user, full_name, pw1,
                     FinnThemeManager::instance().isDark() ? QStringLiteral("1") : QStringLiteral("0"),
                     FinnThemeManager::instance().avatarColor())
                .toUtf8();

        finish_in_progress_ = true;
        if (start_btn_) start_btn_->setEnabled(false);
        setCompleteStatus(QStringLiteral("Applying language, timezone, keyboard, hostname, and account..."), false);

        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, proc](int code, QProcess::ExitStatus status) {
                    finish_in_progress_ = false;
                    if (start_btn_) start_btn_->setEnabled(true);
                    const QString err_text = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                    proc->deleteLater();
                    if (status == QProcess::NormalExit && code == 0) {
                        writeCompleteMarkerAndClose();
                    } else {
                        setCompleteStatus(
                            err_text.isEmpty()
                                ? QStringLiteral("Provisioning failed (exit %1). Check your inputs and try again.").arg(code)
                                : QStringLiteral("Provisioning failed: %1").arg(err_text),
                            true);
                    }
                });
        connect(proc, &QProcess::errorOccurred, this,
                [this, proc, runner_bin](QProcess::ProcessError) {
                    finish_in_progress_ = false;
                    if (start_btn_) start_btn_->setEnabled(true);
                    proc->deleteLater();
                    setCompleteStatus(
                        QStringLiteral("Could not execute %1 to apply system settings.").arg(runner_bin),
                        true);
                });
        connect(proc, &QProcess::started, this, [proc, stdin_payload]() {
            proc->write(stdin_payload);
            proc->closeWriteChannel();
        });
        proc->start(runner_bin, runner_args);
    }

    // The headless, honest step machine. It owns no UI and claims no success;
    // every state below is fed from a real probe or a real user action.
    finn::shell::oobe::FullOobeFlow flow_;
    OobeCircleNavButton* shared_next_btn_ = nullptr;
    bool continue_offline_selected_ = false;
    OobeToggleRow* privacy_loc_row_ = nullptr;
    OobeToggleRow* privacy_diag_row_ = nullptr;
    OobeToggleRow* privacy_crash_row_ = nullptr;
    OobeToggleRow* privacy_ack_row_ = nullptr;
    OobeToggleRow* a11y_sr_ = nullptr;
    OobeToggleRow* a11y_hc_ = nullptr;
    OobeToggleRow* a11y_rm_ = nullptr;
    OobeToggleRow* a11y_sk_ = nullptr;
    bool sticky_keys_active_ = false;
    bool sticky_replaying_ = false;
    Qt::KeyboardModifiers sticky_latched_mods_ = Qt::NoModifier;
    finn::shell::oobe::ThemeChoice selected_theme_ =
        finn::shell::oobe::ThemeChoice::Light;

    bool update_mode_ = false;
    double fixed_phase_ = -1.0;
    QString selected_language_ = QStringLiteral("English (UK)");
    QString selected_locale_ = QStringLiteral("en_GB.UTF-8");
    QString selected_region_ = QStringLiteral("GB");
    QString selected_timezone_ = QStringLiteral("UTC");
    QString selected_kb_layout_ = QStringLiteral("us");
    QString selected_kb_variant_;
    FinnPageStack* stack_ = nullptr;
    QWidget* shared_nav_box_ = nullptr;
    QPointer<OobeSlideTransitionOverlay> slide_overlay_;
    QPointer<OobeHiToLangOverlay> hi_lang_overlay_;
    FinnRibbonAnimation* hi_ribbon_ = nullptr;
    OobeBrandingHeader* hi_branding_ = nullptr;
    OobeCircleNavButton* hi_next_btn_ = nullptr;
    QLabel* lang_prompt_lbl_ = nullptr;
    QLabel* region_prompt_lbl_ = nullptr;
    QLabel* kb_prompt_lbl_ = nullptr;
    QLabel* wifi_prompt_lbl_ = nullptr;
    QLabel* account_prompt_lbl_ = nullptr;
    QLabel* appearance_prompt_lbl_ = nullptr;
    QLabel* a11y_prompt_lbl_ = nullptr;
    QLabel* privacy_prompt_lbl_ = nullptr;
    QLabel* privacy_info_lbl_ = nullptr;
    QLabel* complete_icon_lbl_ = nullptr;
    QLabel* complete_title_ = nullptr;
    QLabel* complete_sub_lbl_ = nullptr;
    QLabel* complete_status_lbl_ = nullptr;
    OobeGetStartedPill* start_btn_ = nullptr;
    bool finish_in_progress_ = false;
    bool complete_has_error_ = false;
    bool hostname_manually_edited_ = false;

    QVector<OobeOptionRow*> language_rows_;
    QVector<OobeOptionRow*> region_rows_;
    QVector<OobeOptionRow*> kb_rows_;
    OobeProgressiveList* region_prog_list_ = nullptr;
    ProgressiveBlurOverlay* region_blur_ov_ = nullptr;
    OobeProgressiveList* wifi_prog_list_ = nullptr;
    ProgressiveBlurOverlay* wifi_blur_ov_ = nullptr;

    QLabel* pfp_preview_lbl_ = nullptr;
    QVector<OobePfpSwatch*> pfp_swatches_;
    OobeInputField* fullname_edit_ = nullptr;
    OobeInputField* username_edit_ = nullptr;
    OobeInputField* hostname_edit_ = nullptr;
    OobeInputField* pw_edit_ = nullptr;
    OobeInputField* pw_verify_edit_ = nullptr;
    QLabel* pw_hint_lbl_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    int initial_step = -1;
    bool update_mode = false;
    double fixed_phase = -1.0;
    QString capabilities_path;
    QString screenshot_path;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QLatin1String("--capabilities") && i + 1 < argc) {
            capabilities_path = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--screenshot") && i + 1 < argc) {
            screenshot_path = QString::fromUtf8(argv[++i]);
        } else if (arg == QLatin1String("--step") && i + 1 < argc) {
            bool ok = false;
            const int parsed = QString::fromUtf8(argv[++i]).toInt(&ok);
            if (ok) initial_step = parsed;
        } else if (arg == QLatin1String("--phase") && i + 1 < argc) {
            bool ok = false;
            const double parsed = QString::fromUtf8(argv[++i]).toDouble(&ok);
            if (ok) fixed_phase = parsed;
        } else if (arg == QLatin1String("--update") ||
                   arg == QLatin1String("--update-complete") ||
                   arg == QLatin1String("--boot")) {
            update_mode = true;
        }
    }

    finn::ui::FinnApplication app(argc, argv, QStringLiteral("oobe"));
    bool preflight_ok = false;
    if (capabilities_path.isEmpty()) {
        capabilities_path = QDir::homePath() + QLatin1Char('/') +
            QString::fromLatin1(finn::shell::oobe::probe::kOobeCapabilitiesRel);
    }
    auto measured_caps = finn::shell::oobe::probe::read_preflight(
        capabilities_path, &preflight_ok);
    if (!preflight_ok) measured_caps = finn::shell::oobe::probe::capabilities();
    OobeWindow w(update_mode, fixed_phase >= 0.0 ? fixed_phase : (!screenshot_path.isEmpty() ? 2.5 : -1.0), measured_caps);
    if (initial_step >= 0) w.openStep(initial_step);
    if (screenshot_path.isEmpty()) {
        if (QScreen* scr = QGuiApplication::primaryScreen()) {
            w.setGeometry(scr->geometry());
        }
        w.showFullScreen();
    } else {
        w.resize(1280, 832);
        w.show();
        QTimer::singleShot(120, &w, [&w, &app, screenshot_path]() {
            const QPixmap pm = w.grab();
            pm.save(screenshot_path, "PNG");
            app.quit();
        });
    }
    return app.exec();
}

#include "main.moc"
