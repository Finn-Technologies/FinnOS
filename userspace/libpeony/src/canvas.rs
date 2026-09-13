//! 2D graphics canvas, color types, and rendering primitives.

#![allow(
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_sign_loss,
    clippy::cast_lossless,
    clippy::similar_names,
    clippy::many_single_char_names
)]

use crate::font::{TEXT_ASCENT, TEXT_DATA, TITLE_ASCENT, TITLE_DATA, lookup_text, lookup_title};

/// 32-bit RGBA color representation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Color {
    /// Red channel [0, 255].
    pub r: u8,
    /// Green channel [0, 255].
    pub g: u8,
    /// Blue channel [0, 255].
    pub b: u8,
    /// Alpha channel [0 = transparent, 255 = opaque].
    pub a: u8,
}

impl Color {
    /// Create a new RGBA color.
    #[must_use]
    pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> Self {
        Self { r, g, b, a }
    }

    /// Create an opaque RGB color.
    #[must_use]
    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b, a: 255 }
    }

    /// Black (#000000).
    pub const BLACK: Self = Self::rgb(0, 0, 0);
    /// White (#FFFFFF).
    pub const WHITE: Self = Self::rgb(255, 255, 255);
    /// Pure Red (#FF0000).
    pub const RED: Self = Self::rgb(255, 0, 0);
    /// Pure Green (#00FF00).
    pub const GREEN: Self = Self::rgb(0, 255, 0);
    /// Pure Blue (#0000FF).
    pub const BLUE: Self = Self::rgb(0, 0, 255);

    // Light Mode Theme Colors:
    /// Light Desktop Background (#ECEFF1).
    pub const DESKTOP_BG: Self = Self::rgb(236, 239, 241);
    /// Desktop Background alias for compatibility.
    pub const DARK_BG: Self = Self::DESKTOP_BG;
    /// Panel Background (#FFFFFF).
    pub const PANEL_BG: Self = Self::rgb(255, 255, 255);
    /// Window Background (#FFFFFF).
    pub const WINDOW_BG: Self = Self::rgb(255, 255, 255);
    /// Window Title Bar Active (#F1F3F5).
    pub const TITLEBAR_ACTIVE: Self = Self::rgb(241, 243, 245);
    /// Window Title Bar Inactive (#F8F9FA).
    pub const TITLEBAR_INACTIVE: Self = Self::rgb(248, 249, 250);
    /// Window Title Bar Divider (#E9ECEF).
    pub const TITLEBAR_DIVIDER: Self = Self::rgb(233, 236, 239);
    /// Accent / `FinnOS` Blue (#2563EB).
    pub const ACCENT_BLUE: Self = Self::rgb(37, 99, 235);
    /// Accent Green (#16A34A).
    pub const ACCENT_GREEN: Self = Self::rgb(22, 163, 74);
    /// Accent Red / Close button (#EF4444).
    pub const ACCENT_RED: Self = Self::rgb(239, 68, 68);
    /// Primary Dark Text (#1E293B).
    pub const TEXT_PRIMARY: Self = Self::rgb(30, 41, 59);
    /// Text Light (aliased to dark text for contrast in light mode).
    pub const TEXT_LIGHT: Self = Self::TEXT_PRIMARY;
    /// Text Muted (#64748B).
    pub const TEXT_MUTED: Self = Self::rgb(100, 116, 139);
    /// Border Gray (#D1D5DB).
    pub const BORDER: Self = Self::rgb(209, 213, 219);
    /// Fully Transparent.
    pub const TRANSPARENT: Self = Self::rgba(0, 0, 0, 0);

    // Modern macOS / GNOME / Fluent Design Theme Tokens:
    /// Modern Deep Twilight Desktop Wallpaper Gradient Start (#0F172A - Slate 900).
    pub const WALLPAPER_TOP: Self = Self::rgb(15, 23, 42);
    /// Modern Deep Twilight Desktop Wallpaper Gradient Mid (#1E1B4B - Indigo 950).
    pub const WALLPAPER_MID: Self = Self::rgb(30, 27, 75);
    /// Modern Deep Twilight Desktop Wallpaper Gradient Bottom (#0E7490 - Cyan 700).
    pub const WALLPAPER_BOTTOM: Self = Self::rgb(14, 116, 144);

    /// Frosted Glass Top Bar (#F8FAFC with alpha 235).
    pub const TOPBAR_BG: Self = Self::rgba(248, 250, 252, 235);
    /// Top Bar Divider (#E2E8F0).
    pub const TOPBAR_BORDER: Self = Self::rgb(226, 232, 240);

    /// Modern Dock Frosted Acrylic Background (#FFFFFF with alpha 195).
    pub const DOCK_BG: Self = Self::rgba(255, 255, 255, 195);
    /// Modern Dock Border (#CBD5E1 with alpha 180).
    pub const DOCK_BORDER: Self = Self::rgba(203, 213, 225, 180);

    /// macOS Traffic Light Close Button (#FF5F56).
    pub const TRAFFIC_CLOSE: Self = Self::rgb(255, 95, 86);
    /// macOS Traffic Light Minimize Button (#FFBD2E).
    pub const TRAFFIC_MINIMIZE: Self = Self::rgb(255, 189, 46);
    /// macOS Traffic Light Zoom Button (#27C93F).
    pub const TRAFFIC_ZOOM: Self = Self::rgb(39, 201, 63);

    /// Modern Window Frame Border (#E2E8F0).
    pub const WINDOW_BORDER_MODERN: Self = Self::rgb(226, 232, 240);
    /// Modern Card Background (#F8FAFC).
    pub const CARD_BG: Self = Self::rgb(248, 250, 252);
    /// Modern Card Border (#E2E8F0).
    pub const CARD_BORDER: Self = Self::rgb(226, 232, 240);

    /// Modern Terminal Background (#181825 - Catppuccin Mantle).
    pub const TERMINAL_BG: Self = Self::rgb(24, 24, 37);
    /// Modern Terminal Text (#CDD6F4 - Catppuccin Text).
    pub const TERMINAL_TEXT: Self = Self::rgb(205, 214, 244);
    /// Modern Terminal Prompt Blue (#89B4FA).
    pub const TERMINAL_PROMPT: Self = Self::rgb(137, 180, 250);
    /// Modern Terminal Success Green (#A6E3A1).
    pub const TERMINAL_GREEN: Self = Self::rgb(166, 227, 161);
    /// Modern Terminal Warning Yellow (#F9E2AF).
    pub const TERMINAL_YELLOW: Self = Self::rgb(249, 226, 175);
    /// Modern Terminal Header Bar (#11111B - Catppuccin Crust).
    pub const TERMINAL_HEADER: Self = Self::rgb(17, 17, 27);
    /// Modern Terminal Inactive Tab.
    pub const TERMINAL_TAB_INACTIVE: Self = Self::rgb(15, 15, 23);
    /// Specular glass top highlight line.
    pub const GLASS_SPECULAR: Self = Self::rgba(255, 255, 255, 55);
    /// Emerald Green for active GPU hardware status.
    pub const GPU_EMERALD: Self = Self::rgb(16, 185, 129);
    /// Battery meter full green.
    pub const BATTERY_GREEN: Self = Self::rgb(34, 197, 94);
    /// Storage drive used bar (Indigo 500).
    pub const STORAGE_USED: Self = Self::rgb(99, 102, 241);

    // Authentic FinnOS Figma tokens (Desktop page 44:2, Branding 0:1):
    /// Bottom taskbar frosted acrylic (`rgba(0,0,0,0.5)` + 25px background blur).
    pub const TASKBAR_BG: Self = Self::rgba(0, 0, 0, 128);
    /// Taskbar top hairline highlight (`rgba(255,255,255,0.16)`).
    pub const TASKBAR_HIGHLIGHT: Self = Self::rgba(255, 255, 255, 40);
    /// Centered dock container (transparent; icons carry the color).
    pub const DOCK_BG_FIGMA: Self = Self::rgba(255, 255, 255, 0);
    /// Start Menu dark acrylic card (`rgba(0,0,0,0.5)`, rx 24).
    pub const START_MENU_BG: Self = Self::rgba(18, 18, 22, 150);
    /// Control Centre dark acrylic popup.
    pub const CONTROL_CENTRE_BG: Self = Self::rgba(28, 28, 32, 175);
    /// Settings sidebar frosted glass (`rgba(255,255,255,0.5)` + 50px blur).
    pub const SIDEBAR_FROST: Self = Self::rgba(248, 250, 252, 200);
    /// Settings content pure white card with 20px outer radius.
    pub const SETTINGS_CONTENT_BG: Self = Self::rgb(255, 255, 255);
    /// Rounded info cards (#E5E7EB).
    pub const INFO_CARD_BG: Self = Self::rgb(229, 231, 235);
    /// Info card border (#D1D5DB).
    pub const INFO_CARD_BORDER: Self = Self::rgb(209, 213, 219);
    /// Monochrome window control line (near-black, not macOS traffic lights).
    pub const CHROME_LINE: Self = Self::rgb(30, 41, 59);
    /// Monochrome window control line on dark surfaces.
    pub const CHROME_LINE_LIGHT: Self = Self::rgb(226, 232, 240);
    /// Search pill background.
    pub const SEARCH_PILL_BG: Self = Self::rgb(241, 245, 249);
    /// User avatar blue (profile silhouette).
    pub const AVATAR_BLUE: Self = Self::rgb(59, 130, 246);
    /// Desert wallpaper sky top (warm dusk blue sampled from Figma 71:47).
    pub const DESERT_SKY_TOP: Self = Self::rgb(122, 162, 205);
    /// Desert wallpaper sky mid (peach horizon).
    pub const DESERT_SKY_MID: Self = Self::rgb(232, 190, 158);
    /// Desert wallpaper dune far (mauve).
    pub const DESERT_DUNE_FAR: Self = Self::rgb(176, 132, 128);
    /// Desert wallpaper dune near (burnt sand).
    pub const DESERT_DUNE_NEAR: Self = Self::rgb(150, 98, 84);
    /// Desert wallpaper sand foreground.
    pub const DESERT_SAND: Self = Self::rgb(196, 148, 112);
    /// Desert sun disc (warm white).
    pub const DESERT_SUN: Self = Self::rgb(255, 244, 224);
    /// Files folder warm yellow (Branding 69:2556).
    pub const FOLDER_YELLOW: Self = Self::rgb(251, 191, 36);
    /// Files folder deep orange shade.
    pub const FOLDER_ORANGE: Self = Self::rgb(217, 119, 6);
    /// Browser globe blue (Branding 69:2545).
    pub const GLOBE_BLUE: Self = Self::rgb(37, 99, 235);
    /// Browser globe deep navy.
    pub const GLOBE_NAVY: Self = Self::rgb(30, 58, 138);
    /// Terminal slate gradient top (Branding 69:2561).
    pub const TERMINAL_SLATE_TOP: Self = Self::rgb(51, 65, 85);
    /// Terminal slate gradient bottom.
    pub const TERMINAL_SLATE_BOTTOM: Self = Self::rgb(15, 23, 42);

    /// Convert color to packed 32-bit pixel (ARGB / BGRA depending on byte layout).
    #[must_use]
    pub const fn to_u32(self) -> u32 {
        // Linear 32bpp BGRX format standard in UEFI GOP and QEMU displays
        (self.b as u32) | ((self.g as u32) << 8) | ((self.r as u32) << 16) | ((self.a as u32) << 24)
    }

    /// Alpha blend `src` over `self` (background).
    #[must_use]
    pub fn blend_over(self, src: Self) -> Self {
        if src.a == 255 {
            return src;
        }
        if src.a == 0 {
            return self;
        }
        let alpha = u32::from(src.a);
        let inv_alpha = 255 - alpha;
        let r = ((u32::from(src.r) * alpha + u32::from(self.r) * inv_alpha) / 255) as u8;
        let g = ((u32::from(src.g) * alpha + u32::from(self.g) * inv_alpha) / 255) as u8;
        let b = ((u32::from(src.b) * alpha + u32::from(self.b) * inv_alpha) / 255) as u8;
        Self { r, g, b, a: 255 }
    }
}

