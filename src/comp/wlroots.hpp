#ifndef FINN_COMP_WLROOTS_HPP
#define FINN_COMP_WLROOTS_HPP

/*
 * wlroots is a C library whose headers declare array parameters as
 * `float matrix[static 9]`. That is C99 syntax and C++ cannot parse it.
 *
 * The two ways out are to write the compositor in C or to keep the C headers
 * out of the C++ translation units. This header does neither: it pre-includes
 * the standard headers the wlroots headers reach (so the C include cannot
 * pull the C++ standard library into the macro) and then erases the `static`
 * keyword for the duration of the include. Nothing else in the tree may use
 * this macro, and it is undefined before this header ends.
 *
 * If wlroots ever ships C++-clean headers, delete this file and include the
 * wlroots headers directly.
 */

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <type_traits>
#include <vector>

#define static
extern "C" {
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}
#undef static

#endif
