# Android 17 runtime coverage (`finn-android`)

`finn-android` is FinnOS's clean-room Android application runtime. It replaces
the old Waydroid probe: there is no container, no binder, no Android SDK, no
Google Mobile Services and no network call to a model. It runs as the `finn`
user, installs APKs under `~/.local/share/finn/android/`, and renders through
the same Qt6/Wayland theme as native FinnOS apps.

Target: **Android 17 (API 37)**. This describes the platform version this
runtime checks against, not a claim that the Android framework is implemented.

## What is real

| Area | Implementation | Honest boundary |
|---|---|---|
| ZIP / APK | `src/android/zip_reader.*` — bounds-checked central-directory reader, Stored + Deflate via zlib, CRC-32 verification, traversal rejection | ZIP64 is not decoded; entries above safety limits are refused |
| Binary manifest | `src/android/android_format.cpp` — AXML string pool (UTF-8/UTF-16), resource map, start/end elements, real `Res_value` typed attributes | Unresolved `@string`/`@drawable` references stay empty; they are not guessed |
| Resources | `src/android/android_format.cpp` — `resources.arsc` table/package/type/key pools and bounded entry resolution | Only entry-key resolution; no style, configuration-qualifier or vector-drawable inflation |
| DEX | `src/android/dex_runtime.*` — header verification (magic, size, sections, Adler-32, SHA-1), string/type/proto/field/method/class tables, class-data and code items | Optimised/oat/vdex formats are not loaded |
| Bytecode | `src/android/dex_runtime.cpp` — table-driven Dalvik decoder for a stated subset (moves, constants, branches, invokes, field/array access, arithmetic, returns), with widths and operand decoding | An opcode outside the subset is reported with its code-unit offset; decoding stops rather than guessing a width |
| ART-style analysis | `src/android/art_bridge.*` — method/class tables, invoke-target and string-constant extraction, framework-surface audit, reflection/JNI reporting, strict-subset interpreter | The interpreter executes only const/move/add/sub/return/goto/if-eqz/if-nez; all other methods are logged as skipped |
| Native libraries | ELF header and ABI match for `lib/<abi>/*.so` | Non-host ABI libraries are reported, not emulated |
| Installer | `src/android/android_installer.*` — inspect, install/uninstall, registry, icon extraction from real PNG resources | Only local `.apk` paths; remote URLs are refused with an explicit message |
| UI | `src/android/ui_bridge.*` — binary AXML layout inflation into a view tree, Qt6/Wayland widgets via `FinnWindow`, live FinnOS palette and tokens | Only the modelled widgets (text, button, edit, image, check, switch, seek/progress, linear/frame/relative/scroll/list/recycler); unknown views are labelled as unmodelled |
| Lifecycle | `onCreate`/`onStart`/`onResume` delegated to the DEX bridge; each phase states "executed" or "skipped" | Pause/stop/destroy are not simulated by a `run` invocation |

## What is deliberately not implemented

- The Android framework, Activity Manager, Binder, Zygote and ART runtime.
- JNI: native methods are listed but cannot be called.
- Google Mobile Services, Play Store, Play Integrity and push messaging.
- Full resource qualifier resolution, vector drawables, nine-patch scaling and
  Android's animation system.
- Cross-ABI emulation, runtime permission enforcement and app sandboxing beyond
  the `finn` user's normal privileges.
- Waydroid in any form — the old probe and its `popen("waydroid app list")`
  call are gone.

`finn-android diagnose <apk>` reports the manifest, SDK range, DEX verification,
native libraries, modelled and unmodelled framework classes, unsupported
opcodes and a runnable verdict. A field with no value is absent, never a
plausible default.

## Verification

- Host: `make -C tests/unit test_android && ./tests/unit/test_android` runs
  `android::android_self_test()` plus host-side ZIP/AXML/DEX/installer checks.
- Runtime: `finn-android self-test` runs the clean-room parser self-test.
- VM: build with `cmake --build build --target finn-android`, install the
  binary, then `finn-android diagnose /tmp/F-Droid.apk` and
  `finn-android install /tmp/F-Droid.apk`.