/// 2D Rectangle definition.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Rect {
    /// X coordinate of top-left corner.
    pub x: i32,
    /// Y coordinate of top-left corner.
    pub y: i32,
    /// Width in pixels.
    pub width: u32,
    /// Height in pixels.
    pub height: u32,
}

impl Rect {
    /// Create a new rectangle.
    #[must_use]
    pub const fn new(x: i32, y: i32, width: u32, height: u32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }

    /// Check whether a point `(px, py)` is inside this rectangle.
    #[must_use]
    pub const fn contains(&self, px: i32, py: i32) -> bool {
        px >= self.x
            && px < self.x + self.width as i32
            && py >= self.y
            && py < self.y + self.height as i32
    }
}

/// 2D drawing canvas operating over a linear 32-bit pixel buffer.
pub struct Canvas<'a> {
    pixels: &'a mut [u32],
    width: usize,
    height: usize,
    stride: usize,
}

impl<'a> Canvas<'a> {
    /// Create a new canvas wrapping a mutable slice of packed 32-bit pixels.
    pub const fn new(pixels: &'a mut [u32], width: usize, height: usize, stride: usize) -> Self {
        Self {
            pixels,
            width,
            height,
            stride,
        }
    }

    /// Canvas width in pixels.
    #[must_use]
    pub const fn width(&self) -> usize {
        self.width
    }

    /// Canvas height in pixels.
    #[must_use]
    pub const fn height(&self) -> usize {
        self.height
    }

    /// Canvas stride in pixels.
    #[must_use]
    pub const fn stride(&self) -> usize {
        self.stride
    }

    /// Access the underlying pixel slice.
    #[must_use]
    pub const fn pixels(&self) -> &[u32] {
        self.pixels
    }

    /// Fill entire canvas with a solid color.
    pub fn clear(&mut self, color: Color) {
        let val = color.to_u32();
        for y in 0..self.height {
            let row_start = y * self.stride;
            let row_end = row_start + self.width;
            self.pixels[row_start..row_end].fill(val);
        }
    }

    /// Set a single pixel with bounds checking.
    pub fn set_pixel(&mut self, x: i32, y: i32, color: Color) {
        if x < 0 || y < 0 {
            return;
        }
        let ux = x as usize;
        let uy = y as usize;
        if ux < self.width && uy < self.height {
            let idx = uy * self.stride + ux;
            if color.a == 255 {
                self.pixels[idx] = color.to_u32();
            } else if color.a > 0 {
                let existing = self.pixels[idx];
                let bg = Color {
                    b: (existing & 0xFF) as u8,
                    g: ((existing >> 8) & 0xFF) as u8,
                    r: ((existing >> 16) & 0xFF) as u8,
                    a: 255,
                };
                self.pixels[idx] = bg.blend_over(color).to_u32();
            }
        }
    }

    /// Set a pixel with an extra 0..=255 analytical coverage multiplier.
    ///
    /// `coverage` scales `color.a` (e.g. 128 renders at half opacity), giving
    /// smooth subpixel-feel edges with integer math only.
    pub fn set_pixel_coverage(&mut self, x: i32, y: i32, color: Color, coverage: u8) {
        if coverage == 0 || color.a == 0 {
            return;
        }
        let scaled_alpha = ((u32::from(color.a) * u32::from(coverage)) / 255) as u8;
        if scaled_alpha == 0 {
            return;
        }
        self.set_pixel(
            x,
            y,
            Color {
                r: color.r,
                g: color.g,
                b: color.b,
                a: scaled_alpha,
            },
        );
    }

    /// Get raw 32-bit pixel value at `(x, y)` with bounds checking.
    #[must_use]
    pub const fn get_pixel_raw(&self, x: i32, y: i32) -> Option<u32> {
        if x < 0 || y < 0 {
            return None;
        }
        let ux = x as usize;
        let uy = y as usize;
        if ux < self.width && uy < self.height {
            let idx = uy * self.stride + ux;
            Some(self.pixels[idx])
        } else {
            None
        }
    }

    /// Set raw 32-bit pixel value at `(x, y)` directly.
    pub const fn set_pixel_raw(&mut self, x: i32, y: i32, val: u32) {
        if x < 0 || y < 0 {
            return;
        }
        let ux = x as usize;
        let uy = y as usize;
        if ux < self.width && uy < self.height {
            let idx = uy * self.stride + ux;
            self.pixels[idx] = val;
        }
    }

    /// Draw a filled rectangle.
    pub fn fill_rect(&mut self, x: i32, y: i32, w: u32, h: u32, color: Color) {
        let x0 = x.max(0);
        let y0 = y.max(0);
        let x1 = (x + w as i32).min(self.width as i32);
        let y1 = (y + h as i32).min(self.height as i32);

        if x0 >= x1 || y0 >= y1 {
            return;
        }

        let val = color.to_u32();
        for cy in y0..y1 {
            let row_start = cy as usize * self.stride + x0 as usize;
            let row_end = cy as usize * self.stride + x1 as usize;
            if color.a == 255 {
                self.pixels[row_start..row_end].fill(val);
            } else {
                for cx in x0..x1 {
                    self.set_pixel(cx, cy, color);
                }
            }
        }
    }

    /// Draw an unfilled rectangle outline.
    pub fn draw_rect(&mut self, x: i32, y: i32, w: u32, h: u32, color: Color) {
        if w == 0 || h == 0 {
            return;
        }
        let right = x + w as i32 - 1;
        let bottom = y + h as i32 - 1;
        for cx in x..=right {
            self.set_pixel(cx, y, color);
            self.set_pixel(cx, bottom, color);
        }
        for cy in y..=bottom {
            self.set_pixel(x, cy, color);
            self.set_pixel(right, cy, color);
        }
    }

    /// Draw a filled circle with analytical anti-aliased edges.
    ///
    /// Interior pixels blend opaque; the 1px boundary ring blends with
    /// coverage derived from 2x2 integer supersampling (offsets at ±1/4px
    /// in 1/4-pixel units), eliminating stair-stepping without floats.
    pub fn fill_circle(&mut self, cx: i32, cy: i32, radius: i32, color: Color) {
        if radius <= 0 {
            return;
        }
        // Small discs stay crisp via the fast path.
        if radius == 1 {
            self.set_pixel(cx, cy, color);
            return;
        }
        let r_sq = radius * radius;
        // Squared radii for the AA band (1px transition, integer only).
        let outer = (radius as i64 + 1) * (radius as i64 + 1);
        for dy in -radius - 1..=radius + 1 {
            let py = cy + dy;
            if py < 0 || py >= self.height as i32 {
                continue;
            }
            for dx in -radius - 1..=radius + 1 {
                let px = cx + dx;
                if px < 0 || px >= self.width as i32 {
                    continue;
                }
                let d_sq = (dx as i64) * (dx as i64) + (dy as i64) * (dy as i64);
                if d_sq <= r_sq as i64 {
                    // 2x2 supersample the pixel footprint for edge softness.
                    let mut inside = 0u32;
                    for oy in [-1i64, 1] {
                        for ox in [-1i64, 1] {
                            // Subpixel offsets at ±1/4 px in quarter units.
                            let qx = (dx as i64) * 4 + ox;
                            let qy = (dy as i64) * 4 + oy;
                            if qx * qx + qy * qy <= (r_sq as i64) * 16 {
                                inside += 1;
                            }
                        }
                    }
                    let coverage = (inside * 255 / 4) as u8;
                    if coverage >= 250 {
                        self.set_pixel(px, py, color);
                    } else {
                        self.set_pixel_coverage(px, py, color, coverage.max(1));
                    }
                } else if d_sq < outer {
                    // Outer fringe: faint halo softens the silhouette.
                    self.set_pixel_coverage(px, py, color, 48);
                }
            }
        }
    }

