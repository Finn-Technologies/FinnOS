#![no_std]
#![deny(missing_docs)]

//! `finn-libpeony`: Graphical desktop toolkit, 2D rasterizer, window compositor,
//! and core apps for the `FinnOS` Peony desktop environment.

pub mod apps;
pub mod canvas;
pub mod compositor;
pub mod decoration;
pub mod font;
pub mod icons;
pub mod layout;
pub mod ui;
pub mod wallpaper;
pub mod widget;

pub use decoration::{BorderRadius, BoxDecoration, BoxShadow, FontWeight, TextStyle};
pub use layout::{
    Alignment, BoxConstraints, CrossAxisAlignment, EdgeInsets, MainAxisAlignment, Offset, Size,
};
pub use ui::{Card, ListTile, NavItem, SearchField, Switch};

pub use apps::{
    DOCK_ICON, DOCK_WIDTH, DesktopState, START_MENU_RADIUS, START_MENU_SIZE, TASKBAR_HEIGHT,
    format_tray_clock, render_control_centre, render_control_centre_state, render_files_app,
    render_files_app_state, render_power_options, render_power_options_state, render_settings_app,
    render_settings_app_state, render_start_menu, render_start_menu_state, render_taskbar,
    render_terminal_app, render_top_panel,
};
pub use canvas::{Canvas, Color, Rect};
pub use compositor::{AppId, Compositor, CompositorWindow};
pub use font::{
    FONT_HEIGHT, FONT_WIDTH, FontSize, Glyph, LINE_HEIGHT_BODY, LINE_HEIGHT_TITLE, TEXT_ASCENT,
    TEXT_DATA, TEXT_DESCENT, TEXT_GLYPHS, TEXT_LINE, TITLE_ASCENT, TITLE_DATA, TITLE_DESCENT,
    TITLE_GLYPHS, TITLE_LINE, glyph_advance, glyph_coverage, lookup_text, lookup_title, text_width,
    title_advance, title_width,
};
pub use widget::{Button, CHROME_CONTROL_SIZE, PillBadge, ToggleSwitch, Window};

#[cfg(test)]
extern crate std;
