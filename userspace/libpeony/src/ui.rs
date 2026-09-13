//! Flutter-inspired declarative component library for Peony.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::similar_names
)]

use crate::canvas::{Canvas, Color, Rect};
use crate::decoration::{BorderRadius, BoxDecoration, TextStyle};
use crate::font::FONT_HEIGHT;
use crate::layout::EdgeInsets;

/// Modern Flutter-style Card component.
pub struct Card {
    /// Bounding rectangle of the card.
    pub bounds: Rect,
    /// Decoration style.
    pub decoration: BoxDecoration,
    /// Inner padding.
    pub padding: EdgeInsets,
}

impl Card {
    /// Create a clean elevated card with white background and rounded corners.
    #[must_use]
    pub const fn new(bounds: Rect) -> Self {
        Self {
            bounds,
            decoration: BoxDecoration {
                color: Some(Color::rgb(30, 30, 46)),
                border_radius: BorderRadius::circular(12),
                border_color: Some(Color::rgb(49, 50, 68)),
                border_width: 1,
                shadow: None,
            },
            padding: EdgeInsets::all(16),
        }
    }

    /// Create a frosted translucent card for dark or acrylic surfaces.
    #[must_use]
    pub const fn frosted(bounds: Rect) -> Self {
        Self {
            bounds,
            decoration: BoxDecoration {
                color: Some(Color::rgba(255, 255, 255, 30)),
                border_radius: BorderRadius::circular(12),
                border_color: Some(Color::rgba(255, 255, 255, 45)),
                border_width: 1,
                shadow: None,
            },
            padding: EdgeInsets::all(14),
        }
    }

    /// Inner content rectangle after applying padding.
    #[must_use]
    pub const fn content_rect(&self) -> Rect {
        self.padding.deflate_rect(self.bounds)
    }

    /// Paint the card background and border.
    pub fn paint(&self, canvas: &mut Canvas) {
        self.decoration.paint(canvas, self.bounds);
    }
}

/// A modern Flutter-style `ListTile` with title, optional subtitle, and trailing switch or badge.
pub struct ListTile<'a> {
    /// Primary title string.
    pub title: &'a str,
    /// Optional subtitle string.
    pub subtitle: Option<&'a str>,
}

impl<'a> ListTile<'a> {
    /// Create a new list tile with a title.
    #[must_use]
    pub const fn new(title: &'a str) -> Self {
        Self {
            title,
            subtitle: None,
        }
    }

    /// Add a subtitle description.
    #[must_use]
    pub const fn with_subtitle(mut self, subtitle: &'a str) -> Self {
        self.subtitle = Some(subtitle);
        self
    }

    /// Render this list tile into `rect` on canvas.
    pub fn render(&self, canvas: &mut Canvas, rect: Rect) {
        let title_y = if self.subtitle.is_some() {
            rect.y + 2
        } else {
            rect.y + ((rect.height as i32 - FONT_HEIGHT as i32) / 2)
        };

        let title_style = TextStyle::title(Color::rgb(205, 214, 244));
        title_style.draw(canvas, rect.x, title_y, self.title);

        if let Some(sub) = self.subtitle {
            let sub_y = title_y + FONT_HEIGHT as i32 + 2;
            let sub_style = TextStyle::caption();
            sub_style.draw(canvas, rect.x, sub_y, sub);
        }
    }
}

/// Modern pill-shaped toggle switch (Cupertino / Material style).
pub struct Switch {
    /// Bounding rectangle (typically 44x24 or 36x18).
    pub bounds: Rect,
    /// Active state.
    pub active: bool,
}

impl Switch {
    /// Create a new switch.
    #[must_use]
    pub const fn new(bounds: Rect, active: bool) -> Self {
        Self { bounds, active }
    }

    /// Check whether a click hit this switch.
    #[must_use]
    pub const fn hit_test(&self, px: i32, py: i32) -> bool {
        self.bounds.contains(px, py)
    }

    /// Paint the switch with track, active fill, and thumb.
    pub fn paint(&self, canvas: &mut Canvas) {
        let r = self.bounds.height / 2;
        let track_col = if self.active {
            Color::rgb(16, 185, 129) // Emerald 500
        } else {
            Color::rgb(203, 213, 225) // Slate 300
        };

        // Track fill & border
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            r,
            track_col,
        );
        canvas.draw_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            r,
            Color::rgba(0, 0, 0, 20),
        );

        // Thumb pill / circle
        let thumb_margin = 2;
        let thumb_r = (self.bounds.height.saturating_sub(thumb_margin * 2)) / 2;
        let thumb_x = if self.active {
            self.bounds.x + self.bounds.width as i32 - thumb_r as i32 - thumb_margin as i32
        } else {
            self.bounds.x + thumb_r as i32 + thumb_margin as i32
        };
        let thumb_y = self.bounds.y + r as i32;

        canvas.fill_circle(thumb_x, thumb_y, thumb_r as i32, Color::WHITE);
        canvas.draw_circle(thumb_x, thumb_y, thumb_r as i32, Color::rgba(0, 0, 0, 30));
    }
}