    /// Draw an unfilled circle outline with anti-aliased edges.
    pub fn draw_circle(&mut self, cx: i32, cy: i32, radius: i32, color: Color) {
        if radius <= 0 {
            return;
        }
        // 1px ring with a soft outer fringe (integer distance band).
        let r_outer_sq = (radius as i64) * (radius as i64);
        let r_inner = (radius - 1).max(0) as i64;
        let r_inner_sq = r_inner * r_inner;
        for dy in -radius - 1..=radius + 1 {
            let py = cy + dy;
            if py < 0 || py >= self.height as i32 {
                continue;
            }
            for dx in -radius - 1..=radius + 1 {
                let px = cx + dx;
                if px < 0 || px >= self.width as i32 {
                    continue;
                }
                let dist_sq = (dx as i64) * (dx as i64) + (dy as i64) * (dy as i64);
                if dist_sq <= r_outer_sq && dist_sq >= r_inner_sq {
                    self.set_pixel(px, py, color);
                } else {
                    let outer_fringe =
                        dist_sq > r_outer_sq && dist_sq <= r_outer_sq + 2 * radius as i64 + 2;
                    let inner_fringe = dist_sq < r_inner_sq
                        && dist_sq >= r_inner_sq - 2 * r_inner - 2
                        && r_inner > 0;
                    if outer_fringe || inner_fringe {
                        self.set_pixel_coverage(px, py, color, 64);
                    }
                }
            }
        }
    }

    /// Draw a filled rounded rectangle with analytical anti-aliased corners.
    ///
    /// Straight edges stay fully opaque; corner pixels use 2x2 integer
    /// supersampling against the quarter-circle for smooth coverage.
    pub fn fill_rounded_rect(&mut self, x: i32, y: i32, w: u32, h: u32, radius: u32, color: Color) {
        if w == 0 || h == 0 {
            return;
        }
        let max_r = (w / 2).min(h / 2);
        let r = radius.min(max_r) as i32;
        if r <= 0 {
            self.fill_rect(x, y, w, h, color);
            return;
        }
        if r == 1 {
            self.fill_rect(x, y, w, h, color);
            return;
        }

        let r_sq = (r as i64) * (r as i64);
        let x0 = x.max(0);
        let y0 = y.max(0);
        let x1 = (x + w as i32).min(self.width as i32);
        let y1 = (y + h as i32).min(self.height as i32);

        let left_corner_x = x + r;
        let right_corner_x = x + w as i32 - r;
        let top_corner_y = y + r;
        let bottom_corner_y = y + h as i32 - r;

        for py in y0..y1 {
            let in_top = py < top_corner_y;
            let in_bottom = py >= bottom_corner_y;

            for px in x0..x1 {
                let in_left = px < left_corner_x;
                let in_right = px >= right_corner_x;

                match (in_left || in_right, in_top || in_bottom) {
                    (false, _) | (_, false) => self.set_pixel(px, py, color),
                    (true, true) => {
                        let dx = if in_left {
                            (left_corner_x - px - 1) as i64
                        } else {
                            (px - right_corner_x) as i64
                        };
                        let dy = if in_top {
                            (top_corner_y - py - 1) as i64
                        } else {
                            (py - bottom_corner_y) as i64
                        };
                        let d_sq = dx * dx + dy * dy;
                        if d_sq * 16 <= r_sq * 16 - 8 * (dx + dy) - 8 {
                            self.set_pixel(px, py, color);
                        } else if d_sq <= r_sq {
                            // 2x2 supersample corner coverage (quarter-px offsets).
                            let mut inside = 0u32;
                            for oy in [-1i64, 1] {
                                for ox in [-1i64, 1] {
                                    let qx = dx * 4 + ox;
                                    let qy = dy * 4 + oy;
                                    if qx * qx + qy * qy <= r_sq * 16 {
                                        inside += 1;
                                    }
                                }
                            }
                            let coverage = (inside * 255 / 4) as u8;
                            if coverage > 0 {
                                self.set_pixel_coverage(px, py, color, coverage.max(1));
                            }
                        }
                    }
                }
            }
        }
    }

    /// Fill a rectangle whose bottom-left corner is rounded with the specified radius
    /// and anti-aliasing, while the other 3 corners remain square.
    pub fn fill_rect_rounded_bottom_left(
        &mut self,
        x: i32,
        y: i32,
        w: u32,
        h: u32,
        radius: u32,
        color: Color,
    ) {
        if w == 0 || h == 0 {
            return;
        }
        let r = (radius as i32).min(w as i32).min(h as i32);
        if r <= 1 {
            self.fill_rect(x, y, w, h, color);
            return;
        }

        let r_sq = (r as i64) * (r as i64);
        let x0 = x.max(0);
        let y0 = y.max(0);
        let x1 = (x + w as i32).min(self.width as i32);
        let y1 = (y + h as i32).min(self.height as i32);

        let corner_cx = x + r;
        let corner_cy = y + h as i32 - r;

        for py in y0..y1 {
            let in_bottom = py >= corner_cy;
            for px in x0..x1 {
                let in_left = px < corner_cx;
                if in_left && in_bottom {
                    let dx = (corner_cx - px - 1) as i64;
                    let dy = (py - corner_cy) as i64;
                    let d_sq = dx * dx + dy * dy;
                    if d_sq * 16 <= r_sq * 16 - 8 * (dx + dy) - 8 {
                        self.set_pixel(px, py, color);
                    } else if d_sq <= r_sq {
                        let mut inside = 0u32;
                        for oy in [-1i64, 1] {
                            for ox in [-1i64, 1] {
                                let qx = dx * 4 + ox;
                                let qy = dy * 4 + oy;
                                if qx * qx + qy * qy <= r_sq * 16 {
                                    inside += 1;
                                }
                            }
                        }
                        let coverage = (inside * 255 / 4) as u8;
                        if coverage > 0 {
                            self.set_pixel_coverage(px, py, color, coverage.max(1));
                        }
                    }
                } else {
                    self.set_pixel(px, py, color);
                }
            }
        }
    }

    /// Draw an unfilled rounded rectangle outline with anti-aliased corners.
    pub fn draw_rounded_rect(&mut self, x: i32, y: i32, w: u32, h: u32, radius: u32, color: Color) {
        if w == 0 || h == 0 {
            return;
        }
        let max_r = (w / 2).min(h / 2);
        let r = radius.min(max_r) as i32;
        if r <= 0 {
            self.draw_rect(x, y, w, h, color);
            return;
        }

        let r_outer_sq = (r as i64) * (r as i64);
        let r_inner = (r - 1).max(0) as i64;
        let r_inner_sq = r_inner * r_inner;
        let left_corner_x = x + r;
        let right_corner_x = x + w as i32 - r;
        let top_corner_y = y + r;
        let bottom_corner_y = y + h as i32 - r;

        // Straight top & bottom segments
        for px in left_corner_x..right_corner_x {
            self.set_pixel(px, y, color);
            self.set_pixel(px, y + h as i32 - 1, color);
        }
        // Straight left & right segments
        for py in top_corner_y..bottom_corner_y {
            self.set_pixel(x, py, color);
            self.set_pixel(x + w as i32 - 1, py, color);
        }

        // Corner arcs with a soft outer fringe.
        for dy in -1..=r {
            for dx in -1..=r {
                let dist_sq = (dx as i64) * (dx as i64) + (dy as i64) * (dy as i64);
                let on_ring = dist_sq <= r_outer_sq && dist_sq >= r_inner_sq;
                let fringe = !on_ring
                    && ((dist_sq > r_outer_sq && dist_sq <= r_outer_sq + 2 * r as i64 + 2)
                        || (r_inner > 0
                            && dist_sq < r_inner_sq
                            && dist_sq >= r_inner_sq - 2 * r_inner - 2));
                if on_ring || fringe {
                    let cov: u8 = if on_ring { 255 } else { 72 };
                    let pts = [
                        (left_corner_x - dx - 1, top_corner_y - dy - 1),
                        (right_corner_x + dx, top_corner_y - dy - 1),
                        (left_corner_x - dx - 1, bottom_corner_y + dy),
                        (right_corner_x + dx, bottom_corner_y + dy),
                    ];
                    for (qx, qy) in pts {
                        if cov == 255 {
                            self.set_pixel(qx, qy, color);
                        } else {
                            self.set_pixel_coverage(qx, qy, color, cov);
                        }
                    }
                }
            }
        }
    }

