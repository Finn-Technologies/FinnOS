//! Desktop top panel, application launcher, and core apps: Terminal, Settings, Files.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::struct_excessive_bools
)]

use crate::canvas::{Canvas, Color, Rect};
use crate::font::text_width;
use crate::ui::{Card, Chip, Divider, ListTile, NavItem, SearchField, Switch};

/// Mini vector folder icon for file table and breadcrumbs.
fn draw_mini_folder_icon(canvas: &mut Canvas, x: i32, y: i32) {
    canvas.fill_rounded_rect(x, y + 2, 14, 10, 2, Color::rgb(245, 158, 11));
    canvas.fill_rounded_rect(x, y, 6, 4, 1, Color::rgb(217, 119, 6));
    canvas.fill_rounded_rect(x, y + 3, 14, 9, 2, Color::rgb(251, 191, 36));
}

/// Mini vector document icon with fold for text/config/doc files.
fn draw_mini_doc_icon(canvas: &mut Canvas, x: i32, y: i32) {
    canvas.fill_rounded_rect(x, y, 12, 14, 2, Color::rgb(241, 245, 249));
    canvas.draw_rounded_rect(x, y, 12, 14, 2, Color::rgb(148, 163, 184));
    canvas.fill_rect(x + 7, y, 5, 5, Color::rgb(203, 213, 225));
    canvas.fill_rect(x + 2, y + 6, 8, 1, Color::rgb(148, 163, 184));
    canvas.fill_rect(x + 2, y + 9, 6, 1, Color::rgb(148, 163, 184));
}

/// Mini vector disk drive icon for storage and volumes.
fn draw_mini_disk_icon(canvas: &mut Canvas, x: i32, y: i32) {
    canvas.fill_rounded_rect(x, y + 1, 14, 12, 2, Color::rgb(99, 102, 241));
    canvas.fill_rect(x + 2, y + 8, 10, 2, Color::rgb(224, 231, 255));
    canvas.fill_circle(x + 10, y + 4, 1, Color::rgb(52, 211, 153));
}

/// Standard height of desktop top status bar (legacy; taskbar replaces it).
pub const PANEL_HEIGHT: u32 = 30;
/// Authentic bottom taskbar height (Figma 71:125, 45px, full-width).
pub const TASKBAR_HEIGHT: u32 = 45;
/// Centered dock size inside the taskbar (Figma 71:448, 170x32).
pub const DOCK_WIDTH: u32 = 170;
/// Dock icon size (crisp 32x32 per Figma).
pub const DOCK_ICON: u32 = 32;

/// Interactive state tracked across all desktop applications, popups, and shell controls.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DesktopState {
    /// Active category in Settings App (0: General, 1: Display, 2: Memory, 3: Security, 4: About).
    pub settings_category: usize,
    /// Settings: Dark appearance toggle.
    pub setting_dark_mode: bool,
    /// Settings: W^X paging protections toggle.
    pub setting_wx_paging: bool,
    /// Settings: Capability sandboxing toggle.
    pub setting_capabilities: bool,
    /// Settings: APIC preemption timer toggle.
    pub setting_preemption: bool,
    /// Settings: Sound effects toggle.
    pub setting_sound: bool,
    /// Settings: Smooth animations toggle.
    pub setting_animations: bool,

    /// Active directory in Files App (0: Home, 1: Documents, 2: Storage, 3: Boot ESP, 4: devfs, 5: data).
    pub files_location: usize,
    /// Selected row index in Files App file list, if any.
    pub files_selected_row: Option<usize>,

    /// Active tab in Terminal App (0: Tab 1, 1: Tab 2).
    pub terminal_tab: usize,
    /// Command history step in Terminal App.
    pub terminal_command_index: usize,

    /// Shell: Whether Start Menu is open.
    pub start_menu_open: bool,
    /// Shell: Whether Control Centre is open.
    pub control_centre_open: bool,
    /// Shell: Whether Power Options modal is open.
    pub power_modal_open: bool,

    /// Control Centre: Wi-Fi radio enabled.
    pub wifi_enabled: bool,
    /// Control Centre: Bluetooth radio enabled.
    pub bluetooth_enabled: bool,
    /// Control Centre: Media playback state (true = playing, false = paused).
    pub media_playing: bool,
    /// Control Centre: Brightness percentage [0, 100].
    pub brightness_pct: u32,
    /// Control Centre: Volume percentage [0, 100].
    pub volume_pct: u32,
    /// Control Centre: Airplane mode enabled.
    pub airplane_mode: bool,
    /// Control Centre: Do Not Disturb / Notification silent mode enabled.
    pub dnd_enabled: bool,
    /// Control Centre: Flashlight enabled.
    pub flashlight_enabled: bool,
    /// Control Centre: Screen Cast enabled.
    pub cast_enabled: bool,

    /// System feedback message for power actions (e.g. "Shutting down...").
    pub system_action_message: Option<&'static str>,
}

impl Default for DesktopState {
    fn default() -> Self {
        Self {
            settings_category: 1, // "Display & GPU" (Hardware Acceleration dashboard)
            setting_dark_mode: false,
            setting_wx_paging: true,
            setting_capabilities: true,
            setting_preemption: true,
            setting_sound: true,
            setting_animations: true,
            files_location: 0,
            files_selected_row: None,
            terminal_tab: 0,
            terminal_command_index: 1,
            start_menu_open: false,
            control_centre_open: false,
            power_modal_open: false,
            wifi_enabled: true,
            bluetooth_enabled: false,
            media_playing: true,
            brightness_pct: 80,
            volume_pct: 65,
            airplane_mode: false,
            dnd_enabled: false,
            flashlight_enabled: false,
            cast_enabled: false,
            system_action_message: None,
        }
    }
}

/// Render the tray clock into a 16-byte buffer as `Mon 18 Oct HH:MM`.
///
/// Figma 71:358 shows `Mon 18 Oct 13:41` (16 chars). Derives a stable readout
/// from the 100 Hz tick counter with integer math only.
const fn tray_clock_bytes(uptime_ticks: u64) -> [u8; 16] {
    let mut buf = *b"Mon 18 Oct 13:41";
    let total_min = 13 * 60 + 41 + (uptime_ticks / 6000) as usize;
    let hh = (total_min / 60) % 24;
    let mm = total_min % 60;
    buf[11] = b'0' + (hh / 10) as u8;
    buf[12] = b'0' + (hh % 10) as u8;
    buf[13] = b':';
    buf[14] = b'0' + (mm / 10) as u8;
    buf[15] = b'0' + (mm % 10) as u8;
    buf
}

/// Format the live tray clock as `Mon 18 Oct 13:41` (Figma 71:358).
pub const fn format_tray_clock(uptime_ticks: u64, out: &mut [u8; 16]) {
    *out = tray_clock_bytes(uptime_ticks);
}

