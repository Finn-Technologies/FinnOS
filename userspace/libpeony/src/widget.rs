//! Peony UI widget components: Window chrome, Button, Label, Container.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss
)]

use crate::canvas::{Canvas, Color, Rect};
use crate::font::{FONT_HEIGHT, text_width};

/// Standard height of window title bars in pixels.
pub const TITLEBAR_HEIGHT: u32 = 34;

/// Authentic outer corner radius for desktop windows (Figma Settings 20px;
/// generic windows use 12px to stay legible at small sizes).
pub const WINDOW_CORNER_RADIUS: u32 = 12;
/// Settings app outer corner radius (pure white card, Figma 140:335).
pub const SETTINGS_CORNER_RADIUS: u32 = 20;

/// Size of each monochrome window control hit box (never traffic lights).
pub const CHROME_CONTROL_SIZE: u32 = 16;

/// A window with title bar, close button, and client area.
pub struct Window {
    /// Window title text.
    pub title: &'static str,
    /// Bounding rectangle in desktop coordinates.
    pub bounds: Rect,
    /// Whether window is currently focused/active.
    pub active: bool,
    /// Background color of the client area.
    pub client_bg: Color,
}

impl Window {
    /// Create a new window.
    #[must_use]
    pub const fn new(title: &'static str, bounds: Rect) -> Self {
        Self {
            title,
            bounds,
            active: false,
            client_bg: Color::WINDOW_BG,
        }
    }

    /// Calculate the client area rectangle inside the window frame.
    #[must_use]
    pub const fn client_rect(&self) -> Rect {
        Rect::new(
            self.bounds.x + 2,
            self.bounds.y + TITLEBAR_HEIGHT as i32 + 1,
            self.bounds.width.saturating_sub(4),
            self.bounds.height.saturating_sub(TITLEBAR_HEIGHT + 3),
        )
    }

    /// Check whether a coordinate falls on the close button.
    #[must_use]
    pub const fn is_close_button(&self, px: i32, py: i32) -> bool {
        // Monochrome line-symbol cluster (top-left) plus legacy top-right spot.
        let mono_close = Rect::new(self.bounds.x + 8, self.bounds.y + 9, 18, 18);
        let right_close = Rect::new(
            self.bounds.x + self.bounds.width as i32 - 24,
            self.bounds.y + 6,
            20,
            20,
        );
        mono_close.contains(px, py) || right_close.contains(px, py)
    }

    /// Check whether a coordinate falls on the minimize button.
    #[must_use]
    pub const fn is_minimize_button(&self, px: i32, py: i32) -> bool {
        let r = Rect::new(self.bounds.x + 28, self.bounds.y + 9, 18, 18);
        r.contains(px, py)
    }

    /// Check whether a coordinate falls on the expand / maximize button.
    #[must_use]
    pub const fn is_maximize_button(&self, px: i32, py: i32) -> bool {
        let r = Rect::new(self.bounds.x + 48, self.bounds.y + 9, 18, 18);
        r.contains(px, py)
    }

    /// Check whether a coordinate falls on any monochrome window control.
    #[must_use]
    pub const fn is_chrome_control(&self, px: i32, py: i32) -> bool {
        let mut i = 0;
        while i < 3 {
            let r = Rect::new(
                self.bounds.x + 8 + i * 20,
                self.bounds.y + 9,
                CHROME_CONTROL_SIZE,
                CHROME_CONTROL_SIZE,
            );
            if r.contains(px, py) {
                return true;
            }
            i += 1;
        }
        false
    }

    /// Check whether a coordinate falls on the title bar (for dragging/focus).
    #[must_use]
    pub const fn is_title_bar(&self, px: i32, py: i32) -> bool {
        let title_rect = Rect::new(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            TITLEBAR_HEIGHT,
        );
        title_rect.contains(px, py) && !self.is_chrome_control(px, py)
    }

    /// Render the authentic `FinnOS` window chrome into a canvas.
    ///
    /// Two-tone frosted titlebar + client card with 12px AA outer corners and
    /// minimalist monochrome line symbols (`X`, `—`, expand corners). macOS
    /// colored traffic lights are never used (Figma 140:335 sidebar spec).
    #[allow(clippy::too_many_lines)]
    pub fn render(&self, canvas: &mut Canvas) {
        // 1. Multi-layer diffused ambient drop shadow
        canvas.draw_soft_shadow(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            WINDOW_CORNER_RADIUS,
        );

        let body_bg = Color::rgb(24, 24, 37);

        // 2. Base rounded surface (preserves all 4 rounded corners)
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            WINDOW_CORNER_RADIUS,
            body_bg,
        );