    /// Draw an anti-aliased 1px line (integer Bresenham + soft neighbours).
    pub fn draw_aa_line(&mut self, x0: i32, y0: i32, x1: i32, y1: i32, color: Color) {
        let dx = (x1 - x0).abs();
        let dy = -(y1 - y0).abs();
        let sx = if x0 < x1 { 1 } else { -1 };
        let sy = if y0 < y1 { 1 } else { -1 };
        let mut err = dx + dy;
        let (mut cx, mut cy) = (x0, y0);
        loop {
            self.set_pixel(cx, cy, color);
            // Soften stair-steps on diagonals with faint orthogonal fringe.
            if dx != 0 && dy != 0 {
                self.set_pixel_coverage(cx + 1, cy, color, 48);
                self.set_pixel_coverage(cx, cy + 1, color, 48);
            }
            if cx == x1 && cy == y1 {
                break;
            }
            let e2 = 2 * err;
            if e2 >= dy {
                err += dy;
                cx += sx;
            }
            if e2 <= dx {
                err += dx;
                cy += sy;
            }
        }
    }

    /// Fill a frosted-glass rectangle (semi-transparent + top highlight).
    ///
    /// Approximates the Figma 25-50px background blur with layered alpha:
    /// base acrylic fill, subtle vertical lightening, and a hairline top edge.
    pub fn fill_frosted_rect(&mut self, x: i32, y: i32, w: u32, h: u32, radius: u32, base: Color) {
        if w == 0 || h == 0 {
            return;
        }
        self.fill_rounded_rect(x, y, w, h, radius, base);
        // Top sheen (lightens upper third like blurred glass).
        let sheen_h = h.min(12);
        if sheen_h > 1 {
            self.fill_rounded_rect(
                x + 1,
                y + 1,
                w.saturating_sub(2),
                sheen_h - 1,
                radius.min(6).min(sheen_h - 1),
                Color::rgba(255, 255, 255, 22),
            );
            // Restore crisp top hairline.
            for px in x..x + w as i32 {
                self.set_pixel_coverage(px, y, Color::TASKBAR_HIGHLIGHT, 200);
            }
        }
    }

    /// Draw a tight modern drop shadow behind a rounded rectangle.
    ///
    /// Two restrained layers (no muddy halo): a crisp contact edge plus a
    /// short soft falloff, macOS/Windows-style.
    /// Three diffused elevation layers for macOS/Windows 11 floating window depth.
    pub fn draw_soft_shadow(&mut self, x: i32, y: i32, w: u32, h: u32, radius: u32) {
        let layers: [(i32, i32, u32, u8); 3] = [(0, 3, 4, 75), (0, 9, 12, 45), (0, 20, 24, 25)];
        for &(off_x, off_y, exp, alpha) in &layers {
            let sx = x + off_x - exp as i32;
            let sy = y + off_y - exp as i32;
            let sw = w + exp * 2;
            let sh = h + exp * 2;
            self.fill_rounded_rect(sx, sy, sw, sh, radius + exp, Color::rgba(0, 0, 0, alpha));
        }
    }

    /// Render a modern frosted glass panel with subtle specular top highlight.
    #[allow(clippy::too_many_arguments)]
    pub fn draw_glass_panel(
        &mut self,
        x: i32,
        y: i32,
        w: u32,
        h: u32,
        radius: u32,
        bg: Color,
        border: Color,
    ) {
        self.fill_rounded_rect(x, y, w, h, radius, bg);
        self.draw_rounded_rect(x, y, w, h, radius, border);
        let hx_start = x + radius as i32;
        let hx_end = x + w as i32 - radius as i32;
        if hx_end > hx_start {
            self.draw_aa_line(hx_start, y + 1, hx_end, y + 1, Color::GLASS_SPECULAR);
        }
    }

    /// Fill a rectangle with a vertical linear color gradient.
    pub fn fill_gradient_v(&mut self, x: i32, y: i32, w: u32, h: u32, top: Color, bottom: Color) {
        if w == 0 || h == 0 {
            return;
        }
        let x0 = x.max(0);
        let x1 = (x + w as i32).min(self.width as i32);
        if x0 >= x1 {
            return;
        }
        let total_h = h as i32;

        for row in 0..h as i32 {
            let py = y + row;
            if py < 0 || py >= self.height as i32 {
                continue;
            }
            let r = i32::from(top.r) + ((i32::from(bottom.r) - i32::from(top.r)) * row) / total_h;
            let g = i32::from(top.g) + ((i32::from(bottom.g) - i32::from(top.g)) * row) / total_h;
            let b = i32::from(top.b) + ((i32::from(bottom.b) - i32::from(top.b)) * row) / total_h;
            let color = Color::rgb(r as u8, g as u8, b as u8);
            let val = color.to_u32();

            let row_start = py as usize * self.stride + x0 as usize;
            let row_end = py as usize * self.stride + x1 as usize;
            self.pixels[row_start..row_end].fill(val);
        }
    }

    /// Render a modern dynamic twilight/aurora desktop wallpaper.
    pub fn fill_modern_wallpaper(&mut self) {
        let h = self.height as i32;
        let w = self.width as i32;
        let mid_h = h * 5 / 9;

        // Top section: Deep space slate (#0F172A) to rich indigo (#1E1B4B)
        self.fill_gradient_v(
            0,
            0,
            self.width as u32,
            mid_h as u32,
            Color::WALLPAPER_TOP,
            Color::WALLPAPER_MID,
        );
        // Bottom section: Rich indigo (#1E1B4B) to twilight cyan (#0E7490)
        self.fill_gradient_v(
            0,
            mid_h,
            self.width as u32,
            (self.height - mid_h as usize) as u32,
            Color::WALLPAPER_MID,
            Color::WALLPAPER_BOTTOM,
        );

        // Subtle aurora accent glow across the bottom-right corner
        let glow_cx = w * 4 / 5;
        let glow_cy = h * 9 / 10;
        let glow_r = w / 3;
        if glow_r > 0 {
            let glow_r_sq = glow_r * glow_r;
            let y_start = (glow_cy - glow_r).max(0);
            let y_end = (glow_cy + glow_r).min(h);
            let x_start = (glow_cx - glow_r).max(0);
            let x_end = (glow_cx + glow_r).min(w);

            for py in y_start..y_end {
                let dy = py - glow_cy;
                for px in x_start..x_end {
                    let dx = px - glow_cx;
                    let d_sq = dx * dx + dy * dy;
                    if d_sq < glow_r_sq {
                        let factor = ((glow_r_sq - d_sq) * 35) / glow_r_sq;
                        if factor > 0 {
                            self.set_pixel(px, py, Color::rgba(56, 189, 248, factor as u8));
                        }
                    }
                }
            }
        }
    }

    /// Render a pre-rasterized 32bpp BGRA/RGBA bitmap with alpha channel blending.
    pub fn draw_rgba_bitmap(&mut self, x: i32, y: i32, w: u32, h: u32, data: &[u32]) {
        let w_i = w as i32;
        let h_i = h as i32;
        for py in 0..h_i {
            let dy = y + py;
            if dy < 0 || dy >= self.height as i32 {
                continue;
            }
            let row_idx = (py as usize) * (w as usize);
            for px in 0..w_i {
                let dx = x + px;
                if dx < 0 || dx >= self.width as i32 {
                    continue;
                }
                let pixel = data[row_idx + px as usize];
                let a = (pixel >> 24) as u8;
                if a == 0 {
                    continue;
                }
                let r = ((pixel >> 16) & 0xFF) as u8;
                let g = ((pixel >> 8) & 0xFF) as u8;
                let b = (pixel & 0xFF) as u8;
                self.set_pixel(dx, dy, Color { r, g, b, a });
            }
        }
    }

    /// Calculate exact Cosmic Aurora wallpaper color at pixel coordinate `(px, py)`.
    #[must_use]
    pub fn wallpaper_color_at(px: i32, py: i32, w: u32, h: u32) -> Color {
        if w == 0 || h == 0 {
            return Color::rgb(14, 18, 38);
        }
        let py_c = (py.clamp(0, h as i32 - 1)) as u32;
        let px_c = (px.clamp(0, w as i32 - 1)) as u32;

        let (base_r, base_g, base_b) = if py_c * 2 < h {
            let t = py_c * 512 / h;
            let inv = 256 - t;
            (
                ((9 * inv + 20 * t) >> 8) as u8,
                ((13 * inv + 26 * t) >> 8) as u8,
                ((27 * inv + 56 * t) >> 8) as u8,
            )
        } else {
            let t = ((py_c - h / 2) * 512 / h).min(256);
            let inv = 256 - t;
            (
                ((20 * inv + 13 * t) >> 8) as u8,
                ((26 * inv + 17 * t) >> 8) as u8,
                ((56 * inv + 36 * t) >> 8) as u8,
            )
        };

        let cx1 = (w as i32) * 62 / 100;
        let cy1 = (h as i32) * 38 / 100;
        let r1_sq = ((w as i64) * 55 / 100) * ((w as i64) * 55 / 100);

        let cx2 = (w as i32) * 82 / 100;
        let cy2 = (h as i32) * 82 / 100;
        let r2_sq = ((w as i64) * 45 / 100) * ((w as i64) * 45 / 100);

        let cx3 = (w as i32) * 22 / 100;
        let cy3 = (h as i32) * 60 / 100;
        let r3_sq = ((w as i64) * 35 / 100) * ((w as i64) * 35 / 100);

        let dx1 = px_c as i32 - cx1;
        let dy1 = py_c as i32 - cy1;
        let d1 = (dx1 as i64) * (dx1 as i64) + (dy1 as i64) * (dy1 as i64);

        let dx2 = px_c as i32 - cx2;
        let dy2 = py_c as i32 - cy2;
        let d2 = (dx2 as i64) * (dx2 as i64) + (dy2 as i64) * (dy2 as i64);

        let dx3 = px_c as i32 - cx3;
        let dy3 = py_c as i32 - cy3;
        let d3 = (dx3 as i64) * (dx3 as i64) + (dy3 as i64) * (dy3 as i64);

        let mut cur_r = u32::from(base_r);
        let mut cur_g = u32::from(base_g);
        let mut cur_b = u32::from(base_b);

        if d1 < r1_sq && r1_sq > 0 {
            let f1 = ((r1_sq - d1) * 75 / r1_sq) as u32;
            cur_r = cur_r.saturating_add(f1 * 85 / 75);
            cur_g = cur_g.saturating_add(f1 * 60 / 75);
            cur_b = cur_b.saturating_add(f1 * 180 / 75);
        }
        if d2 < r2_sq && r2_sq > 0 {
            let f2 = ((r2_sq - d2) * 60 / r2_sq) as u32;
            cur_r = cur_r.saturating_add(f2 * 14 / 60);
            cur_g = cur_g.saturating_add(f2 * 140 / 60);
            cur_b = cur_b.saturating_add(f2 * 210 / 60);
        }
        if d3 < r3_sq && r3_sq > 0 {
            let f3 = ((r3_sq - d3) * 45 / r3_sq) as u32;
            cur_r = cur_r.saturating_add(f3 * 160 / 45);
            cur_g = cur_g.saturating_add(f3 * 40 / 45);
            cur_b = cur_b.saturating_add(f3 * 90 / 45);
        }

        Color::rgb(
            (cur_r.min(255)) as u8,
            (cur_g.min(255)) as u8,
            (cur_b.min(255)) as u8,
        )
    }

