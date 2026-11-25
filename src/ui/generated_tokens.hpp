// Generated from ui/_tokens.fui by tools/tokens.py. Do not edit.
#ifndef FINN_UI_GENERATED_TOKENS_HPP
#define FINN_UI_GENERATED_TOKENS_HPP

#include <cstdint>

namespace finn::ui::colors {

inline constexpr std::uint32_t bone_light = 0xFFFBFBFDu;
inline constexpr std::uint32_t bone_dark = 0xFF0B0D12u;
inline constexpr std::uint32_t tile_light = 0xFFFFFFFFu;
inline constexpr std::uint32_t tile_dark = 0xFF171B24u;
inline constexpr std::uint32_t tile_on_light = 0xFFDCEBFFu;
inline constexpr std::uint32_t tile_on_dark = 0xFF1F3A60u;
inline constexpr std::uint32_t hair_light = 0x14000000u;
inline constexpr std::uint32_t hair_dark = 0x22FFFFFFu;
inline constexpr std::uint32_t hair_soft_light = 0x0D000000u;
inline constexpr std::uint32_t hair_soft_dark = 0x12FFFFFFu;
inline constexpr std::uint32_t ink_light = 0xFF0F141Au;
inline constexpr std::uint32_t ink_dark = 0xFFF5F7FAu;
inline constexpr std::uint32_t mute_light = 0xFF868E9Bu;
inline constexpr std::uint32_t mute_dark = 0xFF949DAEu;
inline constexpr std::uint32_t dim_light = 0xFF525866u;
inline constexpr std::uint32_t dim_dark = 0xFFB4BCCBu;
inline constexpr std::uint32_t accent_light = 0xFF007BFFu;
inline constexpr std::uint32_t accent_dark = 0xFF007BFFu;
inline constexpr std::uint32_t track_light = 0x80000000u;
inline constexpr std::uint32_t track_dark = 0x33FFFFFFu;
inline constexpr std::uint32_t knob_light = 0xFFFFFFFFu;
inline constexpr std::uint32_t knob_dark = 0xFFFFFFFFu;
inline constexpr std::uint32_t on_light = 0xFF007BFFu;
inline constexpr std::uint32_t on_dark = 0xFF0A84FFu;
inline constexpr std::uint32_t fill_light = 0xFFFFFFFFu;
inline constexpr std::uint32_t fill_dark = 0xFFFFFFFFu;
inline constexpr std::uint32_t glass_light = 0xFFFBFBFDu;
inline constexpr std::uint32_t glass_dark = 0xFF11151Eu;
inline constexpr std::uint32_t notif_glass_light = 0xB2FFFFFFu;
inline constexpr std::uint32_t notif_glass_dark = 0xA8161B26u;
inline constexpr std::uint32_t lock_pill_light = 0xA6FFFFFFu;
inline constexpr std::uint32_t lock_pill_dark = 0xB811151Eu;
inline constexpr std::uint32_t nav_sel_light = 0x120F141Au;
inline constexpr std::uint32_t nav_sel_dark = 0x24FFFFFFu;
inline constexpr std::uint32_t hero_tint_light = 0xFFEBFFDFu;
inline constexpr std::uint32_t hero_tint_dark = 0xFF15221Bu;
inline constexpr std::uint32_t sound_badge_light = 0xFFFF0000u;
inline constexpr std::uint32_t sound_badge_dark = 0xFFFF3B30u;
inline constexpr std::uint32_t surface_canvas_light = 0xFFF2F4F8u;
inline constexpr std::uint32_t surface_canvas_dark = 0xFF0E1117u;
inline constexpr std::uint32_t surface_input_light = 0xFFF4F6FAu;
inline constexpr std::uint32_t surface_input_dark = 0xFF1C212Du;
inline constexpr std::uint32_t surface_hover_light = 0x0E000000u;
inline constexpr std::uint32_t surface_hover_dark = 0x14FFFFFFu;
inline constexpr std::uint32_t surface_pressed_light = 0x18000000u;
inline constexpr std::uint32_t surface_pressed_dark = 0x22FFFFFFu;
inline constexpr std::uint32_t danger_light = 0xFFEF4444u;
inline constexpr std::uint32_t danger_dark = 0xFFF87171u;
inline constexpr std::uint32_t danger_hover_light = 0xFFFF0202u;
inline constexpr std::uint32_t danger_hover_dark = 0xFFFF0202u;
inline constexpr std::uint32_t success_light = 0xFF16A34Au;
inline constexpr std::uint32_t success_dark = 0xFF22C55Eu;
inline constexpr std::uint32_t warning_light = 0xFFD97706u;
inline constexpr std::uint32_t warning_dark = 0xFFF59E0Bu;

}  // namespace finn::ui::colors