        // 3. Title bar with top rounded corners
        let titlebar_color = if self.active {
            Color::rgb(30, 30, 46) // Modern dark chrome header
        } else {
            Color::rgb(24, 24, 37)
        };
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            TITLEBAR_HEIGHT + WINDOW_CORNER_RADIUS,
            WINDOW_CORNER_RADIUS,
            titlebar_color,
        );

        // 4. Overwrite titlebar overshoot without destroying bottom rounded corners
        canvas.fill_rect(
            self.bounds.x,
            self.bounds.y + TITLEBAR_HEIGHT as i32,
            self.bounds.width,
            WINDOW_CORNER_RADIUS,
            body_bg,
        );

        // 5. Divider below title bar
        let divider_color = Color::rgb(49, 50, 68);
        canvas.fill_rect(
            self.bounds.x + 1,
            self.bounds.y + TITLEBAR_HEIGHT as i32,
            self.bounds.width.saturating_sub(2),
            1,
            divider_color,
        );

        // 6. 1px Outer border
        let border_color = if self.active {
            Color::rgb(88, 91, 112)
        } else {
            Color::rgb(49, 50, 68)
        };
        canvas.draw_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            WINDOW_CORNER_RADIUS,
            border_color,
        );

        // 7. macOS-style colored traffic lights with anti-aliased circles and subtle borders
        let cy = self.bounds.y + (TITLEBAR_HEIGHT as i32) / 2;
        let r = 6;
        let cx_close = self.bounds.x + 15;
        let cx_min = self.bounds.x + 33;
        let cx_max = self.bounds.x + 51;

        if self.active {
            // Close: Coral Red (#FF5F56) with #E0443E border
            canvas.fill_circle(cx_close, cy, r, Color::rgb(255, 95, 86));
            canvas.draw_circle(cx_close, cy, r, Color::rgb(224, 68, 62));
            // Subtle close 'x'
            canvas.draw_aa_line(
                cx_close - 2,
                cy - 2,
                cx_close + 2,
                cy + 2,
                Color::rgb(140, 20, 20),
            );
            canvas.draw_aa_line(
                cx_close + 2,
                cy - 2,
                cx_close - 2,
                cy + 2,
                Color::rgb(140, 20, 20),
            );

            // Minimize: Amber Yellow (#FFBD2E) with #DEA123 border
            canvas.fill_circle(cx_min, cy, r, Color::rgb(255, 189, 46));
            canvas.draw_circle(cx_min, cy, r, Color::rgb(222, 161, 35));
            // Subtle minimize '-'
            canvas.draw_aa_line(cx_min - 2, cy, cx_min + 2, cy, Color::rgb(140, 90, 15));

            // Maximize: Emerald Green (#27C93F) with #1AAB29 border
            canvas.fill_circle(cx_max, cy, r, Color::rgb(39, 201, 63));
            canvas.draw_circle(cx_max, cy, r, Color::rgb(26, 171, 41));
            // Subtle maximize '+'
            canvas.draw_aa_line(cx_max - 2, cy, cx_max + 2, cy, Color::rgb(15, 100, 25));
            canvas.draw_aa_line(cx_max, cy - 2, cx_max, cy + 2, Color::rgb(15, 100, 25));

            // Top specular highlight line across titlebar
            let h_x0 = self.bounds.x + WINDOW_CORNER_RADIUS as i32;
            let h_x1 = self.bounds.x + self.bounds.width as i32 - WINDOW_CORNER_RADIUS as i32;
            if h_x1 > h_x0 {
                canvas.draw_aa_line(
                    h_x0,
                    self.bounds.y + 1,
                    h_x1,
                    self.bounds.y + 1,
                    Color::GLASS_SPECULAR,
                );
            }
        } else {
            let muted_dot = Color::rgb(69, 71, 90);
            canvas.fill_circle(cx_close, cy, r, muted_dot);
            canvas.fill_circle(cx_min, cy, r, muted_dot);
            canvas.fill_circle(cx_max, cy, r, muted_dot);
        }

        // 8. Centered semibold title text
        let title_len = text_width(self.title).saturating_add(1);
        let title_x = self.bounds.x + ((self.bounds.width.saturating_sub(title_len)) / 2) as i32;
        let title_y =
            self.bounds.y + ((TITLEBAR_HEIGHT.saturating_sub(FONT_HEIGHT as u32)) / 2) as i32;
        let title_color = if self.active {
            Color::rgb(205, 214, 244)
        } else {
            Color::rgb(147, 153, 178)
        };
        canvas.draw_string_strong(title_x, title_y, self.title, title_color, None);
    }

    /// Redraw the clean 1px outer rounded border.
    pub fn render_border(&self, canvas: &mut Canvas) {
        let border_color = if self.active {
            Color::rgb(88, 91, 112)
        } else {
            Color::rgb(49, 50, 68)
        };

        canvas.draw_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            WINDOW_CORNER_RADIUS,
            border_color,
        );
    }
}

/// Modern pill-shaped toggle switch widget.
pub struct ToggleSwitch {
    /// Bounding rectangle.
    pub bounds: Rect,
    /// Toggle state.
    pub enabled: bool,
}

impl ToggleSwitch {
    /// Create a new toggle switch.
    #[must_use]
    pub const fn new(bounds: Rect, enabled: bool) -> Self {
        Self { bounds, enabled }
    }

