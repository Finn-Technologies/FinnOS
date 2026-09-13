//! Window Compositor managing double-buffered root display surface, z-ordering, and mouse cursor.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss
)]

use crate::apps::{
    DOCK_ICON, DOCK_WIDTH, DesktopState, START_MENU_SIZE, TASKBAR_HEIGHT,
    render_control_centre_state, render_files_app_state, render_power_options_state,
    render_settings_app_state, render_start_menu_state, render_taskbar, render_terminal_app,
};
use crate::canvas::{Canvas, Color, Rect};
use crate::widget::Window;

/// Mouse cursor arrow bitmap (12x18 pixels).
pub static MOUSE_CURSOR: [u16; 18] = [
    0b1000_0000_0000,
    0b1100_0000_0000,
    0b1110_0000_0000,
    0b1111_0000_0000,
    0b1111_1000_0000,
    0b1111_1100_0000,
    0b1111_1110_0000,
    0b1111_1111_0000,
    0b1111_1111_1000,
    0b1111_1100_0000,
    0b1101_1100_0000,
    0b1000_1110_0000,
    0b0000_1110_0000,
    0b0000_0111_0000,
    0b0000_0111_0000,
    0b0000_0011_1000,
    0b0000_0011_1000,
    0b0000_0000_0000,
];

const CURSOR_W: usize = 16;
const CURSOR_H: usize = 20;

/// Background pixels saved beneath the mouse cursor for fast, flicker-free restoration.
#[derive(Clone, Copy)]
pub struct CursorSavedBg {
    x: i32,
    y: i32,
    w: usize,
    h: usize,
    pixels: [u32; CURSOR_W * CURSOR_H],
    valid: bool,
}

impl Default for CursorSavedBg {
    fn default() -> Self {
        Self {
            x: 0,
            y: 0,
            w: 0,
            h: 0,
            pixels: [0; CURSOR_W * CURSOR_H],
            valid: false,
        }
    }
}

/// Identifiers for standard core graphical applications.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppId {
    /// Terminal command prompt emulator.
    Terminal,
    /// System settings and properties viewer.
    Settings,
    /// File manager for storage browsing.
    Files,
}

/// A desktop window tracked by the compositor.
pub struct CompositorWindow {
    /// Window widget.
    pub window: Window,
    /// Application type.
    pub app_id: AppId,
    /// Z-order priority (higher is in front).
    pub z_order: u32,
    /// Visibility flag.
    pub visible: bool,
}

/// Desktop Window Compositor.
pub struct Compositor {
    /// Screen width in pixels.
    pub width: u32,
    /// Screen height in pixels.
    pub height: u32,
    /// Mouse pointer X position.
    pub mouse_x: i32,
    /// Mouse pointer Y position.
    pub mouse_y: i32,
    /// Saved background under cursor.
    pub cursor_bg: CursorSavedBg,
    /// Windows managed by compositor.
    pub windows: [Option<CompositorWindow>; 8],
    /// Active window index.
    pub active_index: Option<usize>,
    /// Window currently being dragged by mouse, if any.
    pub dragging_window: Option<usize>,
    /// Mouse offset X relative to window bounds during drag.
    pub drag_offset_x: i32,
    /// Mouse offset Y relative to window bounds during drag.
    pub drag_offset_y: i32,
    /// Whether the centered Start Menu card is open.
    pub start_menu_open: bool,
    /// Whether the Control Centre popup is open.
    pub control_centre_open: bool,
    /// Live interactive state for desktop applications and shell.
    pub state: DesktopState,
    /// Hardware GPU cursor plane enabled (decouples cursor rendering from CPU frame canvas).
    pub hardware_cursor: bool,
}

impl Compositor {
    /// Create a new compositor for a display resolution.
    #[must_use]
    pub const fn new(width: u32, height: u32) -> Self {
        Self {
            width,
            height,
            mouse_x: 200,
            mouse_y: 200,
            cursor_bg: CursorSavedBg {
                x: 0,
                y: 0,
                w: 0,
                h: 0,
                pixels: [0; CURSOR_W * CURSOR_H],
                valid: false,
            },
            windows: [None, None, None, None, None, None, None, None],
            active_index: None,
            dragging_window: None,
            drag_offset_x: 0,
            drag_offset_y: 0,
            start_menu_open: false,
            control_centre_open: false,
            hardware_cursor: false,
            state: DesktopState {
                settings_category: 1,
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
            },
        }
    }