/// Render the authentic full-width bottom 45px frosted taskbar.
///
/// Layout (Figma 71:125): `rgba(0,0,0,0.5)` + 25px blur bar, centered
/// 170x32 dock (Finn/Search/Files/Browser, Figma 71:448-71:462), right tray
/// with Wi-Fi waves + battery gauge (71:127) and Instrument Sans live clock
/// `Mon 18 Oct 13:41` (71:358). `screen_height` anchors the bar at the bottom
/// for 1280x800 and 1024x768 alike.
pub fn render_taskbar(
    canvas: &mut Canvas,
    screen_width: u32,
    screen_height: u32,
    _arch_name: &str,
    uptime_ticks: u64,
) {
    let bar_y = screen_height.saturating_sub(TASKBAR_HEIGHT) as i32;
    // 1. Subtle frosted acrylic tint + top hairline.
    canvas.fill_frosted_rect(
        0,
        bar_y,
        screen_width,
        TASKBAR_HEIGHT,
        0,
        Color::rgba(15, 20, 35, 80),
    );
    for px in 0..screen_width as i32 {
        canvas.set_pixel_coverage(px, bar_y, Color::rgba(255, 255, 255, 25), 90);
    }

    // 2. Centered dock icons with modern floating glass capsule
    let dock_x = ((screen_width.saturating_sub(DOCK_WIDTH)) / 2) as i32;
    let icon_y = bar_y + ((TASKBAR_HEIGHT - DOCK_ICON) / 2) as i32;

    let pill_pad = 14;
    let pill_w = DOCK_WIDTH + pill_pad * 2;
    let pill_x = dock_x - pill_pad as i32;
    let pill_y = bar_y + 3;
    let pill_h = 38;

    // Multi-layer drop shadow under the dock pill
    canvas.draw_soft_shadow(pill_x, pill_y, pill_w, pill_h, 19);

    canvas.fill_rounded_rect(
        pill_x,
        pill_y,
        pill_w,
        pill_h,
        19,
        Color::rgba(20, 24, 38, 210),
    );
    canvas.draw_rounded_rect(
        pill_x,
        pill_y,
        pill_w,
        pill_h,
        19,
        Color::rgba(255, 255, 255, 55),
    );
    // Top specular highlight line
    let hl_start = pill_x + 19;
    let hl_end = pill_x + pill_w as i32 - 19;
    if hl_end > hl_start {
        canvas.draw_aa_line(
            hl_start,
            pill_y + 1,
            hl_end,
            pill_y + 1,
            Color::GLASS_SPECULAR,
        );
    }

    canvas.draw_finn_logo(dock_x, icon_y, DOCK_ICON);
    canvas.draw_search_icon(dock_x + 46, icon_y, DOCK_ICON);
    canvas.draw_files_icon(dock_x + 92, icon_y, DOCK_ICON);
    canvas.draw_browser_icon(dock_x + 138, icon_y, DOCK_ICON);

    // Glowing sky-blue running app indicator dots
    canvas.fill_circle(dock_x + 16, bar_y + 38, 2, Color::rgb(56, 189, 248));
    canvas.fill_circle(dock_x + 92 + 16, bar_y + 38, 2, Color::rgb(56, 189, 248));

    // 3. Right system tray: Battery + Wi-Fi + circular status ring + live clock.
    let clock = tray_clock_bytes(uptime_ticks);
    let clock_str = core::str::from_utf8(&clock).unwrap_or("Mon 18 Oct 13:41");
    let clock_w = text_width(clock_str) + 16;
    let tray_w = 34 + 30 + 30 + clock_w + 24;
    let tx = screen_width.saturating_sub(tray_w) as i32;
    let mid_y = bar_y + (TASKBAR_HEIGHT as i32) / 2;

    // Matching floating glass tray capsule
    canvas.draw_soft_shadow(tx - 8, bar_y + 5, tray_w + 2, TASKBAR_HEIGHT - 10, 16);
    canvas.fill_rounded_rect(
        tx - 8,
        bar_y + 5,
        tray_w + 2,
        TASKBAR_HEIGHT - 10,
        16,
        Color::rgba(20, 24, 38, 210),
    );
    canvas.draw_rounded_rect(
        tx - 8,
        bar_y + 5,
        tray_w + 2,
        TASKBAR_HEIGHT - 10,
        16,
        Color::rgba(255, 255, 255, 55),
    );
    let thl_start = tx - 8 + 16;
    let thl_end = tx - 8 + (tray_w + 2) as i32 - 16;
    if thl_end > thl_start {
        canvas.draw_aa_line(
            thl_start,
            bar_y + 6,
            thl_end,
            bar_y + 6,
            Color::GLASS_SPECULAR,
        );
    }

    // Battery gauge: outline, terminal, green level
    canvas.draw_rounded_rect(tx, mid_y - 5, 20, 10, 2, Color::WHITE);
    canvas.fill_rect(tx + 20, mid_y - 2, 2, 4, Color::WHITE);
    canvas.fill_rounded_rect(tx + 2, mid_y - 3, 15, 6, 1, Color::BATTERY_GREEN);

    canvas.draw_wifi_icon(tx + 30, mid_y - 9, true);
    canvas.draw_status_ring(tx + 60 + 8, mid_y, 8, 85);
    canvas.draw_string(tx + 90, mid_y - 8, clock_str, Color::WHITE, None);
}

/// Render the modern desktop top menu bar (legacy alias).
///
/// Preserved for the kernel interactive loop; forwards to the authentic
/// bottom [`render_taskbar`] so existing callers keep compiling while the
/// desktop shows the Figma bottom bar. `screen_width` maps to a 1280x800 or
/// 1024x768 frame; height is inferred from the canvas.
pub fn render_top_panel(
    canvas: &mut Canvas,
    screen_width: u32,
    arch_name: &str,
    uptime_ticks: u64,
) {
    let h = canvas.height() as u32;
    render_taskbar(canvas, screen_width, h, arch_name, uptime_ticks);
}