/// Search capsule widget with magnifying glass icon and placeholder.
pub struct SearchField<'a> {
    /// Bounding rectangle.
    pub bounds: Rect,
    /// Placeholder text string.
    pub placeholder: &'a str,
}

impl<'a> SearchField<'a> {
    /// Create a new search field.
    #[must_use]
    pub const fn new(bounds: Rect, placeholder: &'a str) -> Self {
        Self {
            bounds,
            placeholder,
        }
    }

    /// Paint the search capsule.
    pub fn paint(&self, canvas: &mut Canvas) {
        let r = self.bounds.height / 2;
        canvas.fill_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            r,
            Color::rgb(30, 30, 46),
        );
        canvas.draw_rounded_rect(
            self.bounds.x,
            self.bounds.y,
            self.bounds.width,
            self.bounds.height,
            r,
            Color::rgb(49, 50, 68),
        );

        // Magnifying glass icon
        let center_x = self.bounds.x + 14;
        let center_y = self.bounds.y + (self.bounds.height as i32 / 2);
        canvas.draw_circle(center_x, center_y - 1, 4, Color::rgb(147, 153, 178));
        canvas.draw_aa_line(
            center_x + 3,
            center_y + 2,
            center_x + 6,
            center_y + 5,
            Color::rgb(147, 153, 178),
        );

        // Placeholder text
        let text_x = self.bounds.x + 26;
        let text_y = self.bounds.y + ((self.bounds.height as i32 - FONT_HEIGHT as i32) / 2);
        canvas.draw_string(
            text_x,
            text_y,
            self.placeholder,
            Color::rgb(147, 153, 178),
            None,
        );
    }
}

/// Pill navigation item in sidebars (active or inactive).
pub struct NavItem<'a> {
    /// Label string.
    pub label: &'a str,
    /// Active / selected state.
    pub active: bool,
}

impl<'a> NavItem<'a> {
    /// Create a new navigation item.
    #[must_use]
    pub const fn new(label: &'a str, active: bool) -> Self {
        Self { label, active }
    }

    /// Paint the navigation item in `rect`.
    pub fn paint(&self, canvas: &mut Canvas, rect: Rect) {
        let r = 8;
        if self.active {
            canvas.fill_rounded_rect(
                rect.x,
                rect.y,
                rect.width,
                rect.height,
                r,
                Color::rgb(59, 130, 246),
            );
            let text_style = TextStyle::title(Color::WHITE);
            let ty = rect.y + ((rect.height as i32 - FONT_HEIGHT as i32) / 2);
            text_style.draw(canvas, rect.x + 10, ty, self.label);
        } else {
            let text_style = TextStyle::body(Color::rgb(166, 173, 200));
            let ty = rect.y + ((rect.height as i32 - FONT_HEIGHT as i32) / 2);
            text_style.draw(canvas, rect.x + 10, ty, self.label);
        }
    }
}

/// Horizontal divider line with custom color and thickness.
pub struct Divider {
    /// Line color.
    pub color: Color,
    /// Thickness in pixels.
    pub thickness: u32,
}

impl Divider {
    /// Create a standard divider with 1px thickness.
    #[must_use]
    pub const fn new(color: Color) -> Self {
        Self {
            color,
            thickness: 1,
        }
    }

    /// Paint the horizontal divider across the given width.
    pub fn paint(&self, canvas: &mut Canvas, x: i32, y: i32, width: u32) {
        canvas.fill_rect(x, y, width, self.thickness, self.color);
    }
}

/// Rounded status pill badge (Flutter Chip / Badge style).
pub struct Chip<'a> {
    /// Chip text.
    pub label: &'a str,
    /// Background fill color.
    pub bg_color: Color,
    /// Text color.
    pub text_color: Color,
}

impl<'a> Chip<'a> {
    /// Create a new Chip.
    #[must_use]
    pub const fn new(label: &'a str, bg_color: Color, text_color: Color) -> Self {
        Self {
            label,
            bg_color,
            text_color,
        }
    }

    /// Paint the Chip and return its total width.
    pub fn paint(&self, canvas: &mut Canvas, x: i32, y: i32) -> u32 {
        let text_w = Canvas::string_width(self.label);
        let chip_w = text_w + 16;
        let chip_h = 18;
        let r = chip_h / 2;
        canvas.fill_rounded_rect(x, y, chip_w, chip_h, r, self.bg_color);
        canvas.draw_string(x + 8, y + 2, self.label, self.text_color, None);
        chip_w
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_card_content_rect() {
        let card = Card::new(Rect::new(10, 10, 100, 80));
        let inner = card.content_rect();
        assert_eq!(inner.x, 26);
        assert_eq!(inner.y, 26);
        assert_eq!(inner.width, 68);
        assert_eq!(inner.height, 48);
    }

    #[test]
    fn test_switch_hit_test() {
        let sw = Switch::new(Rect::new(20, 20, 44, 24), true);
        assert!(sw.hit_test(25, 25));
        assert!(!sw.hit_test(10, 10));
        assert!(!sw.hit_test(70, 25));
    }
}
