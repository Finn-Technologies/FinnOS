//! Flutter-inspired styling and decoration model for Peony: `BorderRadius`, `BoxShadow`, `BoxDecoration`, and `TextStyle`.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss
)]

use crate::canvas::{Canvas, Color, Rect};
use crate::font::{FontSize, text_width, title_width};
use crate::layout::Offset;

/// Corner radii for styling rectangles with rounded edges.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct BorderRadius {
    /// Top-left corner radius.
    pub top_left: u32,
    /// Top-right corner radius.
    pub top_right: u32,
    /// Bottom-right corner radius.
    pub bottom_right: u32,
    /// Bottom-left corner radius.
    pub bottom_left: u32,
}

impl BorderRadius {
    /// Zero corner radius (sharp corners).
    pub const ZERO: Self = Self {
        top_left: 0,
        top_right: 0,
        bottom_right: 0,
        bottom_left: 0,
    };

    /// Create uniform circular corners with the given radius.
    #[must_use]
    pub const fn circular(radius: u32) -> Self {
        Self {
            top_left: radius,
            top_right: radius,
            bottom_right: radius,
            bottom_left: radius,
        }
    }

    /// Create specific radii for each corner.
    #[must_use]
    pub const fn only(top_left: u32, top_right: u32, bottom_right: u32, bottom_left: u32) -> Self {
        Self {
            top_left,
            top_right,
            bottom_right,
            bottom_left,
        }
    }

    /// Maximum radius among all corners.
    #[must_use]
    pub const fn max_radius(&self) -> u32 {
        let a = if self.top_left > self.top_right {
            self.top_left
        } else {
            self.top_right
        };
        let b = if self.bottom_left > self.bottom_right {
            self.bottom_left
        } else {
            self.bottom_right
        };
        if a > b { a } else { b }
    }
}

/// Elevation shadow specification for depth and elevation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BoxShadow {
    /// Shadow color (with alpha).
    pub color: Color,
    /// Offset of shadow relative to box.
    pub offset: Offset,
    /// Blur radius.
    pub blur_radius: u32,
}

impl BoxShadow {
    /// Standard subtle drop shadow for elevated cards and windows.
    #[must_use]
    pub const fn elevation_card() -> Self {
        Self {
            color: Color::rgba(0, 0, 0, 36),
            offset: Offset::new(0, 4),
            blur_radius: 12,
        }
    }

    /// Deep diffused shadow for floating modals and windows.
    #[must_use]
    pub const fn elevation_modal() -> Self {
        Self {
            color: Color::rgba(0, 0, 0, 64),
            offset: Offset::new(0, 8),
            blur_radius: 24,
        }
    }
}

/// Visual styling configuration for a container box.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct BoxDecoration {
    /// Background fill color.
    pub color: Option<Color>,
    /// Rounded corner radius.
    pub border_radius: BorderRadius,
    /// Border color.
    pub border_color: Option<Color>,
    /// Border width in pixels.
    pub border_width: u32,
    /// Drop shadow.
    pub shadow: Option<BoxShadow>,
}

impl BoxDecoration {
    /// Paint this decoration onto a canvas covering `rect`.
    pub fn paint(&self, canvas: &mut Canvas, rect: Rect) {
        let r = self.border_radius.max_radius();

        // 1. Drop shadow
        if let Some(_sh) = self.shadow
            && r > 0
        {
            canvas.draw_soft_shadow(rect.x, rect.y, rect.width, rect.height, r);
        }

        // 2. Background fill
        if let Some(bg) = self.color {
            if r > 0 {
                canvas.fill_rounded_rect(rect.x, rect.y, rect.width, rect.height, r, bg);
            } else {
                canvas.fill_rect(rect.x, rect.y, rect.width, rect.height, bg);
            }
        }

        // 3. Border stroke
        if let Some(border) = self.border_color
            && self.border_width > 0
        {
            if r > 0 {
                canvas.draw_rounded_rect(rect.x, rect.y, rect.width, rect.height, r, border);
            } else {
                canvas.draw_rect(rect.x, rect.y, rect.width, rect.height, border);
            }
        }
    }
}

/// Font weight choices for typography.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub enum FontWeight {
    /// Regular font weight (400).
    #[default]
    Regular,
    /// Semibold font weight (600).
    Semibold,
    /// Bold font weight (700).
    Bold,
}

/// Text styling configuration.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TextStyle {
    /// Color of the glyph ink.
    pub color: Color,
    /// Font size selector (Text 16px, Title 20px).
    pub size: FontSize,
    /// Weight (regular or semibold double-strike).
    pub weight: FontWeight,
}

impl TextStyle {
    /// Prominent page or dialog heading style (20px Semibold).
    #[must_use]
    pub const fn heading(color: Color) -> Self {
        Self {
            color,
            size: FontSize::Title,
            weight: FontWeight::Semibold,
        }
    }

    /// Section or card title style (16px Semibold).
    #[must_use]
    pub const fn title(color: Color) -> Self {
        Self {
            color,
            size: FontSize::Text,
            weight: FontWeight::Semibold,
        }
    }

    /// Standard body text style (16px Regular).
    #[must_use]
    pub const fn body(color: Color) -> Self {
        Self {
            color,
            size: FontSize::Text,
            weight: FontWeight::Regular,
        }
    }

    /// Subtitle or secondary caption style (16px Regular muted).
    #[must_use]
    pub const fn caption() -> Self {
        Self {
            color: Color::TEXT_MUTED,
            size: FontSize::Text,
            weight: FontWeight::Regular,
        }
    }

    /// Measure width of a string formatted with this style.
    #[must_use]
    pub fn measure_width(&self, s: &str) -> u32 {
        let base = match self.size {
            FontSize::Text => text_width(s),
            FontSize::Title => title_width(s),
        };
        if matches!(self.weight, FontWeight::Semibold | FontWeight::Bold) {
            base.saturating_add(1)
        } else {
            base
        }
    }

    /// Render a string on canvas using this text style.
    pub fn draw(&self, canvas: &mut Canvas, x: i32, y: i32, s: &str) {
        let strong = matches!(self.weight, FontWeight::Semibold | FontWeight::Bold);
        match self.size {
            FontSize::Text => {
                if strong {
                    canvas.draw_string_strong(x, y, s, self.color, None);
                } else {
                    canvas.draw_string(x, y, s, self.color, None);
                }
            }
            FontSize::Title => {
                if strong {
                    canvas.draw_title_strong(x, y, s, self.color);
                } else {
                    canvas.draw_title(x, y, s, self.color);
                }
            }
        }
    }
}
