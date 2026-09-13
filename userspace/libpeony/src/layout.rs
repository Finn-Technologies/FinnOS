//! Flutter-inspired layout primitives and constraints model for Peony.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::many_single_char_names
)]

use crate::canvas::Rect;

/// Insets representing padding or margin around a rectangular box.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct EdgeInsets {
    /// Top inset.
    pub top: i32,
    /// Right inset.
    pub right: i32,
    /// Bottom inset.
    pub bottom: i32,
    /// Left inset.
    pub left: i32,
}

impl EdgeInsets {
    /// Zero insets.
    pub const ZERO: Self = Self {
        top: 0,
        right: 0,
        bottom: 0,
        left: 0,
    };

    /// Create uniform insets on all 4 sides.
    #[must_use]
    pub const fn all(val: i32) -> Self {
        Self {
            top: val,
            right: val,
            bottom: val,
            left: val,
        }
    }

    /// Create symmetric horizontal and vertical insets.
    #[must_use]
    pub const fn symmetric(horizontal: i32, vertical: i32) -> Self {
        Self {
            top: vertical,
            right: horizontal,
            bottom: vertical,
            left: horizontal,
        }
    }

    /// Create custom insets for each individual edge.
    #[must_use]
    pub const fn only(top: i32, right: i32, bottom: i32, left: i32) -> Self {
        Self {
            top,
            right,
            bottom,
            left,
        }
    }

    /// Total horizontal insets (`left + right`).
    #[must_use]
    pub const fn horizontal(&self) -> i32 {
        self.left + self.right
    }

    /// Total vertical insets (`top + bottom`).
    #[must_use]
    pub const fn vertical(&self) -> i32 {
        self.top + self.bottom
    }

    /// Deflate a rectangle by subtracting these insets from its edges.
    #[must_use]
    pub const fn deflate_rect(&self, r: Rect) -> Rect {
        let x = r.x + self.left;
        let y = r.y + self.top;
        let h_pad = if self.horizontal() > 0 {
            self.horizontal() as u32
        } else {
            0
        };
        let v_pad = if self.vertical() > 0 {
            self.vertical() as u32
        } else {
            0
        };
        let w = r.width.saturating_sub(h_pad);
        let h = r.height.saturating_sub(v_pad);
        Rect::new(x, y, w, h)
    }
}

/// 2D size with width and height.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct Size {
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
}

impl Size {
    /// Zero size.
    pub const ZERO: Self = Self {
        width: 0,
        height: 0,
    };

    /// Create a new size.
    #[must_use]
    pub const fn new(width: u32, height: u32) -> Self {
        Self { width, height }
    }
}

/// 2D coordinate offset.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub struct Offset {
    /// Horizontal offset in pixels.
    pub dx: i32,
    /// Vertical offset in pixels.
    pub dy: i32,
}

impl Offset {
    /// Zero offset.
    pub const ZERO: Self = Self { dx: 0, dy: 0 };

    /// Create a new offset.
    #[must_use]
    pub const fn new(dx: i32, dy: i32) -> Self {
        Self { dx, dy }
    }
}

/// Box constraints establishing min/max boundaries for layout sizing.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BoxConstraints {
    /// Minimum allowed width.
    pub min_width: u32,
    /// Maximum allowed width.
    pub max_width: u32,
    /// Minimum allowed height.
    pub min_height: u32,
    /// Maximum allowed height.
    pub max_height: u32,
}

impl BoxConstraints {
    /// Create tight constraints enforcing an exact size.
    #[must_use]
    pub const fn tight(size: Size) -> Self {
        Self {
            min_width: size.width,
            max_width: size.width,
            min_height: size.height,
            max_height: size.height,
        }
    }

    /// Create loose constraints with minimums at zero and maximums at `size`.
    #[must_use]
    pub const fn loose(size: Size) -> Self {
        Self {
            min_width: 0,
            max_width: size.width,
            min_height: 0,
            max_height: size.height,
        }
    }