/// Render the modern dark acrylic Terminal application.
#[allow(clippy::too_many_lines)]
pub fn render_terminal_app(canvas: &mut Canvas, client: Rect) {
    // 1. Tab Bar at top of terminal (body already filled with rounded corners by Window::render)
    let tab_h = 24u32;
    canvas.fill_rect(
        client.x,
        client.y,
        client.width,
        tab_h,
        Color::TERMINAL_HEADER,
    );
    canvas.fill_rect(
        client.x,
        client.y + tab_h as i32 - 1,
        client.width,
        1,
        Color::rgb(49, 50, 68),
    );

    // Active tab
    let tab_label = ">_ bash (finnos)";
    let tab_w = (Canvas::string_width(tab_label) + 34).min(client.width.saturating_sub(16));
    canvas.fill_rounded_rect(client.x + 8, client.y + 3, tab_w, 18, 5, Color::TERMINAL_BG);
    canvas.fill_circle(client.x + 18, client.y + 12, 3, Color::TERMINAL_GREEN);
    canvas.draw_string_max(
        client.x + 28,
        client.y + 2,
        tab_label,
        Color::TERMINAL_TEXT,
        None,
        tab_w.saturating_sub(24),
    );

    // Inactive second tab + new tab '+' button
    let tab2_x = client.x + 8 + tab_w as i32 + 4;
    if tab2_x + 60 < client.x + client.width as i32 - 20 {
        canvas.fill_rounded_rect(
            tab2_x,
            client.y + 3,
            56,
            18,
            4,
            Color::TERMINAL_TAB_INACTIVE,
        );
        canvas.draw_string(
            tab2_x + 10,
            client.y + 2,
            "htop",
            Color::rgb(147, 153, 178),
            None,
        );
        canvas.draw_string(
            tab2_x + 64,
            client.y + 2,
            "+",
            Color::rgb(147, 153, 178),
            None,
        );
    }

    // 3. Terminal Client Text Body (ASCII only; 20px pitch; clipped to fit).
    let mut cy = client.y + tab_h as i32 + 6;
    let cx = client.x + 12;
    let body_w = client.width.saturating_sub(24);

    // (style, line)
    let lines = [
        ("INFO", "FinnOS Developer Terminal v0.1.0"),
        ("INFO", "GPU Acceleration: VirtIO-GPU 2D/3D active"),
        ("PROMPT", "finnos@host ~/system (main)"),
        ("CMD", "$ ps -a"),
        ("HEADER", "PID  NAME        STATE  MEM      CPU"),
        ("RUN", "1    init        RUN    128 KiB  0.1%"),
        ("RUN", "2    compositor  RUN    512 KiB  1.2%"),
        ("ROW", "3    sh          IDLE    64 KiB  0.0%"),
        ("ROW", "4    settings    READY  192 KiB  0.1%"),
        ("ROW", "5    files       READY  192 KiB  0.1%"),
        ("PROMPT", "finnos@host ~/system (main)"),
        ("CURSOR", "$"),
    ];

    let max_y = client.y + client.height as i32 - 24;

    for (tag, line) in lines {
        if cy + crate::font::TEXT_LINE as i32 >= max_y {
            break;
        }
        match tag {
            "PROMPT" => {
                canvas.draw_string_strong(cx, cy, "finnos@host", Color::TERMINAL_GREEN, None);
                let u_w = Canvas::string_width("finnos@host");
                canvas.draw_string(cx + u_w as i32, cy, ":", Color::WHITE, None);
                let col_w = Canvas::string_width(":");
                canvas.draw_string_strong(
                    cx + u_w as i32 + col_w as i32,
                    cy,
                    "~/system",
                    Color::rgb(137, 180, 250),
                    None,
                );
                let sys_w = Canvas::string_width("~/system");
                canvas.draw_string(
                    cx + u_w as i32 + col_w as i32 + sys_w as i32 + 4,
                    cy,
                    "(main)",
                    Color::rgb(148, 226, 213),
                    None,
                );
            }
            "CMD" => {
                canvas.draw_string_strong(cx, cy, "$", Color::TERMINAL_PROMPT, None);
                let dollar = Canvas::string_width("$ ");
                canvas.draw_string_max(
                    cx + dollar as i32,
                    cy,
                    line.strip_prefix("$ ").unwrap_or(line),
                    Color::WHITE,
                    None,
                    body_w.saturating_sub(dollar),
                );
            }
            "HEADER" => {
                canvas.draw_string_max(cx, cy, line, Color::rgb(147, 153, 178), None, body_w);
            }
            "RUN" => canvas.draw_string_max(cx, cy, line, Color::TERMINAL_GREEN, None, body_w),
            "ROW" => canvas.draw_string_max(cx, cy, line, Color::TERMINAL_TEXT, None, body_w),
            "CURSOR" => {
                canvas.draw_string_strong(cx, cy, "$", Color::TERMINAL_PROMPT, None);
                let dollar = Canvas::string_width("$ ");
                canvas.fill_rect(cx + dollar as i32, cy + 2, 8, 14, Color::TERMINAL_PROMPT);
            }
            _ => canvas.draw_string_max(cx, cy, line, Color::rgb(148, 163, 184), None, body_w),
        }
        cy += crate::font::TEXT_LINE as i32 - 1;
    }

    // 4. Terminal Bottom Status Line with chips
    let status_y = client.y + client.height as i32 - 20;
    canvas.fill_rounded_rect(
        client.x,
        status_y - 2,
        client.width,
        22,
        10,
        Color::TERMINAL_HEADER,
    );
    canvas.fill_rect(client.x, status_y, client.width, 1, Color::rgb(49, 50, 68));

    let mut chip_x = client.x + 8;
    let chips = [
        ("UTF-8", Color::rgb(49, 50, 68), Color::rgb(205, 214, 244)),
        ("GPU HW", Color::rgb(22, 101, 52), Color::rgb(52, 211, 153)),
        (
            "W^X Guarded",
            Color::rgb(30, 41, 59),
            Color::rgb(52, 211, 153),
        ),
        ("IPC v3", Color::rgb(30, 41, 59), Color::rgb(147, 197, 253)),
    ];
    for (label, bg, fg) in chips {
        if chip_x + Canvas::string_width(label) as i32 + 20 >= client.x + client.width as i32 {
            break;
        }
        let chip = Chip::new(label, bg, fg);
        let cw = chip.paint(canvas, chip_x, status_y + 1);
        chip_x += cw as i32 + 6;
    }
}