    /// Render modern flagship Cosmic Aurora desktop wallpaper.
    ///
    /// Rich electric violet, deep sapphire indigo, and vivid cyan horizon bloom.
    /// Fast integer-only row interpolation with zero heap allocation.
    pub fn fill_desert_wallpaper(&mut self) {
        let w = self.width;
        let h = self.height;
        if w == 0 || h == 0 {
            return;
        }

        let cx1 = (w as i32) * 62 / 100;
        let cy1 = (h as i32) * 38 / 100;
        let r1_sq = ((w as i64) * 55 / 100) * ((w as i64) * 55 / 100);

        let cx2 = (w as i32) * 82 / 100;
        let cy2 = (h as i32) * 82 / 100;
        let r2_sq = ((w as i64) * 45 / 100) * ((w as i64) * 45 / 100);

        let cx3 = (w as i32) * 22 / 100;
        let cy3 = (h as i32) * 60 / 100;
        let r3_sq = ((w as i64) * 35 / 100) * ((w as i64) * 35 / 100);

        for py in 0..h {
            let row_offset = py * self.stride;

            let (base_r, base_g, base_b) = if py * 2 < h {
                let t = (py * 512 / h) as u32;
                let inv = 256 - t;
                (
                    ((9 * inv + 20 * t) >> 8) as u8,
                    ((13 * inv + 26 * t) >> 8) as u8,
                    ((27 * inv + 56 * t) >> 8) as u8,
                )
            } else {
                let t = ((py - h / 2) * 512 / h).min(256) as u32;
                let inv = 256 - t;
                (
                    ((20 * inv + 13 * t) >> 8) as u8,
                    ((26 * inv + 17 * t) >> 8) as u8,
                    ((56 * inv + 36 * t) >> 8) as u8,
                )
            };

            let dy1 = py as i32 - cy1;
            let dy1_sq = (dy1 as i64) * (dy1 as i64);

            let dy2 = py as i32 - cy2;
            let dy2_sq = (dy2 as i64) * (dy2 as i64);

            let dy3 = py as i32 - cy3;
            let dy3_sq = (dy3 as i64) * (dy3 as i64);

            for px in 0..w {
                let dx1 = px as i32 - cx1;
                let d1 = (dx1 as i64) * (dx1 as i64) + dy1_sq;

                let dx2 = px as i32 - cx2;
                let d2 = (dx2 as i64) * (dx2 as i64) + dy2_sq;

                let dx3 = px as i32 - cx3;
                let d3 = (dx3 as i64) * (dx3 as i64) + dy3_sq;

                let mut cur_r = u32::from(base_r);
                let mut cur_g = u32::from(base_g);
                let mut cur_b = u32::from(base_b);

                if d1 < r1_sq && r1_sq > 0 {
                    let f1 = ((r1_sq - d1) * 75 / r1_sq) as u32;
                    cur_r = cur_r.saturating_add(f1 * 85 / 75);
                    cur_g = cur_g.saturating_add(f1 * 60 / 75);
                    cur_b = cur_b.saturating_add(f1 * 180 / 75);
                }
                if d2 < r2_sq && r2_sq > 0 {
                    let f2 = ((r2_sq - d2) * 60 / r2_sq) as u32;
                    cur_r = cur_r.saturating_add(f2 * 14 / 60);
                    cur_g = cur_g.saturating_add(f2 * 140 / 60);
                    cur_b = cur_b.saturating_add(f2 * 210 / 60);
                }
                if d3 < r3_sq && r3_sq > 0 {
                    let f3 = ((r3_sq - d3) * 45 / r3_sq) as u32;
                    cur_r = cur_r.saturating_add(f3 * 160 / 45);
                    cur_g = cur_g.saturating_add(f3 * 40 / 45);
                    cur_b = cur_b.saturating_add(f3 * 90 / 45);
                }

                self.pixels[row_offset + px] = Color::rgb(
                    (cur_r.min(255)) as u8,
                    (cur_g.min(255)) as u8,
                    (cur_b.min(255)) as u8,
                )
                .to_u32();
            }
        }
    }

    /// Official Finn 4-element symbol (Figma 71:450): rounded square, circle,
    /// 4-pointed sparkle, rotated diamond. Crisp floating white glyphs per Figma.
    pub fn draw_finn_logo(&mut self, x: i32, y: i32, size: u32) {
        if size == 32 {
            self.draw_rgba_bitmap(x, y, 32, 32, &crate::icons::DOCK_FINN);
            return;
        }
        let s = size.max(16) as i32;
        let cx = x + s / 2;
        let cy = y + s / 2;
        let r = s / 5;
        let offset = r + 1;

        // Top-left: rounded square
        self.fill_rounded_rect(
            cx - offset - r,
            cy - offset - r,
            (r * 2) as u32,
            (r * 2) as u32,
            2,
            Color::WHITE,
        );
        // Top-right: circle
        self.fill_circle(cx + offset, cy - offset, r, Color::WHITE);
        // Bottom-left: 4-pointed sparkle
        let sx = cx - offset;
        let sy = cy + offset;
        self.draw_aa_line(sx, sy - r - 1, sx, sy + r + 1, Color::WHITE);
        self.draw_aa_line(sx - r - 1, sy, sx + r + 1, sy, Color::WHITE);
        self.fill_circle(sx, sy, 1, Color::WHITE);
        // Bottom-right: rotated diamond
        let d = r + 1;
        let dx = cx + offset;
        let dy = cy + offset;
        for i in 0..=d {
            let span = d - i;
            self.draw_aa_line(dx - span, dy - i, dx + span, dy - i, Color::WHITE);
            if i > 0 {
                self.draw_aa_line(dx - span, dy + i, dx + span, dy + i, Color::WHITE);
            }
        }
    }

    /// Search magnifier (`search-01`, Figma 71:455): crisp floating white ring + handle.
    pub fn draw_search_icon(&mut self, x: i32, y: i32, size: u32) {
        if size == 32 {
            self.draw_rgba_bitmap(x, y, 32, 32, &crate::icons::DOCK_SEARCH);
            return;
        }
        let s = size.max(16) as i32;
        let cx = x + s * 3 / 8;
        let cy = y + s * 3 / 8;
        let r = s / 4;
        self.draw_circle(cx, cy, r.max(4), Color::WHITE);
        self.draw_circle(cx, cy, (r - 1).max(3), Color::WHITE);
        self.draw_aa_line(
            cx + r * 7 / 10,
            cy + r * 7 / 10,
            x + s - s / 5,
            y + s - s / 5,
            Color::WHITE,
        );
        self.draw_aa_line(
            cx + r * 7 / 10 + 1,
            cy + r * 7 / 10,
            x + s - s / 5 + 1,
            y + s - s / 5,
            Color::WHITE,
        );
    }

    /// Warm yellow/orange folder with documents (Figma 69:2556 / 71:459).
    pub fn draw_files_icon(&mut self, x: i32, y: i32, size: u32) {
        if size == 32 {
            self.draw_rgba_bitmap(x, y, 32, 32, &crate::icons::DOCK_FILES);
            return;
        }
        let s = size.max(16) as i32;
        self.fill_rounded_rect(x, y, s as u32, s as u32, 8, Color::rgb(255, 251, 235));
        self.draw_rounded_rect(x, y, s as u32, s as u32, 8, Color::INFO_CARD_BORDER);
        let fx = x + s / 6;
        let fw = s * 2 / 3;
        // Back plate (deep orange) with tab.
        self.fill_rounded_rect(
            fx,
            y + s / 3,
            fw as u32,
            (s / 2) as u32,
            3,
            Color::FOLDER_ORANGE,
        );
        self.fill_rect(
            fx,
            y + s / 4,
            (fw / 2) as u32,
            (s / 6) as u32,
            Color::FOLDER_ORANGE,
        );
        // Document sheet.
        self.fill_rounded_rect(
            fx + 2,
            y + s / 4,
            (fw - 6) as u32,
            (s * 2 / 5) as u32,
            2,
            Color::WHITE,
        );
        // Front plate (warm yellow) with AA top edge.
        self.fill_rounded_rect(
            fx,
            y + s * 2 / 5,
            fw as u32,
            (s * 2 / 5) as u32,
            3,
            Color::FOLDER_YELLOW,
        );
    }