    /// Enable hardware GPU cursor plane rendering.
    pub const fn enable_hardware_cursor(&mut self) {
        self.hardware_cursor = true;
    }

    /// Check if hardware GPU cursor plane is active.
    #[must_use]
    pub const fn is_hardware_cursor_enabled(&self) -> bool {
        self.hardware_cursor
    }

    /// Maximum z-order currently assigned.
    #[must_use]
    pub fn max_z(&self) -> u32 {
        self.windows
            .iter()
            .flatten()
            .map(|w| w.z_order)
            .max()
            .unwrap_or(0)
    }

    /// Raise an application window to front and make it visible.
    pub fn raise_app(&mut self, app_id: AppId) {
        let max_z = self.max_z();
        for (idx, slot) in self.windows.iter_mut().enumerate() {
            if let Some(cw) = slot
                && cw.app_id == app_id
            {
                cw.visible = true;
                cw.z_order = max_z + 1;
                self.active_index = Some(idx);
                return;
            }
        }
    }

    /// X origin of the centered 170px dock (Figma 71:448).
    #[must_use]
    pub const fn dock_x(&self) -> i32 {
        ((self.width.saturating_sub(DOCK_WIDTH)) / 2) as i32
    }

    /// Y origin of the 32px dock icons inside the 45px taskbar.
    #[must_use]
    pub const fn dock_y(&self) -> i32 {
        self.height as i32 - TASKBAR_HEIGHT as i32 + ((TASKBAR_HEIGHT - DOCK_ICON) / 2) as i32
    }

    /// Add a window to the compositor.
    pub fn add_window(&mut self, window: Window, app_id: AppId) -> Option<usize> {
        let slot = self.windows.iter().position(Option::is_none)?;
        let z_order = slot as u32;
        self.windows[slot] = Some(CompositorWindow {
            window,
            app_id,
            z_order,
            visible: true,
        });
        self.active_index = Some(slot);
        Some(slot)
    }

    /// Render desktop wallpaper background (authentic photographic landscape, Figma 71:47).
    pub fn render_wallpaper(&self, canvas: &mut Canvas) {
        canvas.fill_desert_wallpaper();
    }

    /// Dock icon X offsets inside the 170px dock (Figma 71:448: 0/46/92/138).
    const DOCK_OFFSETS: [i32; 4] = [0, 46, 92, 138];

    /// Render the centered 170x32 dock with the 4 core Figma icons.
    pub fn render_dock(&self, canvas: &mut Canvas) {
        let dock_x = self.dock_x();
        let dock_y = self.dock_y();
        if dock_y < 0 {
            return;
        }

        canvas.draw_finn_logo(dock_x, dock_y, DOCK_ICON);
        canvas.draw_search_icon(dock_x + 46, dock_y, DOCK_ICON);
        canvas.draw_files_icon(dock_x + 92, dock_y, DOCK_ICON);
        canvas.draw_browser_icon(dock_x + 138, dock_y, DOCK_ICON);

        // Running indicators: Terminal/Settings/Files
        let running = [true, true, true];
        for (i, on) in running.iter().enumerate() {
            if *on {
                let dot_x = dock_x + Self::DOCK_OFFSETS[i + 1] + (DOCK_ICON as i32) / 2;
                canvas.fill_circle(dot_x, dock_y + DOCK_ICON as i32 + 3, 2, Color::WHITE);
            }
        }
        canvas.fill_circle(
            dock_x + (DOCK_ICON as i32) / 2,
            dock_y + DOCK_ICON as i32 + 3,
            2,
            Color::WHITE,
        );
    }

    /// Restore background pixels previously saved under the cursor.
    pub fn restore_cursor_background(&mut self, canvas: &mut Canvas) {
        if !self.cursor_bg.valid {
            return;
        }
        for r in 0..self.cursor_bg.h {
            let cy = self.cursor_bg.y + r as i32;
            for c in 0..self.cursor_bg.w {
                let cx = self.cursor_bg.x + c as i32;
                let p = self.cursor_bg.pixels[r * CURSOR_W + c];
                canvas.set_pixel_raw(cx, cy, p);
            }
        }
        self.cursor_bg.valid = false;
    }