/// Render the authentic two-tone System Settings application with interactive state.
///
/// Figma 140:335 (`750x500`): left `250x500` frosted sidebar
/// (`rgba(255,255,255,0.5)` + blur) with monochrome line controls, 20px
/// "Settings" heading, 32px search pill, 44px user profile card (blue avatar,
/// user name, "`FinnOS` Local Account"); right `500x500` pure white card with
/// 20px outer radius, "Device Info" heading, `#E5E7EB` info cards. Scales to
/// smaller windows by ratio (sidebar = width/3 clamped to 250).
#[allow(clippy::too_many_lines)]
pub fn render_settings_app_state(
    canvas: &mut Canvas,
    client: Rect,
    arch_name: &str,
    state: &DesktopState,
) {
    // 1. Frosted left sidebar (1/3 width, max 250 like Figma) with rounded bottom-left corner.
    // Note: client area right content background is already drawn with rounded corners by Window::render.
    let sidebar_w = (client.width / 3).clamp(165, 250);
    canvas.fill_rect_rounded_bottom_left(
        client.x,
        client.y,
        sidebar_w,
        client.height,
        10,
        Color::rgb(17, 17, 27),
    );
    // Divider.
    canvas.fill_rect(
        client.x + sidebar_w as i32,
        client.y,
        1,
        client.height,
        Color::rgb(49, 50, 68),
    );

    // 3. Sidebar header: 20px "Settings" title.
    canvas.draw_title_strong(
        client.x + 10,
        client.y + 8,
        "Settings",
        Color::rgb(205, 214, 244),
    );

    // 4. Flutter-style Search capsule.
    let search_y = client.y + 40;
    let search_w = sidebar_w.saturating_sub(20);
    if search_y + 30 <= client.y + client.height as i32 {
        SearchField::new(Rect::new(client.x + 10, search_y, search_w, 30), "Search").paint(canvas);
    }

    // 5. User profile card (44px): blue avatar + user + local account.
    let profile_y = search_y + 36;
    if profile_y + 44 <= client.y + client.height as i32 {
        Card::new(Rect::new(client.x + 10, profile_y, search_w, 44)).paint(canvas);
        canvas.fill_circle(client.x + 28, profile_y + 22, 12, Color::AVATAR_BLUE);
        canvas.fill_circle(client.x + 28, profile_y + 17, 5, Color::WHITE);
        canvas.fill_rounded_rect(client.x + 20, profile_y + 24, 16, 10, 5, Color::WHITE);
        let name_w = search_w.saturating_sub(38);
        canvas.draw_string_strong(
            client.x + 46,
            profile_y + 4,
            "finnos",
            Color::rgb(205, 214, 244),
            None,
        );
        canvas.draw_string_max(
            client.x + 46,
            profile_y + 24,
            "Local Account",
            Color::rgb(147, 153, 178),
            None,
            name_w,
        );
    }

    // 6. Sidebar nav (below profile) using Flutter NavItem.
    let categories = ["General", "Display", "Memory", "Security", "About"];
    let mut side_y = profile_y + 50;
    for (idx, name) in categories.iter().enumerate() {
        if side_y + 22 > client.y + client.height as i32 {
            break;
        }
        let btn_rect = Rect::new(client.x + 10, side_y, search_w, 22);
        let active = state.settings_category == idx;
        NavItem::new(name, active).paint(canvas, btn_rect);
        side_y += 26;
    }

    // 7. Right content pane.
    let content_x = client.x + sidebar_w as i32 + 12;
    let content_w = client.width.saturating_sub(sidebar_w + 24);
    if content_w < 80 {
        return;
    }
    let mut card_y = client.y + 12;

    if state.settings_category == 3 {
        // Security View
        canvas.draw_title_strong(
            content_x,
            card_y,
            "Security & Sandboxing",
            Color::rgb(205, 214, 244),
        );
        card_y += 30;

        let card2_h = 110u32;
        if card_y + card2_h as i32 <= client.y + client.height as i32 - 8 {
            Card::new(Rect::new(content_x, card_y, content_w, card2_h)).paint(canvas);
            let labels = [
                ("W^X Paging Protections", state.setting_wx_paging),
                ("Capability Sandboxing", state.setting_capabilities),
                ("APIC Preemption Timer", state.setting_preemption),
            ];
            let mut ty = card_y + 10;
            for (lbl, on) in labels {
                ListTile::new(lbl).render(
                    canvas,
                    Rect::new(content_x + 12, ty, content_w.saturating_sub(60), 22),
                );
                let sw_x = content_x + content_w as i32 - 46;
                Switch::new(Rect::new(sw_x, ty, 34, 18), on).paint(canvas);
                ty += 32;
            }
        }
    } else if state.settings_category == 1 {
        // Display & GPU Acceleration View
        canvas.draw_title_strong(
            content_x,
            card_y,
            "Display & GPU",
            Color::rgb(205, 214, 244),
        );
        card_y += 28;

        // Card 1: GPU Hardware Acceleration
        let card1_h = 104u32;
        if card_y + card1_h as i32 <= client.y + client.height as i32 - 8 {
            Card::new(Rect::new(content_x, card_y, content_w, card1_h)).paint(canvas);
            canvas.draw_string_strong(
                content_x + 12,
                card_y + 8,
                "GPU Acceleration",
                Color::rgb(205, 214, 244),
                None,
            );
            Chip::new(
                "HW Active",
                Color::rgb(20, 83, 45),
                Color::rgb(134, 239, 172),
            )
            .paint(canvas, content_x + content_w as i32 - 88, card_y + 6);
            Divider::new(Color::rgb(49, 50, 68)).paint(
                canvas,
                content_x + 12,
                card_y + 26,
                content_w.saturating_sub(24),
            );

            let gpu_items = [
                ("Device", "VirtIO-GPU v1.2 (PCI 0x1050)"),
                ("Pipeline", "Double-Buffered (VSync)"),
                ("Cursor", "Dedicated HW Overlay"),
            ];
            let mut gy = card_y + 32;
            for (k, v) in gpu_items {
                canvas.draw_string(content_x + 12, gy, k, Color::rgb(147, 153, 178), None);
                canvas.draw_string_max(
                    content_x + 80,
                    gy,
                    v,
                    Color::rgb(245, 245, 247),
                    None,
                    content_w.saturating_sub(88),
                );
                gy += 22;
            }
            card_y += card1_h as i32 + 8;
        }

        // Card 2: Appearance & Effects
        let card2_h = 90u32;
        if card_y + card2_h as i32 <= client.y + client.height as i32 - 8 {
            Card::new(Rect::new(content_x, card_y, content_w, card2_h)).paint(canvas);
            let labels = [
                ("Dark Appearance", true),
                ("Window Blur & Transparency", true),
            ];
            let mut ty = card_y + 12;
            for (lbl, on) in labels {
                ListTile::new(lbl).render(
                    canvas,
                    Rect::new(content_x + 12, ty, content_w.saturating_sub(60), 22),
                );
                let sw_x = content_x + content_w as i32 - 46;
                Switch::new(Rect::new(sw_x, ty, 34, 18), on).paint(canvas);
                ty += 36;
            }
        }
    } else {
        // Default / About / Device Info View (Figma 140:336)
        canvas.draw_title_strong(content_x, card_y, "Device Info", Color::rgb(205, 214, 244));
        card_y += 30;

        // Card 1: System Overview
        let card1_h = 108u32;
        if card_y + card1_h as i32 <= client.y + client.height as i32 - 8 {
            Card::new(Rect::new(content_x, card_y, content_w, card1_h)).paint(canvas);
            canvas.draw_string_strong(
                content_x + 12,
                card_y + 8,
                "System Overview",
                Color::rgb(205, 214, 244),
                None,
            );
            Divider::new(Color::rgb(49, 50, 68)).paint(
                canvas,
                content_x + 12,
                card_y + 26,
                content_w.saturating_sub(24),
            );

            canvas.draw_string(
                content_x + 12,
                card_y + 34,
                "OS Version",
                Color::rgb(147, 153, 178),
                None,
            );
            canvas.draw_string_strong(
                content_x + 12,
                card_y + 48,
                "FinnOS 0.1.0",
                Color::rgb(205, 214, 244),
                None,
            );
            Chip::new("Preview", Color::rgb(20, 83, 45), Color::rgb(134, 239, 172)).paint(
                canvas,
                content_x + 110,
                card_y + 46,
            );

            canvas.draw_string(
                content_x + 12,
                card_y + 68,
                "Platform",
                Color::rgb(147, 153, 178),
                None,
            );
            canvas.draw_string_strong(
                content_x + 110,
                card_y + 68,
                arch_name,
                Color::rgb(205, 214, 244),
                None,
            );

            canvas.draw_string(
                content_x + 12,
                card_y + 88,
                "GPU Engine",
                Color::rgb(147, 153, 178),
                None,
            );
            canvas.draw_string_strong(
                content_x + 110,
                card_y + 88,
                "VirtIO-GPU HW 2D/3D",
                Color::rgb(205, 214, 244),
                None,
            );
            card_y += card1_h as i32 + 8;
        }

        // Card 2: Security Controls
        let card2_h = 104u32;
        if card_y + card2_h as i32 <= client.y + client.height as i32 - 8 {
            Card::new(Rect::new(content_x, card_y, content_w, card2_h)).paint(canvas);
            canvas.draw_string_strong(
                content_x + 12,
                card_y + 8,
                "Security Controls",
                Color::rgb(205, 214, 244),
                None,
            );
            Divider::new(Color::rgb(49, 50, 68)).paint(
                canvas,
                content_x + 12,
                card_y + 26,
                content_w.saturating_sub(24),
            );

            let toggles = [
                ("W^X Paging Guards", state.setting_wx_paging),
                ("Capability Sandboxing", state.setting_capabilities),
                ("APIC Preemption Timer", state.setting_preemption),
            ];
            let sw_x = content_x + content_w as i32 - 46;
            let mut ty = card_y + 32;
            for (lbl, on) in toggles {
                canvas.draw_string(content_x + 12, ty + 1, lbl, Color::rgb(205, 214, 244), None);
                Switch::new(Rect::new(sw_x, ty, 34, 16), on).paint(canvas);
                ty += 22;
            }
        }
    }
}