    /// Glossy blue globe with network nodes + orbital ring (Figma 69:2545).
    pub fn draw_browser_icon(&mut self, x: i32, y: i32, size: u32) {
        if size == 32 {
            self.draw_rgba_bitmap(x, y, 32, 32, &crate::icons::DOCK_BROWSER);
            return;
        }
        let s = size.max(16) as i32;
        self.fill_rounded_rect(x, y, s as u32, s as u32, 8, Color::rgb(239, 246, 255));
        self.draw_rounded_rect(x, y, s as u32, s as u32, 8, Color::INFO_CARD_BORDER);
        let cx = x + s / 2;
        let cy = y + s / 2;
        let r = s * 3 / 10;
        self.fill_circle(cx, cy, r.max(4), Color::GLOBE_BLUE);
        self.fill_circle(cx - 1, cy - 1, (r * 3 / 4).max(3), Color::rgb(96, 165, 250));
        // Meridian + parallel strokes.
        self.draw_circle(cx, cy, (r * 2 / 3).max(2), Color::GLOBE_NAVY);
        self.draw_aa_line(cx - r, cy, cx + r, cy, Color::GLOBE_NAVY);
        // Orbital ring (tilted): approximated with offset arc dots.
        self.draw_circle(cx, cy, r + 2, Color::rgb(56, 189, 248));
        // Network nodes.
        self.fill_circle(cx - r / 2, cy - r / 3, 1, Color::WHITE);
        self.fill_circle(cx + r / 2, cy + r / 4, 1, Color::WHITE);
    }

    /// Embossed hexagon-nut settings glyph (Branding 69:2559).
    pub fn draw_settings_gear(&mut self, x: i32, y: i32, size: u32) {
        self.draw_settings_gear_tinted(x, y, size, Color::CHROME_LINE);
    }

    /// Hexagon-nut settings glyph in an explicit line color (dark Version
    /// suits light tiles; white suits acrylic cards).
    pub fn draw_settings_gear_tinted(&mut self, x: i32, y: i32, size: u32, line: Color) {
        let s = size.max(12) as i32;
        let cx = x + s / 2;
        let cy = y + s / 2;
        let outer = s * 2 / 5;
        // Hexagon outline via six AA spokes.
        let pts = [
            (cx + outer, cy),
            (cx + outer / 2, cy - outer * 7 / 8),
            (cx - outer / 2, cy - outer * 7 / 8),
            (cx - outer, cy),
            (cx - outer / 2, cy + outer * 7 / 8),
            (cx + outer / 2, cy + outer * 7 / 8),
        ];
        for i in 0..6 {
            let (ax, ay) = pts[i];
            let (bx, by) = pts[(i + 1) % 6];
            self.draw_aa_line(ax, ay, bx, by, line);
        }
        self.fill_circle(cx, cy, (outer / 2).max(2), line);
        self.fill_circle(cx, cy, (outer / 4).max(1), Color::WHITE);
    }

    /// Bluetooth rune (vertical stroke crossed by two diagonals).
    pub fn draw_bluetooth(&mut self, cx: i32, cy: i32, r: i32, color: Color) {
        let r = r.max(4);
        self.draw_aa_line(cx, cy - r, cx, cy + r, color);
        self.draw_aa_line(cx, cy - r, cx + r, cy - r / 3, color);
        self.draw_aa_line(cx + r, cy - r / 3, cx, cy, color);
        self.draw_aa_line(cx, cy, cx + r, cy + r / 3, color);
        self.draw_aa_line(cx + r, cy + r / 3, cx, cy + r, color);
    }

    /// Embossed `> _` terminal glyph on slate (Branding 69:2561).
    pub fn draw_terminal_glyph(&mut self, x: i32, y: i32, size: u32) {
        let s = size.max(12) as i32;
        self.fill_rounded_rect(x, y, s as u32, s as u32, 6, Color::TERMINAL_SLATE_TOP);
        self.fill_rect(
            x,
            y + s / 2,
            s as u32,
            (s / 2) as u32,
            Color::TERMINAL_SLATE_BOTTOM,
        );
        self.draw_rounded_rect(x, y, s as u32, s as u32, 6, Color::rgb(71, 85, 105));
        let cx = x + s / 4;
        let cy = y + s / 2;
        self.draw_aa_line(cx, cy - 3, cx + 4, cy, Color::WHITE);
        self.draw_aa_line(cx + 4, cy, cx, cy + 3, Color::WHITE);
        self.fill_rect(cx + 7, cy + 1, 5, 2, Color::WHITE);
    }

    /// Wi-Fi waves (tray, Figma 71:127): dot + two upward AA arcs.
    pub fn draw_wifi_icon(&mut self, x: i32, y: i32, light: bool) {
        let fg = if light {
            Color::WHITE
        } else {
            Color::TEXT_PRIMARY
        };
        let (cx, cy) = (x + 8, y + 13);
        self.fill_circle(cx, cy, 2, fg);
        // Upper semicircular arcs only (integer distance band, dy <= 0).
        for r in [6, 10] {
            let r_sq = r * r;
            let inner = (r - 1) * (r - 1);
            for dy in -r..=0 {
                for dx in -r..=r {
                    let d_sq = dx * dx + dy * dy;
                    if d_sq <= r_sq && d_sq >= inner {
                        self.set_pixel(cx + dx, cy + dy, fg);
                    }
                }
            }
        }
    }

    /// Battery gauge (tray, Figma 71:127): rounded body + level + nub.
    pub fn draw_battery_icon(&mut self, x: i32, y: i32, level_pct: u32, light: bool) {
        let frame = if light {
            Color::WHITE
        } else {
            Color::TEXT_PRIMARY
        };
        self.draw_rounded_rect(x, y + 3, 22, 11, 3, frame);
        self.fill_rect(x + 23, y + 6, 2, 5, frame);
        let inner = (20 * level_pct.min(100)) / 100;
        if inner > 0 {
            self.fill_rounded_rect(x + 1, y + 4, inner, 9, 2, Color::rgb(52, 211, 153));
        }
    }

    /// Monochrome close `X` line symbol (never macOS red traffic light).
    pub fn draw_close_glyph(&mut self, x: i32, y: i32, size: u32, color: Color) {
        let s = size.max(8) as i32;
        self.draw_aa_line(x + 2, y + 2, x + s - 3, y + s - 3, color);
        self.draw_aa_line(x + s - 3, y + 2, x + 2, y + s - 3, color);
    }

    /// Monochrome minimize `—` line symbol.
    pub fn draw_minimize_glyph(&mut self, x: i32, y: i32, size: u32, color: Color) {
        let s = size.max(8) as i32;
        self.fill_rect(x + 2, y + s / 2 - 1, (s - 4).max(1) as u32, 2, color);
    }

    /// Monochrome expand `⌜ ⌟` corner symbol.
    pub fn draw_expand_glyph(&mut self, x: i32, y: i32, size: u32, color: Color) {
        let s = size.max(8) as i32;
        let (ex, ey, ew) = (x + 2, y + 2, (s - 4).max(4));
        self.fill_rect(ex, ey, ew as u32, 2, color);
        self.fill_rect(ex, ey, 2, ew as u32, color);
        let (bx, by) = (x + s - 2 - ew, y + s - 2 - ew);
        self.fill_rect(bx + ew - 2, by, 2, ew as u32, color);
        self.fill_rect(bx, by + ew - 2, ew as u32, 2, color);
    }

    /// Circular power symbol (ring + vertical stroke).
    pub fn draw_power_glyph(&mut self, cx: i32, cy: i32, r: i32, color: Color) {
        self.draw_circle(cx, cy, r.max(4), color);
        self.draw_aa_line(cx, cy - r, cx, cy + 1, color);
        self.fill_circle(cx, cy - r, 1, color);
    }

    /// Circular battery / status ring (Figma 71:127).
    ///
    /// Authentic 3/4 white circular ring with remaining segment dimmed.
    pub fn draw_status_ring(&mut self, cx: i32, cy: i32, r: i32, level_pct: u32) {
        let r_inner = (r - 4).max(1);
        let r_outer_sq = (r * r) as i64;
        let r_inner_sq = (r_inner * r_inner) as i64;
        let dim = Color::rgba(255, 255, 255, 65);

        for dy in -r..=r {
            for dx in -r..=r {
                let d_sq = (dx * dx + dy * dy) as i64;
                if d_sq >= r_inner_sq && d_sq <= r_outer_sq {
                    // Split active arc based on level_pct
                    let is_active = if level_pct >= 90 {
                        dx >= 0 || dy <= 0 || dx >= -r / 2
                    } else if level_pct >= 60 {
                        dx >= 0 || dy <= 0
                    } else {
                        dy <= 0
                    };
                    let col = if is_active { Color::WHITE } else { dim };
                    self.set_pixel_coverage(cx + dx, cy + dy, col, 240);
                }
            }
        }
    }

    /// Crescent moon glyph for Sleep action (Figma 112:352).
    pub fn draw_moon_glyph(&mut self, cx: i32, cy: i32, r: i32, color: Color, bg_cutout: Color) {
        self.fill_circle(cx, cy, r, color);
        self.fill_circle(cx + r / 2 + 1, cy - r / 3, r - 1, bg_cutout);
    }