    /// Draw the mouse cursor arrow and save background under it.
    #[allow(clippy::needless_range_loop)]
    pub fn draw_mouse_cursor(&mut self, canvas: &mut Canvas) {
        let mx = self.mouse_x;
        let my = self.mouse_y;

        let max_w = CURSOR_W.min((canvas.width() as i32 - mx).max(0) as usize);
        let max_h = CURSOR_H.min((canvas.height() as i32 - my).max(0) as usize);
        self.cursor_bg.x = mx;
        self.cursor_bg.y = my;
        self.cursor_bg.w = max_w;
        self.cursor_bg.h = max_h;
        for r in 0..max_h {
            let cy = my + r as i32;
            for c in 0..max_w {
                let cx = mx + c as i32;
                self.cursor_bg.pixels[r * CURSOR_W + c] = canvas.get_pixel_raw(cx, cy).unwrap_or(0);
            }
        }
        self.cursor_bg.valid = true;

        for row in 0..18 {
            let line = MOUSE_CURSOR[row];
            for col in 0..12usize {
                if (line & (0x800 >> col)) != 0 {
                    let px = mx + col as i32;
                    let py = my + row as i32;
                    let is_interior = col > 0 && col < 9 && row > 1 && row < 14 && (col < row);
                    let color = if is_interior {
                        Color::WHITE
                    } else {
                        Color::BLACK
                    };
                    canvas.set_pixel(px, py, color);
                }
            }
        }
    }

    /// Update mouse coordinates and restore/redraw cursor with zero flicker.
    pub fn update_mouse_position(&mut self, canvas: &mut Canvas, new_x: i32, new_y: i32) {
        let clamped_x = new_x.clamp(0, (self.width.saturating_sub(1)) as i32);
        let clamped_y = new_y.clamp(0, (self.height.saturating_sub(1)) as i32);

        if self.mouse_x == clamped_x && self.mouse_y == clamped_y {
            return;
        }

        self.mouse_x = clamped_x;
        self.mouse_y = clamped_y;

        if let Some(slot) = self.dragging_window
            && let Some(cw) = &mut self.windows[slot]
        {
            cw.window.bounds.x = clamped_x - self.drag_offset_x;
            cw.window.bounds.y = (clamped_y - self.drag_offset_y)
                .clamp(0, (self.height - TASKBAR_HEIGHT - 30) as i32);
            self.compose(canvas, "finnos", 100);
            return;
        }

        if !self.hardware_cursor {
            self.restore_cursor_background(canvas);
            self.draw_mouse_cursor(canvas);
        }
    }

    /// Handle mouse button release to stop window dragging.
    pub const fn handle_mouse_up(&mut self) {
        self.dragging_window = None;
    }