/// Render the authentic two-tone System Settings application (legacy wrapper).
pub fn render_settings_app(canvas: &mut Canvas, client: Rect, arch_name: &str) {
    render_settings_app_state(canvas, client, arch_name, &DesktopState::default());
}

/// Render the modern 2-column Files application with interactive state.
#[allow(clippy::too_many_lines)]
pub fn render_files_app_state(canvas: &mut Canvas, client: Rect, state: &DesktopState) {
    // 1. Left Sidebar (Favorites & Devices) with rounded bottom-left corner
    let sidebar_w = 124u32;
    canvas.fill_rect_rounded_bottom_left(
        client.x,
        client.y,
        sidebar_w,
        client.height,
        10,
        Color::rgb(17, 17, 27),
    );
    canvas.fill_rect(
        client.x + sidebar_w as i32,
        client.y,
        1,
        client.height,
        Color::rgb(49, 50, 68),
    );

    let mut side_y = client.y + 10;
    canvas.draw_string(
        client.x + 8,
        side_y,
        "FAVORITES",
        Color::rgb(108, 112, 134),
        None,
    );
    side_y += 18;

    let favorites = ["Home (/)", "Documents", "Storage"];
    for (idx, fav) in favorites.iter().enumerate() {
        let pill_rect = Rect::new(client.x + 6, side_y, sidebar_w - 12, 22);
        let active = state.files_location == idx;
        if active {
            canvas.fill_rounded_rect(
                pill_rect.x,
                pill_rect.y,
                pill_rect.width,
                pill_rect.height,
                8,
                Color::rgb(59, 130, 246),
            );
            canvas.draw_string_strong(pill_rect.x + 10, pill_rect.y + 3, fav, Color::WHITE, None);
        } else {
            canvas.draw_string(
                pill_rect.x + 10,
                pill_rect.y + 3,
                fav,
                Color::rgb(166, 173, 200),
                None,
            );
        }
        side_y += 24;
    }

    side_y += 6;
    canvas.draw_string(
        client.x + 8,
        side_y,
        "DEVICES",
        Color::rgb(108, 112, 134),
        None,
    );
    side_y += 18;

    let devices = [("Boot ESP", 3), ("data", 5)];
    for (dev, dev_idx) in devices {
        let pill_rect = Rect::new(client.x + 6, side_y, sidebar_w - 12, 22);
        let active = state.files_location == dev_idx;
        if active {
            canvas.fill_rounded_rect(
                pill_rect.x,
                pill_rect.y,
                pill_rect.width,
                pill_rect.height,
                8,
                Color::rgb(59, 130, 246),
            );
            canvas.draw_string_strong(pill_rect.x + 10, pill_rect.y + 3, dev, Color::WHITE, None);
        } else {
            canvas.draw_string(
                pill_rect.x + 10,
                pill_rect.y + 3,
                dev,
                Color::rgb(166, 173, 200),
                None,
            );
        }
        side_y += 24;
    }

    // Storage capacity meter at bottom of sidebar (inset from bottom-left corner)
    if client.height >= 200 {
        let meter_y = client.y + client.height as i32 - 42;
        canvas.draw_string(
            client.x + 10,
            meter_y,
            "Storage (52%)",
            Color::rgb(166, 173, 200),
            None,
        );
        let bar_w = sidebar_w - 20;
        canvas.fill_rounded_rect(
            client.x + 10,
            meter_y + 22,
            bar_w,
            5,
            2,
            Color::rgb(49, 50, 68),
        );
        let used_w = (bar_w * 52) / 100;
        canvas.fill_rounded_rect(
            client.x + 10,
            meter_y + 22,
            used_w,
            5,
            2,
            Color::rgb(59, 130, 246),
        );
    }

    // 3. Right Content Area: Toolbar & File Table
    let content_x = client.x + sidebar_w as i32 + 10;
    let content_w = client.width.saturating_sub(sidebar_w + 20);

    // Top Navigation Toolbar
    let toolbar_y = client.y + 8;
    canvas.fill_rounded_rect(content_x, toolbar_y, 24, 22, 5, Color::rgb(30, 30, 46));
    canvas.draw_rounded_rect(content_x, toolbar_y, 24, 22, 5, Color::rgb(49, 50, 68));
    canvas.draw_string(
        content_x + 8,
        toolbar_y + 3,
        "<",
        Color::rgb(147, 153, 178),
        None,
    );

    canvas.fill_rounded_rect(content_x + 28, toolbar_y, 24, 22, 5, Color::rgb(30, 30, 46));
    canvas.draw_rounded_rect(content_x + 28, toolbar_y, 24, 22, 5, Color::rgb(49, 50, 68));
    canvas.draw_string(
        content_x + 36,
        toolbar_y + 3,
        ">",
        Color::rgb(147, 153, 178),
        None,
    );

    // Location breadcrumb pill
    let crumb_text = match state.files_location {
        1 => "root > documents",
        2 => "root > storage",
        3 => "root > boot > esp",
        4 => "root > dev",
        5 => "root > data > state",
        _ => "root > home",
    };
    let crumb_w = content_w.saturating_sub(62).clamp(80, 220);
    canvas.fill_rounded_rect(
        content_x + 58,
        toolbar_y,
        crumb_w,
        22,
        11,
        Color::rgb(30, 30, 46),
    );
    canvas.draw_rounded_rect(
        content_x + 58,
        toolbar_y,
        crumb_w,
        22,
        11,
        Color::rgb(49, 50, 68),
    );
    draw_mini_folder_icon(canvas, content_x + 64, toolbar_y + 5);
    canvas.draw_string_max(
        content_x + 82,
        toolbar_y + 3,
        crumb_text,
        Color::rgb(205, 214, 244),
        None,
        crumb_w.saturating_sub(28),
    );

    // File Table Headers
    let header_y = toolbar_y + 30;
    canvas.fill_rect(content_x, header_y, content_w, 20, Color::rgb(17, 17, 27));
    canvas.fill_rect(
        content_x,
        header_y + 19,
        content_w,
        1,
        Color::rgb(49, 50, 68),
    );
    canvas.draw_string(
        content_x + 24,
        header_y + 2,
        "NAME",
        Color::rgb(108, 112, 134),
        None,
    );
    canvas.draw_string(
        content_x + 120,
        header_y + 2,
        "TYPE",
        Color::rgb(108, 112, 134),
        None,
    );
    canvas.draw_string(
        content_x + 190,
        header_y + 2,
        "SIZE",
        Color::rgb(108, 112, 134),
        None,
    );
    canvas.draw_string(
        content_x + 250,
        header_y + 2,
        "STATUS",
        Color::rgb(108, 112, 134),
        None,
    );

    // File Rows
    let rows = [
        ("dev/", "Folder", "-", "devfs", 0),
        ("data/", "Storage", "16 MB", "virtio", 1),
        ("system.log", "Text File", "4 KiB", "Active", 2),
        ("config.toml", "Settings", "1 KiB", "Read-only", 3),
        ("readme.txt", "Doc", "2 KiB", "Verified", 4),
    ];

    let mut row_y = header_y + 24;
    for (row_idx, (name, ftype, size, status, icon_type)) in rows.iter().enumerate() {
        if row_y + crate::font::TEXT_LINE as i32 >= client.y + client.height as i32 - 6 {
            break;
        }
        let is_selected = state.files_selected_row == Some(row_idx);
        if is_selected {
            canvas.fill_rounded_rect(
                content_x + 2,
                row_y - 2,
                content_w.saturating_sub(4),
                20,
                4,
                Color::rgb(49, 50, 68),
            );
        } else if row_idx % 2 == 1 {
            canvas.fill_rect(
                content_x + 2,
                row_y - 2,
                content_w.saturating_sub(4),
                20,
                Color::rgb(30, 30, 46),
            );
        }

        match icon_type {
            0 => draw_mini_folder_icon(canvas, content_x + 4, row_y + 1),
            1 => draw_mini_disk_icon(canvas, content_x + 4, row_y + 1),
            _ => draw_mini_doc_icon(canvas, content_x + 5, row_y),
        }

        let is_dir = name.ends_with('/');
        let name_color = if is_dir {
            Color::rgb(137, 180, 250)
        } else {
            Color::rgb(205, 214, 244)
        };
        canvas.draw_string_max(content_x + 24, row_y, name, name_color, None, 92);
        canvas.draw_string_max(
            content_x + 120,
            row_y,
            ftype,
            Color::rgb(147, 153, 178),
            None,
            64,
        );
        canvas.draw_string_max(
            content_x + 190,
            row_y,
            size,
            Color::rgb(147, 153, 178),
            None,
            54,
        );

        let (bg, fg) = match *status {
            "Active" | "Verified" => (Color::rgb(20, 83, 45), Color::rgb(134, 239, 172)),
            "virtio" => (Color::rgb(59, 7, 100), Color::rgb(216, 180, 254)),
            "devfs" => (Color::rgb(8, 51, 68), Color::rgb(103, 232, 249)),
            "Read-only" => (Color::rgb(69, 26, 3), Color::rgb(253, 186, 116)),
            _ => (Color::rgb(30, 30, 46), Color::rgb(147, 153, 178)),
        };
        Chip::new(status, bg, fg).paint(canvas, content_x + 250, row_y - 2);

        row_y += crate::font::TEXT_LINE as i32 + 1;
    }
}