    /// Circular refresh / restart arrows glyph (Figma 112:351).
    pub fn draw_restart_glyph(&mut self, cx: i32, cy: i32, r: i32, color: Color) {
        self.draw_circle(cx, cy, r, color);
        self.draw_circle(cx, cy, (r - 1).max(1), color);
        // Break arc at top-right
        self.fill_rect(cx + r / 2, cy - r - 2, 6, 8, Color::TRANSPARENT);
        // Arrowheads
        let ax = cx + r / 2 + 2;
        let ay = cy - r + 2;
        self.draw_aa_line(ax - 4, ay - 4, ax + 2, ay, color);
        self.draw_aa_line(ax - 4, ay + 4, ax + 2, ay, color);
    }

    /// Play triangle icon.
    pub fn draw_play_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let half = size / 2;
        for dx in 0..size {
            let h = (dx * half) / size.max(1);
            self.draw_aa_line(
                cx - half / 2 + dx,
                cy - h,
                cx - half / 2 + dx,
                cy + h,
                color,
            );
        }
    }

    /// Pause two-bars icon.
    pub fn draw_pause_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let bar_w = (size / 3).max(2);
        let bar_h = size;
        self.fill_rounded_rect(
            cx - size / 2,
            cy - bar_h / 2,
            bar_w as u32,
            bar_h as u32,
            2,
            color,
        );
        self.fill_rounded_rect(
            cx + size / 2 - bar_w,
            cy - bar_h / 2,
            bar_w as u32,
            bar_h as u32,
            2,
            color,
        );
    }

    /// Rewind double-triangle icon.
    pub fn draw_rewind_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let half = size / 2;
        for dx in 0..half {
            let h = (dx * half) / half.max(1);
            self.draw_aa_line(cx - dx, cy - h, cx - dx, cy + h, color);
            self.draw_aa_line(cx + half - dx, cy - h, cx + half - dx, cy + h, color);
        }
    }

    /// Fast forward double-triangle icon.
    pub fn draw_forward_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let half = size / 2;
        for dx in 0..half {
            let h = (dx * half) / half.max(1);
            self.draw_aa_line(cx - half + dx, cy - h, cx - half + dx, cy + h, color);
            self.draw_aa_line(cx + dx, cy - h, cx + dx, cy + h, color);
        }
    }

    /// Airplane mode glyph (Control Centre).
    pub fn draw_airplane_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let s = size.max(10);
        // Fuselage
        self.draw_aa_line(cx, cy - s / 2, cx, cy + s / 2, color);
        self.draw_aa_line(cx - 1, cy - s / 2 + 2, cx - 1, cy + s / 2 - 2, color);
        // Wings
        self.draw_aa_line(cx - s / 2, cy - 1, cx + s / 2, cy - 1, color);
        self.draw_aa_line(cx - s / 2 + 1, cy, cx + s / 2 - 1, cy, color);
        // Tail
        self.draw_aa_line(
            cx - s / 4,
            cy + s / 2 - 2,
            cx + s / 4,
            cy + s / 2 - 2,
            color,
        );
    }

    /// Bell / Do Not Disturb glyph.
    pub fn draw_bell_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let r = size / 2;
        self.draw_circle(cx, cy - 2, (r - 2).max(3), color);
        self.draw_aa_line(cx - r, cy + r - 4, cx + r, cy + r - 4, color);
        self.fill_circle(cx, cy + r - 2, 2, color);
    }

    /// Flashlight glyph.
    pub fn draw_flashlight_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let s = size / 2;
        // Lightning bolt shape
        self.draw_aa_line(cx + 2, cy - s, cx - 3, cy, color);
        self.draw_aa_line(cx - 3, cy, cx + 1, cy, color);
        self.draw_aa_line(cx + 1, cy, cx - 2, cy + s, color);
    }

    /// Screen Cast glyph.
    pub fn draw_cast_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let half = size / 2;
        self.draw_rounded_rect(
            cx - half,
            cy - half + 2,
            size as u32,
            (size - 4) as u32,
            2,
            color,
        );
        self.fill_circle(cx - half + 4, cy + half - 4, 2, color);
        self.draw_circle(cx - half + 4, cy + half - 4, 4, color);
    }

    /// Sun glyph for brightness slider.
    pub fn draw_sun_glyph(&mut self, cx: i32, cy: i32, r: i32, color: Color) {
        self.fill_circle(cx, cy, (r - 3).max(2), color);
        let ray = r + 2;
        self.draw_aa_line(cx, cy - ray, cx, cy - r, color);
        self.draw_aa_line(cx, cy + r, cx, cy + ray, color);
        self.draw_aa_line(cx - ray, cy, cx - r, cy, color);
        self.draw_aa_line(cx + r, cy, cx + ray, cy, color);
    }

    /// Speaker glyph for volume slider.
    pub fn draw_speaker_glyph(&mut self, cx: i32, cy: i32, size: i32, color: Color) {
        let h = size / 2;
        self.fill_rect(
            cx - h,
            cy - h / 3,
            (h / 2) as u32,
            (h * 2 / 3) as u32,
            color,
        );
        self.draw_aa_line(cx - h / 2, cy - h / 3, cx, cy - h, color);
        self.draw_aa_line(cx, cy - h, cx, cy + h, color);
        self.draw_aa_line(cx, cy + h, cx - h / 2, cy + h / 3, color);
        self.draw_aa_line(cx + 3, cy - h / 2, cx + 3, cy + h / 2, color);
        self.draw_aa_line(cx + 6, cy - h * 3 / 4, cx + 6, cy + h * 3 / 4, color);
    }

    /// Vertical capsule slider (Control Centre, Figma 44:455).
    #[allow(clippy::too_many_arguments)]
    pub fn draw_vertical_slider(
        &mut self,
        x: i32,
        y: i32,
        width: u32,
        height: u32,
        level_pct: u32,
        is_sun: bool,
        active: bool,
    ) {
        let rx = width / 2;
        // Background track (dark translucent pill)
        self.fill_rounded_rect(x, y, width, height, rx, Color::rgba(255, 255, 255, 26));
        self.draw_rounded_rect(x, y, width, height, rx, Color::rgba(255, 255, 255, 35));

        // Active level fill from bottom
        let fill_h = ((height as u64 * level_pct.min(100) as u64) / 100) as u32;
        if fill_h > 0 {
            let fill_y = y + (height as i32) - (fill_h as i32);
            let fill_col = if active {
                Color::WHITE
            } else {
                Color::rgba(255, 255, 255, 210)
            };
            self.fill_rounded_rect(x, fill_y, width, fill_h, rx, fill_col);
        }

        // Icon at bottom
        let icon_y = y + height as i32 - (rx as i32);
        let icon_col = if level_pct > 20 {
            Color::rgb(30, 41, 59)
        } else {
            Color::WHITE
        };
        if is_sun {
            self.draw_sun_glyph(x + rx as i32, icon_y, 6, icon_col);
        } else {
            self.draw_speaker_glyph(x + rx as i32, icon_y, 10, icon_col);
        }
    }

    /// Draw a single 16px Instrument Sans character (baseline layout).
    ///
    /// `y` is the top of the ascender box; ink lands at
    /// `(x + x_off, y + ascent + y_off)` with true 4-bit alpha coverage.
    /// Returns the pen advance.
    #[allow(clippy::needless_range_loop)]
    pub fn draw_char(&mut self, x: i32, y: i32, c: char, fg: Color, bg: Option<Color>) -> i32 {
        let g = lookup_text(c);
        let adv = i32::from(g.advance);
        if let Some(bg_color) = bg {
            self.fill_rect(x, y, adv as u32, crate::font::TEXT_LINE, bg_color);
        }
        if g.w == 0 || g.h == 0 {
            return adv;
        }
        let baseline = y + TEXT_ASCENT;
        let ox = x + i32::from(g.x_off);
        let oy = baseline + i32::from(g.y_off);
        for row in 0..u32::from(g.h) {
            for col in 0..u32::from(g.w) {
                let alpha = crate::font::glyph_coverage(&TEXT_DATA, g, row, col);
                if alpha >= 250 {
                    self.set_pixel(ox + col as i32, oy + row as i32, fg);
                } else if alpha > 0 {
                    self.set_pixel_coverage(ox + col as i32, oy + row as i32, fg, alpha);
                }
            }
        }
        adv
    }

    /// Draw a 16px Instrument Sans string.
    pub fn draw_string(&mut self, x: i32, y: i32, text: &str, fg: Color, bg: Option<Color>) {
        let mut cur_x = x;
        for c in text.chars() {
            if c == '\n' {
                cur_x = x;
                continue;
            }
            let adv = self.draw_char(cur_x, y, c, fg, bg);
            cur_x += adv;
        }
    }

    /// Draw a single 20px Instrument Sans title character.
    #[allow(clippy::needless_range_loop)]
    pub fn draw_title_char(&mut self, x: i32, y: i32, c: char, fg: Color) -> i32 {
        let g = lookup_title(c);
        let adv = i32::from(g.advance);
        if g.w == 0 || g.h == 0 {
            return adv;
        }
        let baseline = y + TITLE_ASCENT;
        let ox = x + i32::from(g.x_off);
        let oy = baseline + i32::from(g.y_off);
        for row in 0..u32::from(g.h) {
            for col in 0..u32::from(g.w) {
                let alpha = crate::font::glyph_coverage(&TITLE_DATA, g, row, col);
                if alpha >= 250 {
                    self.set_pixel(ox + col as i32, oy + row as i32, fg);
                } else if alpha > 0 {
                    self.set_pixel_coverage(ox + col as i32, oy + row as i32, fg, alpha);
                }
            }
        }
        adv
    }

    /// Draw a 20px Instrument Sans title string.
    pub fn draw_title(&mut self, x: i32, y: i32, text: &str, fg: Color) {
        let mut cur_x = x;
        for c in text.chars() {
            if c == '\n' {
                cur_x = x;
                continue;
            }
            cur_x += self.draw_title_char(cur_x, y, c, fg);
        }
    }

    /// Draw a 20px title with semibold emphasis (double-strike +1px).
    ///
    /// The embedded atlas carries Regular only; overstriking one pixel over
    /// approximates the semibold headings of modern desktop shells.
    pub fn draw_title_strong(&mut self, x: i32, y: i32, text: &str, fg: Color) {
        self.draw_title(x, y, text, fg);
        self.draw_title(x + 1, y, text, fg);
    }

    /// Draw a 16px string with semibold emphasis (double-strike +1px).
    pub fn draw_string_strong(&mut self, x: i32, y: i32, text: &str, fg: Color, bg: Option<Color>) {
        self.draw_string(x, y, text, fg, bg);
        self.draw_string(x + 1, y, text, fg, None);
    }

    /// Pixel-accurate width of a 16px string.
    #[must_use]
    pub fn string_width(text: &str) -> u32 {
        crate::font::text_width(text)
    }

    /// Pixel-accurate width of a 20px title string.
    #[must_use]
    pub fn title_width(text: &str) -> u32 {
        crate::font::title_width(text)
    }

    /// Draw a 16px string clipped to `max_w` with an ellipsis when truncated.
    pub fn draw_string_max(
        &mut self,
        x: i32,
        y: i32,
        text: &str,
        fg: Color,
        bg: Option<Color>,
        max_w: u32,
    ) {
        let full = crate::font::text_width(text);
        if full <= max_w {
            self.draw_string(x, y, text, fg, bg);
            return;
        }
        let ell = crate::font::text_width("…");
        let mut cur_x = x;
        let limit = x + max_w.saturating_sub(ell) as i32;
        for c in text.chars() {
            let adv = crate::font::glyph_advance(c) as i32;
            if cur_x + adv > limit {
                break;
            }
            cur_x += self.draw_char(cur_x, y, c, fg, bg);
        }
        self.draw_string(cur_x, y, "…", fg, bg);
    }

    /// Blit an image/surface rectangle into this canvas.
    pub fn blit(
        &mut self,
        src: &[u32],
        src_stride: usize,
        dest_x: i32,
        dest_y: i32,
        width: u32,
        height: u32,
    ) {
        for dy in 0..height as i32 {
            let cy = dest_y + dy;
            if cy < 0 || cy >= self.height as i32 {
                continue;
            }
            for dx in 0..width as i32 {
                let cx = dest_x + dx;
                if cx < 0 || cx >= self.width as i32 {
                    continue;
                }
                let src_idx = dy as usize * src_stride + dx as usize;
                let pixel = src[src_idx];
                let color = Color {
                    b: (pixel & 0xFF) as u8,
                    g: ((pixel >> 8) & 0xFF) as u8,
                    r: ((pixel >> 16) & 0xFF) as u8,
                    a: ((pixel >> 24) & 0xFF) as u8,
                };
                self.set_pixel(cx, cy, color);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn color_blending() {
        let bg = Color::WHITE;
        let fg = Color::rgba(0, 0, 0, 128);
        let blended = bg.blend_over(fg);
        assert!(blended.r > 120 && blended.r < 135);
        assert_eq!(blended.r, blended.g);
        assert_eq!(blended.g, blended.b);
    }

    #[test]
    fn rect_contains_point() {
        let rect = Rect::new(10, 20, 100, 50);
        assert!(rect.contains(10, 20));
        assert!(rect.contains(50, 40));
        assert!(rect.contains(109, 69));
        assert!(!rect.contains(9, 20));
        assert!(!rect.contains(110, 70));
    }

    #[test]
    fn canvas_fill_and_clear() {
        let mut buf = [0u32; 100];
        let mut canvas = Canvas::new(&mut buf, 10, 10, 10);
        canvas.clear(Color::RED);
        assert_eq!(canvas.pixels[0], Color::RED.to_u32());
        assert_eq!(canvas.pixels[99], Color::RED.to_u32());

        canvas.fill_rect(2, 2, 4, 4, Color::BLUE);
        assert_eq!(canvas.pixels[2 * 10 + 2], Color::BLUE.to_u32());
        assert_eq!(canvas.pixels[0], Color::RED.to_u32());
    }

    #[test]
    fn canvas_draw_string() {
        let mut buf = [0u32; 256];
        let mut canvas = Canvas::new(&mut buf, 16, 16, 16);
        canvas.clear(Color::BLACK);
        canvas.draw_string(0, 0, "A", Color::WHITE, None);
        // Verify that some white pixels were drawn
        assert!(canvas.pixels.iter().any(|&p| p == Color::WHITE.to_u32()));
    }

    #[test]
    fn canvas_circle() {
        let mut buf = [0u32; 400];
        let mut canvas = Canvas::new(&mut buf, 20, 20, 20);
        canvas.clear(Color::BLACK);
        canvas.fill_circle(10, 10, 5, Color::RED);
        // Center pixel should be red
        assert_eq!(canvas.get_pixel_raw(10, 10), Some(Color::RED.to_u32()));
        // Outside circle should be black
        assert_eq!(canvas.get_pixel_raw(0, 0), Some(Color::BLACK.to_u32()));
    }

    #[test]
    fn canvas_rounded_rect() {
        let mut buf = [0u32; 900];
        let mut canvas = Canvas::new(&mut buf, 30, 30, 30);
        canvas.clear(Color::BLACK);
        canvas.fill_rounded_rect(5, 5, 20, 20, 4, Color::GREEN);
        // Center should be green
        assert_eq!(canvas.get_pixel_raw(15, 15), Some(Color::GREEN.to_u32()));
        // Extreme corner should NOT be green (rounded away)
        assert_eq!(canvas.get_pixel_raw(5, 5), Some(Color::BLACK.to_u32()));
    }

    #[test]
    fn canvas_gradient() {
        let mut buf = [0u32; 100];
        let mut canvas = Canvas::new(&mut buf, 10, 10, 10);
        canvas.fill_gradient_v(0, 0, 10, 10, Color::BLACK, Color::WHITE);
        // Top should be black
        assert_eq!(canvas.get_pixel_raw(5, 0), Some(Color::BLACK.to_u32()));
        // Bottom should be close to white
        let bottom = canvas.get_pixel_raw(5, 9).unwrap();
        assert!((bottom & 0xFF) > 200);
    }

    #[test]
    fn aa_coverage_blends_partial_alpha() {
        let mut buf = [0u32; 100];
        let mut canvas = Canvas::new(&mut buf, 10, 10, 10);
        canvas.clear(Color::BLACK);
        canvas.set_pixel_coverage(5, 5, Color::WHITE, 128);
        let px = canvas.get_pixel_raw(5, 5).unwrap();
        let r = ((px >> 16) & 0xFF) as u8;
        assert!(
            r > 100 && r < 160,
            "half coverage should gray-blend, got {r}"
        );
        canvas.set_pixel_coverage(6, 6, Color::WHITE, 0);
        assert_eq!(canvas.get_pixel_raw(6, 6), Some(Color::BLACK.to_u32()));
    }

    #[test]
    fn aa_circle_edge_is_smooth_not_jagged() {
        let mut buf = [0u32; 1600];
        let mut canvas = Canvas::new(&mut buf, 40, 40, 40);
        canvas.clear(Color::BLACK);
        canvas.fill_circle(20, 20, 10, Color::WHITE);
        assert_eq!(canvas.get_pixel_raw(20, 20), Some(Color::WHITE.to_u32()));
        // A pixel just outside the ideal radius carries a soft fringe, not full white.
        let edge = canvas.get_pixel_raw(20 + 10, 20).unwrap();
        assert_ne!(edge, Color::BLACK.to_u32());
    }

    #[test]
    fn desert_wallpaper_covers_canvas() {
        let mut buf = std::vec![0u32; 640 * 480];
        let mut canvas = Canvas::new(&mut buf, 640, 480, 640);
        canvas.fill_desert_wallpaper();
        assert!(canvas.pixels().iter().any(|&p| p != 0));
        // Sky top differs from sand bottom (vertical gradient + dunes).
        assert_ne!(
            canvas.get_pixel_raw(320, 10),
            canvas.get_pixel_raw(320, 470)
        );
    }

    #[test]
    fn vector_icons_render_crisp() {
        let mut buf = [0u32; 64 * 64];
        let mut canvas = Canvas::new(&mut buf, 64, 64, 64);
        canvas.clear(Color::BLACK);
        canvas.draw_finn_logo(4, 4, 32);
        canvas.draw_search_icon(4, 40, 16);
        assert!(canvas.pixels().iter().any(|&p| p != 0));
    }

    #[test]
    fn proportional_text_advances_narrower_for_i() {
        let mut buf = [0u32; 32 * 32];
        let mut canvas = Canvas::new(&mut buf, 32, 32, 32);
        canvas.clear(Color::BLACK);
        canvas.draw_string(0, 0, "iiii", Color::WHITE, None);
        let narrow_count = canvas
            .pixels()
            .iter()
            .filter(|&&p| p != Color::BLACK.to_u32())
            .count();
        let mut buf2 = [0u32; 32 * 32];
        let mut canvas2 = Canvas::new(&mut buf2, 32, 32, 32);
        canvas2.clear(Color::BLACK);
        canvas2.draw_string(0, 0, "mmmm", Color::WHITE, None);
        let wide_count = canvas2
            .pixels()
            .iter()
            .filter(|&&p| p != Color::BLACK.to_u32())
            .count();
        assert!(wide_count > narrow_count);
        assert_eq!(Canvas::string_width("Hi!"), crate::font::text_width("Hi!"));
    }
}