namespace finn::ui::spacing {

inline constexpr int sp_0 = 0;
inline constexpr int sp_half = 2;
inline constexpr int sp_1 = 4;
inline constexpr int sp_2 = 8;
inline constexpr int sp_3 = 12;
inline constexpr int sp_4 = 16;
inline constexpr int sp_5 = 20;
inline constexpr int sp_6 = 24;
inline constexpr int sp_8 = 32;
inline constexpr int sp_10 = 40;
inline constexpr int sp_12 = 48;
inline constexpr int sp_16 = 64;

}  // namespace finn::ui::spacing

namespace finn::ui::radii {

inline constexpr double r_xs = 4.0;
inline constexpr double r_sm = 6.0;
inline constexpr double r_md = 8.0;
inline constexpr double r_lg = 12.0;
inline constexpr double r_folder = 14.0;
inline constexpr double r_xl = 20.0;
inline constexpr double r_nav = 5.0;
inline constexpr double r_bubble = 10.4;
inline constexpr double r_panel = 10.4;
inline constexpr double r_tile = 5.37;
inline constexpr double r_note = 10.4;
inline constexpr double r_card = 10.4;
inline constexpr double r_app = 7.67;
inline constexpr double r_hero = 39.0;
inline constexpr double r_pill = 999.0;
inline constexpr double corner_smoothing = 0.6;

}  // namespace finn::ui::radii

namespace finn::ui::blur {

inline constexpr int blur_glass = 35;
inline constexpr int blur_notification = 15;
inline constexpr int blur_progressive = 20;

}  // namespace finn::ui::blur

namespace finn::ui::elevation {

inline constexpr int elev_flat = 0;
inline constexpr int elev_card = 1;
inline constexpr int elev_window = 4;
inline constexpr int elev_popover = 4;
inline constexpr int elev_notification = 4;

}  // namespace finn::ui::elevation

namespace finn::ui::breakpoints {

inline constexpr int bp_phone_max = 599;
inline constexpr int bp_tablet_min = 600;
inline constexpr int bp_tablet_max = 1024;
inline constexpr int bp_desktop_min = 1025;
inline constexpr int bp_tv_min = 1920;

}  // namespace finn::ui::breakpoints

namespace finn::ui::weights {

inline constexpr double glass_panel = 0.65;
inline constexpr double glass_tile = 0.85;
inline constexpr double glass_tile_pass = 0.5714286;
inline constexpr double glass_well = 0.53;
inline constexpr double glass_footer = 0.4;
inline constexpr double surface_scale = 0.92;
inline constexpr double wash_hover = 0.055;
inline constexpr double wash_press = 0.09;

}  // namespace finn::ui::weights

namespace finn::ui::motion {

inline constexpr int surface_open_ms = 280;
inline constexpr int surface_close_ms = 180;
inline constexpr int scrim_in_ms = 200;
inline constexpr int scrim_out_ms = 140;
inline constexpr int hover_in_ms = 110;
inline constexpr int hover_out_ms = 200;
inline constexpr int press_in_ms = 80;
inline constexpr int press_out_ms = 180;
inline constexpr int fill_ms = 200;
inline constexpr int grid_cell_ms = 200;
inline constexpr int grid_step_ms = 22;
inline constexpr int grid_delay_ms = 60;
inline constexpr int clock_tick_ms = 260;
inline constexpr int note_ms = 220;
inline constexpr int theme_ms = 360;

}  // namespace finn::ui::motion

namespace finn::ui::tv {

inline constexpr double type_scale = 1.5;
inline constexpr int min_sp = 32;

}  // namespace finn::ui::tv

#endif