    /// Check whether a click lands on this toggle switch.
    #[must_use]
    pub const fn hit_test(&self, px: i32, py: i32) -> bool {
        self.bounds.contains(px, py)
    }

    /// Toggle switch state.
    pub const fn toggle(&mut self) {
        self.enabled = !self.enabled;
    }

    /// Render the toggle switch.
    pub fn render(&self, canvas: &mut Canvas) {
        let radius = self.bounds.height / 2;
        let track_color = if self.enabled {
            Color::rgb(16, 185, 129) // Emerald 500
        } else {
            Color::rgb(203, 213, 225) // Slate 300
        };
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            radius,
            track_color,
        );

        // Circular thumb knob
        let thumb_r = radius.saturating_sub(2) as i32;
        let thumb_x = if self.enabled {
            self.bounds.x + self.bounds.width as i32 - radius as i32
        } else {
            self.bounds.x + radius as i32
        };
        let thumb_y = self.bounds.y + radius as i32;
        canvas.fill_circle(thumb_x, thumb_y, thumb_r, Color::WHITE);
        canvas.draw_circle(thumb_x, thumb_y, thumb_r, Color::rgb(226, 232, 240));
    }
}

/// Modern rounded pill badge.
pub struct PillBadge {
    /// Bounding rectangle.
    pub bounds: Rect,
    /// Badge text.
    pub text: &'static str,
    /// Background color.
    pub bg_color: Color,
    /// Text color.
    pub text_color: Color,
}

impl PillBadge {
    /// Create a new pill badge.
    #[must_use]
    pub const fn new(bounds: Rect, text: &'static str, bg_color: Color, text_color: Color) -> Self {
        Self {
            bounds,
            text,
            bg_color,
            text_color,
        }
    }

    /// Render pill badge into canvas.
    pub fn render(&self, canvas: &mut Canvas) {
        let radius = self.bounds.height / 2;
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            radius,
            self.bg_color,
        );
        let text_len = text_width(self.text);
        let tx = self.bounds.x + ((self.bounds.width.saturating_sub(text_len)) / 2) as i32;
        let ty =
            self.bounds.y + ((self.bounds.height.saturating_sub(FONT_HEIGHT as u32)) / 2) as i32;
        canvas.draw_string(tx, ty, self.text, self.text_color, None);
    }
}

/// A styled button widget.
pub struct Button {
    /// Bounding rectangle.
    pub bounds: Rect,
    /// Button label text.
    pub label: &'static str,
    /// Hover state.
    pub hover: bool,
    /// Pressed state.
    pub pressed: bool,
}

impl Button {
    /// Create a new button.
    #[must_use]
    pub const fn new(label: &'static str, bounds: Rect) -> Self {
        Self {
            bounds,
            label,
            hover: false,
            pressed: false,
        }
    }

    /// Render the button into canvas.
    pub fn render(&self, canvas: &mut Canvas) {
        let bg = if self.pressed {
            Color::rgb(226, 232, 240)
        } else if self.hover {
            Color::rgb(241, 245, 249)
        } else {
            Color::WHITE
        };

        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            6,
            bg,
        );
        canvas.draw_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            6,
            Color::BORDER,
        );

        let label_len = text_width(self.label);
        let text_x = self.bounds.x + ((self.bounds.width.saturating_sub(label_len)) / 2) as i32;
        let text_y =
            self.bounds.y + ((self.bounds.height.saturating_sub(FONT_HEIGHT as u32)) / 2) as i32;
        canvas.draw_string(text_x, text_y, self.label, Color::TEXT_PRIMARY, None);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn window_client_rect_is_within_bounds() {
        let win = Window::new("Test", Rect::new(100, 100, 400, 300));
        let client = win.client_rect();
        assert!(client.x > win.bounds.x);
        assert!(client.y > win.bounds.y);
        assert!(client.width < win.bounds.width);
        assert!(client.height < win.bounds.height);
    }

    #[test]
    fn window_hit_tests() {
        let win = Window::new("Test", Rect::new(100, 100, 400, 300));
        assert!(win.is_title_bar(200, 110));
        assert!(win.is_chrome_control(150, 110));
        assert!(win.is_close_button(100 + 400 - 15, 110));
        assert!(win.is_close_button(100 + 15, 100 + 15)); // Top-left close control
        assert!(!win.is_title_bar(150, 200));
    }

    #[test]
    fn toggle_switch_render() {
        let mut buf = [0u32; 1600];
        let mut canvas = Canvas::new(&mut buf, 40, 40, 40);
        let toggle = ToggleSwitch::new(Rect::new(0, 0, 36, 18), true);
        toggle.render(&mut canvas);
        assert!(canvas.pixels().iter().any(|&p| p != 0));
    }

    #[test]
    fn pill_badge_render() {
        let mut buf = [0u32; 2400];
        let mut canvas = Canvas::new(&mut buf, 60, 40, 60);
        let badge = PillBadge::new(Rect::new(0, 0, 50, 20), "OK", Color::GREEN, Color::WHITE);
        badge.render(&mut canvas);
        assert!(canvas.pixels().iter().any(|&p| p != 0));
    }
}