/// Render the modern 2-column Files application (legacy wrapper).
pub fn render_files_app(canvas: &mut Canvas, client: Rect) {
    render_files_app_state(canvas, client, &DesktopState::default());
}

/// Size of the centered Start Menu card (Figma 71:438, 500x500, rx 24).
pub const START_MENU_SIZE: u32 = 500;
/// Corner radius of the Start Menu dark acrylic card.
pub const START_MENU_RADIUS: u32 = 24;

/// Render the centered floating Start Menu with interactive state.
#[allow(clippy::too_many_lines)]
pub fn render_start_menu_state(
    canvas: &mut Canvas,
    screen_width: u32,
    screen_height: u32,
    _state: &DesktopState,
) {
    let size = START_MENU_SIZE
        .min(screen_width)
        .min(screen_height.saturating_sub(TASKBAR_HEIGHT + 16));
    let mx = ((screen_width.saturating_sub(size)) / 2) as i32;
    let my = ((screen_height
        .saturating_sub(TASKBAR_HEIGHT)
        .saturating_sub(size))
        / 2) as i32;
    canvas.draw_soft_shadow(mx, my, size, size, START_MENU_RADIUS);
    canvas.fill_frosted_rect(mx, my, size, size, START_MENU_RADIUS, Color::START_MENU_BG);
    canvas.draw_rounded_rect(
        mx,
        my,
        size,
        size,
        START_MENU_RADIUS,
        Color::rgba(255, 255, 255, 60),
    );
    // 1. Avatar + greeting.
    let ax = mx + 28;
    let ay = my + 24;
    canvas.fill_circle(ax + 20, ay + 20, 20, Color::AVATAR_BLUE);
    canvas.fill_circle(ax + 20, ay + 14, 8, Color::WHITE);
    canvas.fill_rounded_rect(ax + 10, ay + 24, 20, 12, 6, Color::WHITE);
    canvas.draw_title_strong(ax + 52, ay + 6, "Hi, finnos!", Color::WHITE);
    canvas.draw_string(
        ax + 52,
        ay + 26,
        "FinnOS Local Account",
        Color::CHROME_LINE_LIGHT,
        None,
    );

    // Top Right Settings and Power circle buttons (Figma 71:438)
    let s_btn_x = mx + size as i32 - 76;
    let p_btn_x = mx + size as i32 - 38;
    canvas.fill_circle(s_btn_x, ay + 18, 16, Color::rgba(255, 255, 255, 50));
    canvas.draw_settings_gear_tinted(s_btn_x - 8, ay + 10, 16, Color::WHITE);
    canvas.fill_circle(p_btn_x, ay + 18, 16, Color::rgba(255, 255, 255, 50));
    canvas.draw_power_glyph(p_btn_x, ay + 18, 7, Color::WHITE);

    // 2. Search bar capsule
    let s_w = size.saturating_sub(56);
    let search_y = my + 76;
    canvas.fill_rounded_rect(
        mx + 28,
        search_y,
        s_w,
        32,
        16,
        Color::rgba(255, 255, 255, 25),
    );
    canvas.draw_rounded_rect(
        mx + 28,
        search_y,
        s_w,
        32,
        16,
        Color::rgba(255, 255, 255, 45),
    );
    canvas.draw_circle(mx + 44, search_y + 15, 4, Color::CHROME_LINE_LIGHT);
    canvas.draw_aa_line(
        mx + 47,
        search_y + 18,
        mx + 50,
        search_y + 21,
        Color::CHROME_LINE_LIGHT,
    );
    canvas.draw_string(
        mx + 58,
        search_y + 8,
        "Search apps, files and settings...",
        Color::CHROME_LINE_LIGHT,
        None,
    );

    // 3. Pinned Applications: 2 rows x 4 columns = 8 items
    let cell_w = s_w / 4;
    let grid_y1 = search_y + 44;
    let row1 = [
        ("Terminal", 0),
        ("Files", 1),
        ("Browser", 2),
        ("Settings", 3),
    ];
    for (i, (label, app_idx)) in row1.iter().enumerate() {
        let cx = mx + 28 + (i as i32) * cell_w as i32 + (cell_w as i32 - 48) / 2;
        let ix = cx + 8;
        let iy = grid_y1 + 8;
        match app_idx {
            0 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_TERMINAL),
            1 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_FILES),
            2 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_BROWSER),
            _ => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_SETTINGS),
        }
        let lw = text_width(label);
        canvas.draw_string(
            cx + (48u32.saturating_sub(lw) / 2) as i32,
            grid_y1 + 52,
            label,
            Color::WHITE,
            None,
        );
    }

    let grid_y2 = grid_y1 + 76;
    let row2 = [
        ("Notes", 4),
        ("Clock", 5),
        ("Weather", 6),
        ("Calculator", 7),
    ];
    for (i, (label, app_idx)) in row2.iter().enumerate() {
        let cx = mx + 28 + (i as i32) * cell_w as i32 + (cell_w as i32 - 48) / 2;
        let ix = cx + 8;
        let iy = grid_y2 + 8;
        match app_idx {
            4 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_NOTES),
            5 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_CLOCK),
            6 => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_WEATHER),
            _ => canvas.draw_rgba_bitmap(ix, iy, 32, 32, &crate::icons::APP_CALCULATOR),
        }
        let lw = text_width(label);
        canvas.draw_string(
            cx + (48u32.saturating_sub(lw) / 2) as i32,
            grid_y2 + 52,
            label,
            Color::WHITE,
            None,
        );
    }

    // 4. Recent Documents Card (eliminates the empty space)
    let recents_y = grid_y2 + 76;
    if recents_y + 110 <= my + size as i32 {
        canvas.draw_string_strong(mx + 30, recents_y, "Recent Documents", Color::WHITE, None);
        let recents_card_y = recents_y + 20;
        let card_h = (my + size as i32 - 14 - recents_card_y).min(96) as u32;
        canvas.fill_rounded_rect(
            mx + 28,
            recents_card_y,
            s_w,
            card_h,
            12,
            Color::rgba(255, 255, 255, 18),
        );
        canvas.draw_rounded_rect(
            mx + 28,
            recents_card_y,
            s_w,
            card_h,
            12,
            Color::rgba(255, 255, 255, 30),
        );

        let recent_items = [
            ("system.log", "System Log", "4 KiB", 2),
            ("config.toml", "Settings", "1 KiB", 3),
            ("readme.txt", "Release Notes", "2 KiB", 4),
        ];

        let mut item_y = recents_card_y + 8;
        for (name, desc, sz, icon_t) in recent_items {
            if item_y + 20 > recents_card_y + card_h as i32 {
                break;
            }
            match icon_t {
                2 | 4 => draw_mini_doc_icon(canvas, mx + 38, item_y + 2),
                _ => draw_mini_folder_icon(canvas, mx + 38, item_y + 2),
            }
            canvas.draw_string(mx + 56, item_y + 2, name, Color::WHITE, None);
            canvas.draw_string(mx + 160, item_y + 2, desc, Color::CHROME_LINE_LIGHT, None);
            canvas.draw_string(mx + 270, item_y + 2, sz, Color::CHROME_LINE_LIGHT, None);
            item_y += 24;
        }
    }
}

