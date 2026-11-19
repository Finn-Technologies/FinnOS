# `finn-darwin` framework coverage

This page records what the clean-room macOS runtime in `src/darwin/` actually
provides, and how it reports what it does not. It is the detail behind the
"Incomplete macOS framework coverage" entry in
[`docs/architecture.md`](architecture.md#what-is-not-here-yet).

The rule is the one in [`AGENTS.md`](../AGENTS.md): a missing capability is
reported as missing. `finn-darwin` never installs a plausible-looking stub for
an unimplemented framework, and never lets a binary that needs one start as if
it worked.

## What runs today

The loader executes unmodified 64-bit Mach-O (thin or Universal) on a matching
CPU. On top of `libSystem`, `libobjc` and the Objective-C message-send assembly,
these framework surfaces are real:

- **Foundation / CoreFoundation**: strings, arrays, dictionaries, numbers,
  data, dates, JSON, file management, user defaults, URLs, bundles, process
  info, run loop, error objects, notification center and the common `CF*`
  C APIs.
- **Foundation additions in this slice**:
  - `NSAttributedString` / `NSMutableAttributedString`: `initWithString:`,
    `initWithString:attributes:`, `string`, `length`,
    `attributesAtIndex:effectiveRange:`, `attributedSubstringFromRange:`,
    `isEqualToAttributedString:`, `addAttribute:value:range:`,
    `addAttributes:range:`, `setAttributes:range:`, `removeAttribute:range:`,
    `replaceCharactersInRange:withString:`, `deleteCharactersInRange:` and
    `appendAttributedString:`. Attribute dictionaries are stored per UTF-8 byte
    and effective ranges are computed by comparing adjacent runs.
  - `NSDateFormatter`: `dateFormat` / `setDateFormat:`, `stringFromDate:` and
    `dateFromString:` using a documented `strftime`/`strptime` token subset
    (`yyyy`, `yy`, `MMMM`, `MMM`, `MM`, `dd`, `HH`, `hh`, `mm`, `ss`, `EEEE`,
    `EEE`, `a`, plus non-padded single-letter forms). Formatting and parsing
    are in UTC.
  - `NSCalendar` / `NSDateComponents`: `currentCalendar`,
    `calendarWithIdentifier:` (Gregorian only), `calendarIdentifier`,
    `component:fromDate:`, `components:fromDate:`, `dateFromComponents:`,
    `dateByAddingUnit:value:toDate:options:` and component getters/setters.
    Unsupported calendar identifiers and units return `nil` rather than
    quietly behaving like a Gregorian calendar.
  - `NSNotificationCenter`: `defaultCenter`, `addObserver:selector:name:object:`,
    `removeObserver:`, `removeObserver:name:object:`,
    `postNotificationName:object:userInfo:` and `postNotification:`, plus
    `NSNotification` accessors (`name`, `object`, `userInfo`) and
    `notificationWithName:object:userInfo:`. Posted notifications are
    autoreleased, so an observer may keep one.
  - `NSSearchPathForDirectoriesInDomains`, `NSHomeDirectory` and
    `NSTemporaryDirectory`. User-domain paths are mapped onto `$HOME` and the
    XDG cache/data directories; system-domain paths use the conventional
    `/Applications` and `/Library` trees. Unsupported directory constants
    yield an empty array, never an invented path.
  - Exported string constants: `NSFontAttributeName`,
    `NSForegroundColorAttributeName`, `NSBackgroundColorAttributeName`,
    `NSUnderlineStyleAttributeName` and `NSCalendarIdentifierGregorian`.
- **CoreGraphics / QuartzCore**: RGBA8 bitmap contexts, drawing primitives,
  `CGRect` geometry and `CALayer`.
- **AppKit-on-Qt6**: windows, menus, buttons, text fields/views, scroll views,
  sliders, progress indicators, panels, timers, pasteboard and workspace.
- **Core Swift ABI**: `HeapObject` allocation, ARC retain/release (including
  `_n`, unknown-object and bridged forms), exclusivity markers, `swift_once`,
  slow allocation and ObjC class interop.
- **SwiftUI slice** (`src/darwin/swiftui/`): a minimal compiled SwiftUI app now
  runs. Real ABI-shaped nominal/protocol/conformance descriptors (`Mn`, `Mp`,
  `Mc`) and witness tables back the compiler-emitted metadata symbols; a
  mangled-name parser resolves ordinary Swift names to real metadata (compressed
  symbolic-reference manglings fall back to a documented opaque metadata object
  rather than a fabricated type match); `swift_getWitnessTable`,
  `swift_getTypeByMangledNameInContext*`, `___chkstk_darwin`, Swift `print` and
  the OS-version probe are implemented. The compiled app's own `body` getters
  are invoked, their string literals are extracted from the Mach-O image, and
  the resulting `Text`/`Button`/`VStack`/`WindowGroup` tree is materialised as
  real `NSWindow` → `NSView` → `NSTextField`/`NSButton` widgets through the
  existing AppKit-on-Qt6 bridge. `App.main()` runs the real `NSApplication` loop.
  `@State`/`@Binding` storage and Combine-style `sink`/`send` primitives have
  real semantics and unit tests.
- **Shared C/Objective-C ABI shims**: Foundation geometry (`NSEqualRects`,
  `NSInsetRect`, `NSOffsetRect`, `NSUnionRect`, `NSIntersectionRect`,
  `NSDivideRect`, `NSPointInRect`, `NSContainsRect`, `NSStringFromRect`,
  `NSStringFromRange`) and the `NSZeroPoint` / `NSZeroSize` / `NSZeroRect`
  constants; Foundation reflection (`NSStringFromClass`,
  `NSStringFromSelector`, `NSClassFromString`, `NSSelectorFromString`);
  exported `NSString` constants (appearance names, attribute keys,
  notification names, binding options, locale/file/URL keys, bundle keys);
  libobjc associated objects, selector-backed property accessors, weak
  references zeroed on deallocation, `objc_retainBlock`,
  `objc_begin_catch` / `objc_end_catch`; libdispatch semaphores with real
  blocking; QuartzCore transform math and `CACurrentMediaTime`; and host
  libc / Itanium C++ ABI entries (`fseeko`, `ftello`, `strerror`,
  `pthread_*`, `__Unwind_Resume`, `__cxa_*`, `operator new/delete`). Every
  entry has a real implementation or binds to a real host function; none is a
  trap.
- **Installer**: HTTP(S), `.app`, `.pkg`, `.zip` and `.dmg`.

## How missing capabilities fail

`src/darwin/framework_coverage.cpp` is a single data-driven registry of
unimplemented frameworks and ABI symbols. `status`, `diagnose` and the
loader's launch-failure message all read it, so the answer cannot drift between
commands:

- `finn-darwin status` prints the full, explicit not-implemented list.
- `finn-darwin diagnose <binary | App.app>` prints the linked dylibs, every
  imported and missing symbol, the subset of missing symbols with a known
  explanation, the linked-but-unimplemented frameworks with reasons, and a
  verdict. It exits `0` when runnable, `2` when the binary needs something we
  cannot provide, and `1` on a malformed target. The pre-flight walks the
  bundled `@rpath` dylib closure (so imports satisfied by real bundled
  frameworks such as Sparkle are not reported as missing) and ignores weak
  imports, matching what `load_and_run` actually binds. It prints the entire
  missing list, not a truncated sample.
- `finn-darwin run <binary>` refuses to start when any strong import is
  unresolved. It exits non-zero with a message of the form
  `cannot launch: unresolved macOS symbols (N): ... [reason]`, naming every
  symbol and explaining the ones we know. `--allow-missing` is the explicit
  opt-in that binds those imports to a trap; it is never the default.

A real `swiftc`-compiled "hello world" Mach-O, for example, imports
`_swift_getTypeByMangledNameInContext2` and Swift stdlib metadata such as
`_$ss5print_9separator10terminatoryypd_S2StF`. `diagnose` names both and
explains that Swift type-metadata materialization and the full standard library
are not implemented; `run` refuses to execute it.

## What is genuinely not implemented

| Framework / capability | Why it is not provided here |
|---|---|
| SwiftUI (full framework) | Only the minimal slice above exists. The modifier DSL, most property wrappers, navigation, lists, animations and the full SwiftUI render graph are not implemented. Apps beyond a Text/Button/VStack/WindowGroup hello-world still report their missing symbols honestly. |
| Full Swift standard library and runtime | Core heap/ARC/once ABI plus mangled-name lookup, witness-table resolution and a few stdlib entry points are implemented. Compressed symbolic-reference manglings are not fully demangled, and generics, error existentials, concurrency and stdlib collections remain absent. Some ordinary compiled Swift binaries are still blocked. |
| Metal / MetalKit | No device, command queue, pipeline state, shader compiler or `MTKView`. |
| WebKit | No web renderer or `WKWebView`. |
| CoreData | No managed object model, context or persistent store coordinator. |
| Combine, Security/Keychain, CloudKit, StoreKit, AVFoundation, MapKit, HealthKit | No implementation; linked frameworks and imported symbols are reported by name. |

Other deliberate limits of the implemented subset:

- Attributed-string ranges are UTF-8 byte offsets (matching this runtime's
  `NSString.length`), not UTF-16 indices. Non-ASCII text therefore reports
  different offsets from real Foundation.
- Date formatting is UTC/Gregorian only. Named time zones, locales and
  user-defined calendar identifiers are not applied; unsupported tokens pass
  through literally.
- Block-based notification observers (`addObserverForName:object:queue:usingBlock:`)
  are not implemented. Use the target/selector API.
- `NSDateFormatter` does not implement style-based presets (`dateStyle`,
  `timeStyle`); the explicit `dateFormat` path is the supported one.

## Observed ceiling with real packages

Installation and execution are separate capabilities. The installers handle
real App Store / vendor packages correctly (`.zip` for Itsycal and Hidden Bar,
`.dmg` for Nightfall, `.pkg` for AquaSKK), but launching the installed binary
still fails honestly when it needs frameworks we do not implement:

Counts below are from `finn-darwin diagnose` on the aarch64 dev VM after the
shared ABI shim slice. They include imports from bundled dylibs (Sparkle,
MASShortcut) that the loader really maps; the smaller `run` refusal lists only
the symbols that are still unresolved at bind time.

| Package | Install | Launch diagnosis |
|---|---|---|
| Itsycal | succeeds (real `.zip`) | 224 unresolved across 5 frameworks; 26 at load time (down from 38): Carbon/HIToolbox hotkeys and TIS/UCKeyTranslate (`_RegisterEventHotKey`, `_UCKeyTranslate`, `_TISGetInputSourceProperty`, ...), ten AppKit classes and `__availability_version_check` |
| Hidden Bar | succeeds (real `.zip`) | 577 unresolved across 18 frameworks; 98 at load time (down from 119), including WebKit (`WKWebView`, `WKUserContentController`), Security, ServiceManagement and 40+ AppKit classes |
| AquaSKK | succeeds (real gzip+cpio Payload from an Apple TOC) | 80 unresolved across 4 frameworks at diagnosis, down from the original 134; the remaining ones are libc++ `std::__1::basic_string`, locale and exception typeinfo symbols |
| Nightfall (3.1.0) | succeeds (looping `Applications` symlink tolerated) | 671 unresolved across 15 frameworks; 513 SwiftUI/Combine mangled metadata and dispatch thunks |
| Rectangle (2.0.4) | succeeds (real `.dmg`) | 1128 unresolved across 19 frameworks; links SwiftUI and Combine; 98 at load time (down from 119) including WebKit (`WKWebView`, `WKContentRuleListStore`), Security/CodeSigning transforms, ServiceManagement, IOKit power assertions and 40+ AppKit classes |
| Figma Agent | succeeds (real `.dmg`) | architecture mismatch: x86_64-only binary on an aarch64 host; 190 unresolved as well |

This is the ceiling, not a bug to paper over: a `.app` can be unpacked,
registered and given an icon while `finn-darwin diagnose` still refuses to run
it. Neither path claims the app is runnable when it is not. The SwiftUI wall is
now partial rather than total: a minimal `Text`/`Button`/`VStack`/`WindowGroup`
app imports a few dozen compiler-generated metadata symbols, and those are
backed by real descriptors and thunks in `src/darwin/swiftui/`. Larger SwiftUI
apps still import hundreds of additional `_$s7SwiftUI...`/`_$s7Combine...`
descriptors, modifiers and dispatch thunks that are not registered, and
`diagnose` keeps naming every one of them instead of inventing metadata. The
full render graph remains Apple-private and is not claimed.

## Real downloaded apps: current status

The table below records `finn-darwin diagnose` on the aarch64 dev VM for the
real packages in `/tmp/finnapps/`, after installing each into
`~/.local/share/finn/macos/Applications`. The diagnose count includes imports of
bundled dylibs (Sparkle, MASShortcut) that the loader really maps; the smaller
`run` refusal list is the set that actually blocks launch.

| App | Install | Diagnose (baseline) | Load-time refusal (baseline) | Current |
|---|---|---|---|---|
| Itsycal 0.29.0 | succeeds (real `.zip`) | 224 | 26 | Carbon/HIToolbox hotkeys + late AppKit classes implemented in `carbon_compat.cpp` |
| Hidden Bar 1.13.2 | succeeds (real `.zip`) | 569 | 119 | not yet addressed |
| Nightfall 3.1.0 | succeeds (real `.dmg`) | 641 | — | not yet addressed; 513 SwiftUI/Combine gaps |
| Rectangle 2.0.4 | succeeds (real `.dmg`) | 1101 | — | not yet addressed; links SwiftUI/Combine/WebKit/Security |
| AquaSKK 4.7.3 | succeeds (real `.pkg`) | 80 | — | not yet addressed; libc++ string/locale/exception typeinfo |
| Figma Agent | succeeds (real `.dmg`) | 190 | — | **architecture mismatch**: x86_64-only binary on an aarch64 host; no amount of symbol coverage can run it here, and this runtime does not claim to emulate x86_64 |

## Verification

On the host (Apple Silicon with Xcode) the unit suite includes a live test that
compiles a Swift binary with `xcrun swiftc`, diagnoses it and checks that launch
fails cleanly with an explained error. It skips itself when `swiftc` is not
available.

```sh
make -C tests/unit test_darwin && ./tests/unit/test_darwin
make test-unit
make -C src/darwin finn-darwin
finn-darwin status
finn-darwin diagnose /path/to/App.app
```