    /// Constrain a given width within `min_width..=max_width`.
    #[must_use]
    pub const fn constrain_width(&self, width: u32) -> u32 {
        if width < self.min_width {
            self.min_width
        } else if width > self.max_width {
            self.max_width
        } else {
            width
        }
    }

    /// Constrain a given height within `min_height..=max_height`.
    #[must_use]
    pub const fn constrain_height(&self, height: u32) -> u32 {
        if height < self.min_height {
            self.min_height
        } else if height > self.max_height {
            self.max_height
        } else {
            height
        }
    }

    /// Constrain a given size to satisfy these boundaries.
    #[must_use]
    pub const fn constrain(&self, size: Size) -> Size {
        Size {
            width: self.constrain_width(size.width),
            height: self.constrain_height(size.height),
        }
    }
}

/// Alignment positions within a container.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub enum Alignment {
    /// Top-left alignment.
    #[default]
    TopLeft,
    /// Top-center alignment.
    TopCenter,
    /// Top-right alignment.
    TopRight,
    /// Center-left alignment.
    CenterLeft,
    /// Exact center alignment.
    Center,
    /// Center-right alignment.
    CenterRight,
    /// Bottom-left alignment.
    BottomLeft,
    /// Bottom-center alignment.
    BottomCenter,
    /// Bottom-right alignment.
    BottomRight,
}

impl Alignment {
    /// Compute the top-left coordinate of a child within a parent rectangle.
    #[must_use]
    pub const fn align_child(&self, parent: Rect, child: Size) -> Offset {
        let parent_w = parent.width as i32;
        let parent_h = parent.height as i32;
        let child_w = child.width as i32;
        let child_h = child.height as i32;

        let (dx, dy) = match self {
            Self::TopLeft => (0, 0),
            Self::TopCenter => ((parent_w - child_w) / 2, 0),
            Self::TopRight => (parent_w - child_w, 0),
            Self::CenterLeft => (0, (parent_h - child_h) / 2),
            Self::Center => ((parent_w - child_w) / 2, (parent_h - child_h) / 2),
            Self::CenterRight => (parent_w - child_w, (parent_h - child_h) / 2),
            Self::BottomLeft => (0, parent_h - child_h),
            Self::BottomCenter => ((parent_w - child_w) / 2, parent_h - child_h),
            Self::BottomRight => (parent_w - child_w, parent_h - child_h),
        };

        Offset::new(parent.x + dx, parent.y + dy)
    }
}

/// Main axis alignment for Flex (Row and Column).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub enum MainAxisAlignment {
    /// Place children at the start.
    #[default]
    Start,
    /// Place children in the center.
    Center,
    /// Place children at the end.
    End,
    /// Space evenly between children.
    SpaceBetween,
}

/// Cross axis alignment for Flex (Row and Column).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Default)]
pub enum CrossAxisAlignment {
    /// Align children at the cross-axis start.
    #[default]
    Start,
    /// Center children along the cross axis.
    Center,
    /// Align children at the cross-axis end.
    End,
    /// Stretch children across the cross axis.
    Stretch,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insets_deflate_rect() {
        let r = Rect::new(10, 20, 100, 80);
        let insets = EdgeInsets::all(10);
        let inner = insets.deflate_rect(r);
        assert_eq!(inner.x, 20);
        assert_eq!(inner.y, 30);
        assert_eq!(inner.width, 80);
        assert_eq!(inner.height, 60);
    }

    #[test]
    fn constraints_clamping() {
        let c = BoxConstraints {
            min_width: 50,
            max_width: 100,
            min_height: 20,
            max_height: 40,
        };
        assert_eq!(c.constrain_width(20), 50);
        assert_eq!(c.constrain_width(150), 100);
        assert_eq!(c.constrain_width(75), 75);
    }

    #[test]
    fn alignment_centering() {
        let parent = Rect::new(0, 0, 100, 100);
        let child = Size::new(20, 20);
        let offset = Alignment::Center.align_child(parent, child);
        assert_eq!(offset.dx, 40);
        assert_eq!(offset.dy, 40);
    }
}