/// Render the centered floating Start Menu (legacy wrapper).
pub fn render_start_menu(canvas: &mut Canvas, screen_width: u32, screen_height: u32) {
    render_start_menu_state(
        canvas,
        screen_width,
        screen_height,
        &DesktopState::default(),
    );
}

/// Render the bottom-right Control Centre quick-settings popup with interactive state.
#[allow(clippy::too_many_lines)]
pub fn render_control_centre_state(
    canvas: &mut Canvas,
    screen_width: u32,
    screen_height: u32,
    state: &DesktopState,
) {
    let (pw, ph) = (261u32, 302u32);
    let px = screen_width.saturating_sub(pw + 12) as i32;
    let py = screen_height.saturating_sub(TASKBAR_HEIGHT + ph + 12) as i32;
    canvas.draw_soft_shadow(px, py, pw, ph, 18);
    canvas.fill_frosted_rect(px, py, pw, ph, 18, Color::CONTROL_CENTRE_BG);
    canvas.draw_rounded_rect(px, py, pw, ph, 18, Color::rgba(255, 255, 255, 60));

    // Pill toggles: Wi-Fi, Bluetooth.
    let wifi_col = if state.wifi_enabled {
        Color::ACCENT_BLUE
    } else {
        Color::rgba(255, 255, 255, 28)
    };
    canvas.fill_rounded_rect(px + 12, py + 12, 112, 56, 14, wifi_col);
    canvas.fill_circle(
        px + 32,
        py + 40,
        14,
        if state.wifi_enabled {
            Color::WHITE
        } else {
            Color::rgba(255, 255, 255, 40)
        },
    );
    canvas.draw_wifi_icon(px + 24, py + 33, !state.wifi_enabled);
    canvas.draw_string(px + 52, py + 22, "Wi-Fi", Color::WHITE, None);
    canvas.draw_string(
        px + 52,
        py + 40,
        if state.wifi_enabled {
            "Connected"
        } else {
            "Off"
        },
        Color::CHROME_LINE_LIGHT,
        None,
    );

    let bt_col = if state.bluetooth_enabled {
        Color::ACCENT_BLUE
    } else {
        Color::rgba(255, 255, 255, 28)
    };
    canvas.fill_rounded_rect(px + 132, py + 12, 117, 56, 14, bt_col);
    canvas.fill_circle(
        px + 152,
        py + 40,
        14,
        if state.bluetooth_enabled {
            Color::WHITE
        } else {
            Color::rgba(255, 255, 255, 40)
        },
    );
    canvas.draw_bluetooth(
        px + 152,
        py + 40,
        6,
        if state.bluetooth_enabled {
            Color::ACCENT_BLUE
        } else {
            Color::WHITE
        },
    );
    canvas.draw_string(px + 172, py + 22, "Bluetooth", Color::WHITE, None);
    canvas.draw_string(
        px + 172,
        py + 40,
        if state.bluetooth_enabled { "On" } else { "Off" },
        Color::CHROME_LINE_LIGHT,
        None,
    );

    // Media card.
    canvas.fill_rounded_rect(
        px + 12,
        py + 76,
        237,
        64,
        12,
        Color::rgba(255, 255, 255, 20),
    );
    canvas.draw_rounded_rect(
        px + 12,
        py + 76,
        237,
        64,
        12,
        Color::rgba(255, 255, 255, 30),
    );
    canvas.fill_rounded_rect(px + 22, py + 86, 44, 44, 8, Color::GLOBE_BLUE);
    canvas.draw_browser_icon(px + 22, py + 86, 44);
    canvas.draw_cast_glyph(px + 232, py + 88, 12, Color::CHROME_LINE_LIGHT);
    canvas.draw_string(
        px + 74,
        py + 86,
        "Now Playing",
        Color::CHROME_LINE_LIGHT,
        None,
    );
    let track = if state.media_playing {
        "Dunes at Dusk"
    } else {
        "Dunes (Paused)"
    };
    canvas.draw_string_strong(px + 74, py + 102, track, Color::WHITE, None);

    // Playback control buttons
    canvas.draw_rewind_glyph(px + 145, py + 120, 9, Color::WHITE);
    if state.media_playing {
        canvas.draw_pause_glyph(px + 175, py + 120, 9, Color::WHITE);
    } else {
        canvas.draw_play_glyph(px + 175, py + 120, 9, Color::WHITE);
    }
    canvas.draw_forward_glyph(px + 205, py + 120, 9, Color::WHITE);

    // Sliders: brightness + volume.
    canvas.draw_vertical_slider(px + 12, py + 148, 54, 106, state.brightness_pct, true, true);
    canvas.draw_vertical_slider(px + 74, py + 148, 54, 106, state.volume_pct, false, true);

    // Circular actions row (4 quick toggles: Airplane, DND/Bell, Flashlight, Cast).
    let actions_y = py + 276;
    let toggles = [
        (state.airplane_mode, 0),
        (state.dnd_enabled, 1),
        (state.flashlight_enabled, 2),
        (state.cast_enabled, 3),
    ];
    for (on, i) in toggles {
        let cx = px + 28 + i * 60;
        let btn_bg = if on {
            Color::ACCENT_BLUE
        } else {
            Color::rgba(255, 255, 255, 48)
        };
        canvas.fill_circle(cx, actions_y, 16, btn_bg);
        match i {
            0 => canvas.draw_airplane_glyph(cx, actions_y, 14, Color::WHITE),
            1 => canvas.draw_bell_glyph(cx, actions_y, 14, Color::WHITE),
            2 => canvas.draw_flashlight_glyph(cx, actions_y, 14, Color::WHITE),
            _ => canvas.draw_cast_glyph(cx, actions_y, 14, Color::WHITE),
        }
    }
}