    /// Handle mouse click to activate, raise, close, or toggle shell popups and widgets.
    #[allow(clippy::too_many_lines)]
    pub fn handle_click(&mut self, canvas: &mut Canvas, left_pressed: bool) {
        if !left_pressed {
            return;
        }
        let mx = self.mouse_x;
        let my = self.mouse_y;

        // 1. Power Modal (if open)
        if self.state.power_modal_open {
            if self.state.system_action_message.is_some() {
                self.state.system_action_message = None;
                self.state.power_modal_open = false;
                self.compose(canvas, "finnos", 100);
                return;
            }
            let r = 40i32;
            let gap = 36i32;
            let total = 3 * (r * 2) + 2 * gap;
            let mut cx = (self.width as i32 - total) / 2 + r;
            let cy = self.height as i32 / 2 - 20;

            let mut clicked_btn = None;
            for idx in 0..3 {
                let dx = i64::from(mx - cx);
                let dy = i64::from(my - cy);
                let r_i64 = i64::from(r);
                if dx * dx + dy * dy <= r_i64 * r_i64 {
                    clicked_btn = Some(idx);
                    break;
                }
                cx += r * 2 + gap;
            }

            match clicked_btn {
                Some(0) => {
                    self.state.system_action_message = Some("FinnOS Sleep Mode (Click to resume)");
                }
                Some(1) => {
                    self.state.system_action_message = Some("Shutting down FinnOS...");
                }
                Some(2) => {
                    self.state.system_action_message = Some("Restarting FinnOS...");
                }
                _ => {
                    self.state.power_modal_open = false;
                }
            }
            self.compose(canvas, "finnos", 100);
            return;
        }

        // 2. Start Menu (if open)
        if self.start_menu_open {
            let size = START_MENU_SIZE
                .min(self.width)
                .min(self.height.saturating_sub(TASKBAR_HEIGHT + 16));
            let sm_x = ((self.width.saturating_sub(size)) / 2) as i32;
            let sm_y = ((self
                .height
                .saturating_sub(TASKBAR_HEIGHT)
                .saturating_sub(size))
                / 2) as i32;
            let sm_rect = Rect::new(sm_x, sm_y, size, size);

            if sm_rect.contains(mx, my) {
                let s_btn_x = sm_x + size as i32 - 76;
                let p_btn_x = sm_x + size as i32 - 38;
                let btn_y = sm_y + 42;
                let d_s = (mx - s_btn_x) * (mx - s_btn_x) + (my - btn_y) * (my - btn_y);
                let d_p = (mx - p_btn_x) * (mx - p_btn_x) + (my - btn_y) * (my - btn_y);

                if d_s <= 16 * 16 {
                    self.start_menu_open = false;
                    self.state.start_menu_open = false;
                    self.raise_app(AppId::Settings);
                    self.compose(canvas, "finnos", 100);
                    return;
                } else if d_p <= 16 * 16 {
                    self.start_menu_open = false;
                    self.state.start_menu_open = false;
                    self.state.power_modal_open = true;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let cell = size.saturating_sub(56) / 4;
                // Row 1 (Terminal, Files, Browser, Settings)
                let grid_y1 = sm_y + 120;
                for i in 0..4usize {
                    let cx = sm_x + 28 + (i as i32) * cell as i32 + (cell as i32 - 48) / 2;
                    let tile_rect = Rect::new(cx, grid_y1, 48, 68);
                    if tile_rect.contains(mx, my) {
                        self.start_menu_open = false;
                        self.state.start_menu_open = false;
                        match i {
                            0 => self.raise_app(AppId::Terminal),
                            1 | 2 => self.raise_app(AppId::Files),
                            _ => self.raise_app(AppId::Settings),
                        }
                        self.compose(canvas, "finnos", 100);
                        return;
                    }
                }

                // Row 2 (Editor, Monitor, Storage, Calculator)
                let grid_y2 = sm_y + 196;
                for i in 0..4usize {
                    let cx = sm_x + 28 + (i as i32) * cell as i32 + (cell as i32 - 48) / 2;
                    let tile_rect = Rect::new(cx, grid_y2, 48, 68);
                    if tile_rect.contains(mx, my) {
                        self.start_menu_open = false;
                        self.state.start_menu_open = false;
                        match i {
                            0 | 1 => self.raise_app(AppId::Terminal),
                            2 => self.raise_app(AppId::Files),
                            _ => self.raise_app(AppId::Settings),
                        }
                        self.compose(canvas, "finnos", 100);
                        return;
                    }
                }

                // Recent documents
                let recents_card_y = sm_y + 292;
                for i in 0..3usize {
                    let item_rect = Rect::new(
                        sm_x + 28,
                        recents_card_y + 8 + (i as i32) * 24,
                        size.saturating_sub(56),
                        24,
                    );
                    if item_rect.contains(mx, my) {
                        self.start_menu_open = false;
                        self.state.start_menu_open = false;
                        match i {
                            0 => self.raise_app(AppId::Terminal),
                            1 => self.raise_app(AppId::Settings),
                            _ => self.raise_app(AppId::Files),
                        }
                        self.compose(canvas, "finnos", 100);
                        return;
                    }
                }

                return;
            }
            self.start_menu_open = false;
            self.state.start_menu_open = false;
            self.compose(canvas, "finnos", 100);
            return;
        }

        // 3. Control Centre (if open)
        if self.control_centre_open {
            let (pw, ph) = (261u32, 302u32);
            let px = self.width.saturating_sub(pw + 12) as i32;
            let py = self.height.saturating_sub(TASKBAR_HEIGHT + ph + 12) as i32;
            let cc_rect = Rect::new(px, py, pw, ph);

            if cc_rect.contains(mx, my) {
                let wifi_rect = Rect::new(px + 12, py + 12, 112, 56);
                if wifi_rect.contains(mx, my) {
                    self.state.wifi_enabled = !self.state.wifi_enabled;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let bt_rect = Rect::new(px + 132, py + 12, 117, 56);
                if bt_rect.contains(mx, my) {
                    self.state.bluetooth_enabled = !self.state.bluetooth_enabled;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let play_rect = Rect::new(px + 140, py + 112, 40, 24);
                if play_rect.contains(mx, my) {
                    self.state.media_playing = !self.state.media_playing;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let bright_rect = Rect::new(px + 12, py + 148, 54, 106);
                if bright_rect.contains(mx, my) {
                    let rel_y = (py + 148 + 106 - my).clamp(0, 106);
                    self.state.brightness_pct = ((rel_y as u32 * 100) / 106).clamp(10, 100);
                    self.compose(canvas, "finnos", 100);
                    return;
                }
                let vol_rect = Rect::new(px + 74, py + 148, 54, 106);
                if vol_rect.contains(mx, my) {
                    let rel_y = (py + 148 + 106 - my).clamp(0, 106);
                    self.state.volume_pct = ((rel_y as u32 * 100) / 106).clamp(0, 100);
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let actions_y = py + 276;
                for i in 0..4i32 {
                    let cx = px + 28 + i * 60;
                    let d_sq = (mx - cx) * (mx - cx) + (my - actions_y) * (my - actions_y);
                    if d_sq <= 16 * 16 {
                        match i {
                            0 => self.state.airplane_mode = !self.state.airplane_mode,
                            1 => self.state.dnd_enabled = !self.state.dnd_enabled,
                            2 => self.state.flashlight_enabled = !self.state.flashlight_enabled,
                            _ => self.state.cast_enabled = !self.state.cast_enabled,
                        }
                        self.compose(canvas, "finnos", 100);
                        return;
                    }
                }
                return;
            }
            self.control_centre_open = false;
            self.state.control_centre_open = false;
            self.compose(canvas, "finnos", 100);
            return;
        }

        // 4. Centered dock inside 45px taskbar
        let dock_x = self.dock_x();
        let dock_y = self.dock_y();
        if my >= dock_y - 4
            && my < dock_y + DOCK_ICON as i32 + 8
            && mx >= dock_x - 4
            && mx < dock_x + DOCK_WIDTH as i32 + 4
        {
            if mx >= dock_x && mx < dock_x + DOCK_ICON as i32 {
                self.start_menu_open = !self.start_menu_open;
                self.state.start_menu_open = self.start_menu_open;
                self.compose(canvas, "finnos", 100);
                return;
            }
            let clicked_app = if mx >= dock_x + 46 && mx < dock_x + 46 + DOCK_ICON as i32 {
                Some(AppId::Terminal)
            } else if mx >= dock_x + 92 && mx < dock_x + 92 + DOCK_ICON as i32 {
                Some(AppId::Files)
            } else if mx >= dock_x + 138 && mx < dock_x + 138 + DOCK_ICON as i32 {
                Some(AppId::Settings)
            } else {
                None
            };
            if let Some(target) = clicked_app {
                self.start_menu_open = false;
                self.state.start_menu_open = false;
                self.raise_app(target);
                self.compose(canvas, "finnos", 100);
                return;
            }
        }

        // 5. Right tray click toggles Control Centre
        let bar_y = self.height as i32 - TASKBAR_HEIGHT as i32;
        if my >= bar_y && mx >= self.width as i32 - 260 {
            self.control_centre_open = !self.control_centre_open;
            self.state.control_centre_open = self.control_centre_open;
            self.compose(canvas, "finnos", 100);
            return;
        }

        // 6. Check windows in reverse z-order
        let mut order: [usize; 8] = [0, 1, 2, 3, 4, 5, 6, 7];
        for i in 0..8 {
            for j in (i + 1)..8 {
                let z_i = self.windows[order[i]].as_ref().map_or(0, |w| w.z_order);
                let z_j = self.windows[order[j]].as_ref().map_or(0, |w| w.z_order);
                if z_i < z_j {
                    order.swap(i, j);
                }
            }
        }

        let current_max_z = self.max_z();

        for &slot in &order {
            if let Some(cw) = &mut self.windows[slot] {
                if !cw.visible {
                    continue;
                }

                if cw.window.is_close_button(mx, my) {
                    cw.visible = false;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                if cw.window.is_minimize_button(mx, my) {
                    cw.visible = false;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                if cw.window.is_title_bar(mx, my) {
                    cw.z_order = current_max_z + 1;
                    self.active_index = Some(slot);
                    self.dragging_window = Some(slot);
                    self.drag_offset_x = mx - cw.window.bounds.x;
                    self.drag_offset_y = my - cw.window.bounds.y;
                    self.compose(canvas, "finnos", 100);
                    return;
                }

                let client = cw.window.client_rect();
                if client.contains(mx, my) {
                    cw.z_order = current_max_z + 1;
                    self.active_index = Some(slot);

                    match cw.app_id {
                        AppId::Settings => {
                            let sidebar_w = (client.width / 3).clamp(96, 250);
                            if mx < client.x + sidebar_w as i32 {
                                let profile_y = client.y + 40 + 36;
                                let mut side_y = profile_y + 50;
                                for idx in 0..5usize {
                                    let btn_r = Rect::new(
                                        client.x + 10,
                                        side_y,
                                        sidebar_w.saturating_sub(20),
                                        22,
                                    );
                                    if btn_r.contains(mx, my) {
                                        self.state.settings_category = idx;
                                        break;
                                    }
                                    side_y += 26;
                                }
                            } else {
                                let content_x = client.x + sidebar_w as i32 + 12;
                                let content_w = client.width.saturating_sub(sidebar_w + 24);
                                let sw_x = content_x + content_w as i32 - 46;

                                if self.state.settings_category == 3 {
                                    let ty_base = client.y + 12 + 30 + 10;
                                    for idx in 0..3usize {
                                        let sw_r = Rect::new(
                                            sw_x - 10,
                                            ty_base + (idx as i32) * 32 - 4,
                                            50,
                                            26,
                                        );
                                        if sw_r.contains(mx, my) {
                                            match idx {
                                                0 => {
                                                    self.state.setting_wx_paging =
                                                        !self.state.setting_wx_paging;
                                                }
                                                1 => {
                                                    self.state.setting_capabilities =
                                                        !self.state.setting_capabilities;
                                                }
                                                _ => {
                                                    self.state.setting_preemption =
                                                        !self.state.setting_preemption;
                                                }
                                            }
                                            break;
                                        }
                                    }
                                } else if self.state.settings_category == 1 {
                                    let ty_base = client.y + 12 + 30 + 12;
                                    for idx in 0..2usize {
                                        let sw_r = Rect::new(
                                            sw_x - 10,
                                            ty_base + (idx as i32) * 36 - 4,
                                            50,
                                            26,
                                        );
                                        if sw_r.contains(mx, my) {
                                            if idx == 0 {
                                                self.state.setting_dark_mode =
                                                    !self.state.setting_dark_mode;
                                            } else {
                                                self.state.setting_animations =
                                                    !self.state.setting_animations;
                                            }
                                            break;
                                        }
                                    }
                                } else if self.state.settings_category == 4 {
                                    let ty_base = client.y + 12 + 30 + 92 + 8 + 32;
                                    for idx in 0..3usize {
                                        let sw_r = Rect::new(
                                            sw_x - 10,
                                            ty_base + (idx as i32) * 22 - 4,
                                            50,
                                            24,
                                        );
                                        if sw_r.contains(mx, my) {
                                            match idx {
                                                0 => {
                                                    self.state.setting_wx_paging =
                                                        !self.state.setting_wx_paging;
                                                }
                                                1 => {
                                                    self.state.setting_capabilities =
                                                        !self.state.setting_capabilities;
                                                }
                                                _ => {
                                                    self.state.setting_preemption =
                                                        !self.state.setting_preemption;
                                                }
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        AppId::Files => {
                            let sidebar_w = 114u32;
                            if mx < client.x + sidebar_w as i32 {
                                let mut side_y = client.y + 10 + 18;
                                for idx in 0..3usize {
                                    let btn_r = Rect::new(client.x + 6, side_y, sidebar_w - 12, 22);
                                    if btn_r.contains(mx, my) {
                                        self.state.files_location = idx;
                                        break;
                                    }
                                    side_y += 24;
                                }
                                side_y += 6 + 18;
                                for idx in 3..6usize {
                                    let btn_r = Rect::new(client.x + 6, side_y, sidebar_w - 12, 22);
                                    if btn_r.contains(mx, my) {
                                        self.state.files_location = idx;
                                        break;
                                    }
                                    side_y += 24;
                                }
                            } else {
                                let content_x = client.x + sidebar_w as i32 + 10;
                                let content_w = client.width.saturating_sub(sidebar_w + 20);
                                let row_start_y = client.y + 8 + 30 + 24;
                                for row_idx in 0..5usize {
                                    let row_r = Rect::new(
                                        content_x,
                                        row_start_y
                                            + (row_idx as i32)
                                                * (crate::font::TEXT_LINE as i32 + 1)
                                            - 2,
                                        content_w,
                                        20,
                                    );
                                    if row_r.contains(mx, my) {
                                        self.state.files_selected_row = Some(row_idx);
                                        break;
                                    }
                                }
                            }
                        }
                        AppId::Terminal => {
                            self.state.terminal_command_index =
                                (self.state.terminal_command_index + 1) % 3;
                        }
                    }

                    self.compose(canvas, "finnos", 100);
                    return;
                }
            }
        }
    }

    /// Compose all desktop layers into canvas.
    ///
    /// Order: desert photographic wallpaper (71:47) → windows (z-order) → Start Menu card
    /// (71:438) / Control Centre (44:381) → 45px frosted taskbar with centered
    /// dock + tray (71:125) → Power Modal (112:245) → mouse cursor.
    pub fn compose(&mut self, canvas: &mut Canvas, arch_name: &str, uptime_ticks: u64) {
        self.cursor_bg.valid = false;

        // 1. Authentic photographic desert landscape wallpaper.
        self.render_wallpaper(canvas);

        // 2. Render windows in z-order
        let mut order: [usize; 8] = [0, 1, 2, 3, 4, 5, 6, 7];
        for i in 0..8 {
            for j in (i + 1)..8 {
                let z_i = self.windows[order[i]].as_ref().map_or(0, |w| w.z_order);
                let z_j = self.windows[order[j]].as_ref().map_or(0, |w| w.z_order);
                if z_i > z_j {
                    order.swap(i, j);
                }
            }
        }

        for &idx in &order {
            if let Some(cw) = &mut self.windows[idx] {
                if !cw.visible {
                    continue;
                }
                cw.window.active = self.active_index == Some(idx);
                cw.window.render(canvas);

                let client = cw.window.client_rect();
                match cw.app_id {
                    AppId::Terminal => render_terminal_app(canvas, client),
                    AppId::Settings => {
                        render_settings_app_state(canvas, client, arch_name, &self.state);
                    }
                    AppId::Files => render_files_app_state(canvas, client, &self.state),
                }
                cw.window.render_border(canvas);
            }
        }

        // 3. Floating shell popups (above windows, below taskbar).
        if self.start_menu_open {
            render_start_menu_state(canvas, self.width, self.height, &self.state);
        }
        if self.control_centre_open {
            render_control_centre_state(canvas, self.width, self.height, &self.state);
        }

        // 4. Bottom 45px frosted taskbar with centered dock + tray.
        render_taskbar(canvas, self.width, self.height, arch_name, uptime_ticks);

        // 5. Full-screen Power Options Modal (if open)
        if self.state.power_modal_open {
            render_power_options_state(canvas, self.width, self.height, &self.state);
        }

        // 6. Mouse cursor on top of all desktop layers (only if software cursor is active)
        if !self.hardware_cursor {
            self.draw_mouse_cursor(canvas);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn compositor_window_management() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(1280, 800);
        let win = Window::new("Terminal", Rect::new(50, 50, 600, 400));
        let slot = compositor.add_window(win, AppId::Terminal);
        assert_eq!(slot, Some(0));
        assert_eq!(compositor.active_index, Some(0));

        let mut buf = std::vec![0u32; 1280 * 800];
        let mut canvas = Canvas::new(&mut buf, 1280, 800, 1280);
        compositor.compose(&mut canvas, "x86_64", 100);

        // Assert that canvas has non-zero pixels rendered
        assert!(canvas.pixels().iter().any(|&p| p != 0));
    }

    #[test]
    fn compositor_mouse_movement_updates_cursor() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(640, 480);
        let win = Window::new("Settings", Rect::new(20, 40, 200, 150));
        compositor.add_window(win, AppId::Settings);

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 0);

        assert_eq!(compositor.mouse_x, 200);
        assert_eq!(compositor.mouse_y, 200);

        // Move mouse
        compositor.update_mouse_position(&mut canvas, 250, 300);
        assert_eq!(compositor.mouse_x, 250);
        assert_eq!(compositor.mouse_y, 300);

        // Test clicking close button
        compositor.update_mouse_position(&mut canvas, 20 + 200 - 15, 40 + 10);
        compositor.handle_click(&mut canvas, true);
        assert!(!compositor.windows[0].as_ref().unwrap().visible);
    }

    #[test]
    fn compositor_window_dragging() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(640, 480);
        let win = Window::new("Terminal", Rect::new(50, 50, 200, 150));
        compositor.add_window(win, AppId::Terminal);

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 0);

        // Click title bar at (150, 60) - inside title bar [50..250, 50..84], past chrome controls
        compositor.update_mouse_position(&mut canvas, 150, 60);
        compositor.handle_click(&mut canvas, true);
        assert_eq!(compositor.dragging_window, Some(0));
        assert_eq!(compositor.drag_offset_x, 100);
        assert_eq!(compositor.drag_offset_y, 10);

        // Drag window by moving mouse to (210, 120)
        compositor.update_mouse_position(&mut canvas, 210, 120);
        assert_eq!(compositor.windows[0].as_ref().unwrap().window.bounds.x, 110);
        assert_eq!(compositor.windows[0].as_ref().unwrap().window.bounds.y, 110);

        // Release mouse
        compositor.handle_mouse_up();
        assert_eq!(compositor.dragging_window, None);

        // Move mouse without dragging: window position stays at (110, 110)
        compositor.update_mouse_position(&mut canvas, 200, 200);
        assert_eq!(compositor.windows[0].as_ref().unwrap().window.bounds.x, 110);
        assert_eq!(compositor.windows[0].as_ref().unwrap().window.bounds.y, 110);
    }

    #[test]
    fn compositor_dock_click_unhides_window() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(640, 480);
        let win = Window::new("Terminal", Rect::new(50, 50, 200, 150));
        compositor.add_window(win, AppId::Terminal);

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 0);

        // Hide window
        compositor.windows[0].as_mut().unwrap().visible = false;
        assert!(!compositor.windows[0].as_ref().unwrap().visible);

        // Click Terminal (Search) tile in the Figma 170x32 dock:
        // dock_x = (640 - 170) / 2 = 235, dock_y = 480 - 45 + 6 = 441.
        // Search icon at dock_x + 46 = 281 (32x32 => center 297, 457).
        compositor.update_mouse_position(&mut canvas, 297, 457);
        compositor.handle_click(&mut canvas, true);

        // Window should be unhidden and raised!
        assert!(compositor.windows[0].as_ref().unwrap().visible);
    }

    #[test]
    fn compositor_taskbar_renders_bottom_bar() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(640, 480);
        let win = Window::new("Terminal", Rect::new(50, 50, 200, 150));
        compositor.add_window(win, AppId::Terminal);

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 6000);
        // Bottom taskbar row is painted (not wallpaper alone).
        let bar_y = 480 - crate::apps::TASKBAR_HEIGHT as usize;
        assert!(canvas.pixels()[bar_y * 640..].iter().any(|&p| p != 0));
        // Finn logo toggles the Start Menu.
        let dock_x = ((640 - crate::apps::DOCK_WIDTH) / 2) as i32;
        let dock_y = 480i32 - crate::apps::TASKBAR_HEIGHT as i32 + 6;
        compositor.update_mouse_position(&mut canvas, dock_x + 16, dock_y + 16);
        compositor.handle_click(&mut canvas, true);
        assert!(compositor.start_menu_open);
    }

    #[test]
    fn compositor_tray_toggles_control_centre() {
        use crate::canvas::Rect;

        let mut compositor = Compositor::new(640, 480);
        let win = Window::new("Files", Rect::new(50, 50, 200, 150));
        compositor.add_window(win, AppId::Files);

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 0);
        compositor.update_mouse_position(&mut canvas, 600, 460);
        compositor.handle_click(&mut canvas, true);
        assert!(compositor.control_centre_open);
    }

    #[test]
    fn compositor_hardware_cursor_decouples_from_canvas() {
        let mut compositor = Compositor::new(640, 480);
        assert!(!compositor.is_hardware_cursor_enabled());
        compositor.enable_hardware_cursor();
        assert!(compositor.is_hardware_cursor_enabled());

        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        compositor.compose(&mut canvas, "test", 0);

        // Snapshot buffer after compose with hardware cursor enabled
        let snapshot = canvas.pixels().to_vec();

        // Update mouse position with hardware cursor active
        compositor.update_mouse_position(&mut canvas, 300, 200);

        // Canvas pixels should be completely untouched because GPU handles cursor plane
        assert_eq!(canvas.pixels(), snapshot.as_slice());
    }
}