/// Render the bottom-right Control Centre quick-settings popup (legacy wrapper).
pub fn render_control_centre(canvas: &mut Canvas, screen_width: u32, screen_height: u32) {
    render_control_centre_state(
        canvas,
        screen_width,
        screen_height,
        &DesktopState::default(),
    );
}

/// Render Power Options modal: dimmed overlay + Sleep / Shut Down / Restart.
pub fn render_power_options_state(
    canvas: &mut Canvas,
    screen_width: u32,
    screen_height: u32,
    state: &DesktopState,
) {
    canvas.fill_rect(0, 0, screen_width, screen_height, Color::rgba(0, 0, 0, 145));

    if let Some(msg) = state.system_action_message {
        let (dw, dh) = (380u32, 160u32);
        let dx = (screen_width.saturating_sub(dw)) as i32 / 2;
        let dy = (screen_height.saturating_sub(dh)) as i32 / 2;
        canvas.draw_soft_shadow(dx, dy, dw, dh, 18);
        canvas.fill_frosted_rect(dx, dy, dw, dh, 18, Color::rgb(24, 24, 32));
        canvas.draw_rounded_rect(dx, dy, dw, dh, 18, Color::rgba(255, 255, 255, 60));
        canvas.fill_circle(dx + (dw as i32) / 2, dy + 48, 22, Color::ACCENT_BLUE);
        canvas.draw_power_glyph(dx + (dw as i32) / 2, dy + 48, 10, Color::WHITE);
        let lw = text_width(msg);
        canvas.draw_title_strong(
            dx + ((dw.saturating_sub(lw)) as i32) / 2,
            dy + 90,
            msg,
            Color::WHITE,
        );
        let sub = "Saving state and completing cycle...";
        let sw = text_width(sub);
        canvas.draw_string(
            dx + ((dw.saturating_sub(sw)) as i32) / 2,
            dy + 120,
            sub,
            Color::CHROME_LINE_LIGHT,
            None,
        );
        return;
    }

    let labels = ["Sleep", "Shut Down", "Restart"];
    let r = 40;
    let gap = 36;
    let total = 3 * (r * 2) + 2 * gap;
    let mut cx = (screen_width as i32 - total) / 2 + r;
    let cy = screen_height as i32 / 2 - 20;

    for (i, label) in labels.iter().enumerate() {
        canvas.fill_circle(cx, cy, r, Color::rgba(28, 28, 36, 230));
        canvas.draw_circle(cx, cy, r, Color::rgba(255, 255, 255, 90));
        match i {
            0 => canvas.draw_moon_glyph(cx, cy, 14, Color::WHITE, Color::rgba(28, 28, 36, 230)),
            1 => canvas.draw_power_glyph(cx, cy, 14, Color::WHITE),
            _ => canvas.draw_restart_glyph(cx, cy, 14, Color::WHITE),
        }
        let lw = text_width(label);
        canvas.draw_title_strong(cx - (lw / 2) as i32, cy + r + 14, label, Color::WHITE);
        cx += r * 2 + gap;
    }
}

/// Render Power Options (legacy wrapper).
pub fn render_power_options(canvas: &mut Canvas, screen_width: u32, screen_height: u32) {
    render_power_options_state(
        canvas,
        screen_width,
        screen_height,
        &DesktopState::default(),
    );
}
