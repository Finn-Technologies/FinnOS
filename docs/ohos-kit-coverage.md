# OpenHarmony kit coverage in `finn-ohos`

`finn-ohos` is a clean-room runtime for OpenHarmony 6.1 LTS (API Level 23).
This page records exactly which ArkTS modules, ArkUI components and Node-API
functions are implemented, which parts are deliberate clean-room substitutions,
and which kits cannot be bridged without hardware or proprietary system
services. It is the companion to the `finn-ohos` section of
[architecture.md](architecture.md); the "what is not here yet" list there stays
true.

A missing capability is reported as missing. `finn-ohos diagnose` exits `0`
for a compatible package, `2` for a package that requires an unimplemented
kit or an incompatible ABI/API, and `1` when the file itself cannot be read.

## Implemented ArkTS modules

These are installed as both globals and module specifiers, so `requireModule`
and bytecode `TryLdGlobalByName*` find them:

| Module specifier | Global | What is really implemented |
|---|---|---|
| `@ohos.hilog` | `hilog` | `debug/info/warn/error/fatal` (plus `d/i/w/e` aliases) with `%{public}s`-style substitution into the real runtime log. |
| `@ohos.deviceInfo` | `deviceInfo` | `sdkApiVersion` (23), `osFullName`, brand/manufacture/model, `deviceType`, and the **actual host** `abiList`. |
| `@ohos.router` | `router` | `pushUrl`, `replaceUrl`, `back`, `getLength`, backed by a real page stack. |
| `@ohos.data.preferences` | `preferences` | `getPreferencesSync`, `getSync`, `putSync`, `hasSync`, `deleteSync`, `flushSync`, persisted per bundle under `~/.local/share/finn/ohos/prefs/`. |
| `@ohos.file.fs` | `fs` | `accessSync`, `mkdirSync`, `writeTextSync`, `readTextSync`, `unlinkSync`, sandboxed to the bundle data directory. |
| `@ohos.window` | `window` | `getMainWindowSync`, `setWindowBackgroundColor`, `WindowStage.loadContent` from `UIAbility.onWindowStageCreate`. |
| `@ohos.app.ability.UIAbility` | `UIAbility` | `onCreate` → `onWindowStageCreate` → `onForeground` → `onBackground` → `onDestroy` with API 23 `LaunchParam.startupTimeMs`. |
| `@ohos.util` | `util` | `generateRandomUUID()` (RFC 4122 v4 from host entropy), `getRandom(min,max)` (inclusive), `Base64.encode/decode` (strict RFC 4648), and `Base64Encoder`/`Base64Decoder` with `encodeSync`/`decodeSync`. |
| `@ohos.bundle.bundleManager` | `bundleManager` | `getBundleInfoForSelf()` returning `name`, `bundleName`, `versionName`, `versionCode`, `moduleName`, `mainElement`, `apiVersion` from the real parsed `module.json`. |
| `@ohos.app` | `app` | `getAppInfo()` returning real bundle name/version/API from the parsed `module.json`. |
| `@ohos.curves` | `curves` | Real easing evaluators `linear`, `easeIn`, `easeOut`, `easeInOut` with input clamped to `0..1`. |
| `@ohos.matrix4` | `matrix4` | Real column-major 4×4 `identity`, `translation`, `scaling`, `multiply` over 16-element arrays. |

ECMAScript globals compiled ArkTS bytecode expects are also real: `Number`,
`String`, `Boolean`, and `JSON.stringify` / `JSON.parse` (backed by the shared
JSON5 parser). An unimplemented member of a builtin module is logged as
`[module] … skipped: not implemented by finn-ohos`, never silently claimed to
have worked.

`@kit.ArkUI`, `@kit.AbilityKit`, `@kit.ArkData`, `@kit.CoreFileKit`,
`@kit.BasicServicesKit` and `@kit.PerformanceAnalysisKit` are recognised as
umbrella specifiers that map onto the modules above.

### Deliberate clean-room differences

These are not bugs and are not claimed to be byte-for-byte OpenHarmony
behaviour:

- `bundleManager.getBundleInfoForSelf()` returns the `BundleInfo` object
  synchronously. There is no ArkTS Promise/event loop, no binder IPC and no
  bundle service in FinnOS. Fields absent from `module.json` stay empty or
  zero — nothing is invented.
- `util.Base64` is a FinnOS convenience wrapper over the same strict RFC 4648
  codec used by `Base64Encoder`/`Base64Decoder`; malformed input returns an
  empty string rather than a guessed decode.
- `util.encodeSync`/`decodeSync` operate on UTF-8 strings, not ArkTS
  `Uint8Array` buffers.
- Randomness comes from `std::random_device`; there is no deterministic seed
  and no fake UUID.

### Why `@ohos.app`, `@ohos.curves` and `@ohos.matrix4` are supported

Real HAP bytecode contains Panda class descriptors such as `L@ohos.app;`,
`L@ohos.curves;` and `L@ohos.matrix4;`. These are compiler-generated class
names, not `@ohos.*` module imports; an earlier diagnostics pass mistook them
for missing kits. They are now backed by the real shims above and classified
as supported. Genuinely hardware/proprietary kits are still rejected.

## Panda bytecode and ES module imports

Opcode numbers and operand widths follow the public Panda/ArkTS ISA
(`arkcompiler_ets_runtime/ecmascript/isa.yaml`), verified against the real
`modules.abc` in `Calc_Demo.hap`, `Clock_Demo.hap`, `ScreenShot.hap` and
`FilePicker.hap`. In particular `lda.str` uses a **2-byte** string id, and the
call family uses `0x29..0x30` (not a local renumbering). Implemented additions:
`ldexternalmodulevar`/`ldlocalmodulevar` (`0x7e`/`0x7d`),
`throw.undefinedifholewithname` (`0xfe 0x09`), `ldlexvar`/`stlexvar`
(`0x3c`/`0x3d`), `newlexenv` (`0x09`), `ldglobal` (`0x6d`),
`createobjectwithbuffer` (`0x07`), `definefunc` (`0x33`),
`definefieldbyname` (`0xdb`) and `neg` (`0x1f`).

Panda module records are read from the `.abc`: their regular-import entries are
the index space used by `ldexternalmodulevar`. An import of a supported kit
binds to that real shim. An app-internal import finn-ohos cannot link gets an
honest placeholder whose method calls are logged as skipped; chains stay alive
so the UIAbility lifecycle can complete. Unimplemented opcodes still fail with
an explicit `unimplemented Panda bytecode opcode 0xNN at pc=… in <method>`
error — they are never guessed.

## ArkUI components

`ArkUiEngine` parses declarative JSON/JSON5 trees, assigns ids, fires
`aboutToAppear`/`onAttach`/`onDetach`/`aboutToDisappear`, and keeps reactive
`@State` bindings. Implemented components:

`Column`, `Row`, `Stack`, `Flex`, `Scroll`, `Tabs`, `TabContent`, `Swiper`,
`RelativeContainer`, `Text`, `Button`, `TextInput`, `Image`, `Toggle`,
`Slider`, `Progress`, `List`, `ListItem`, `Grid`, `GridItem`, `Navigation`,
`Divider`, `Blank`, `AlertDialog`.

`Grid.multiSelect` and the API 23 lifecycle hooks are real and exercised by
the unit tests. The Qt6 bridge renders live widgets when built with
`FINN_OHOS_QT6`; headless builds render and validate the same tree as text.

Image resource references (`$media:name`, `$rawfile:name` and
`$r('app.media.name')`) are resolved against **real files** under
`resource_root()` (for example `resources/base/media/logo.png`). Numeric
`resources.index` ID-to-path decoding is **not** implemented: lookup is by
resource name across the real qualifier directories. A missing resource is
reported as `[Image missing: …]`; no placeholder path is fabricated.

`RelativeContainer` is parsed and mounted, but the Qt bridge lays its children
out in declaration order instead of evaluating ArkUI relative-position
constraints. The layout is simplified; the component is not claimed to be
pixel-faithful.

## Node-API (NAPI) host bridge

The clean-room C ABI implements: `napi_module_register`,
`napi_define_properties`, `napi_create_{string_utf8,int32,int64,uint32,double,
object,array,array_with_length}`, `napi_get_{undefined,null,boolean}`,
`napi_typeof`, `napi_is_array`, `napi_get_array_length`,
`napi_{set,get}_element`, `napi_{get,set}_named_property`, `napi_get_cb_info`,
`napi_get_value_{int32,int64,double,bool,string_utf8}`, and `napi_throw_error`
(the message is recorded and exposed as `AbcRuntime::last_napi_error()`).

`load_native_module()` uses `dlopen` and only registers modules built for the
host ABI. Non-host `.so` files are reported as ABI mismatches, not loaded.
The rest of the Node-API surface (async work, threadsafe functions, references,
BigInt, external values, promises) is **not** implemented.

## Genuinely unavailable kits

The following require hardware, proprietary SDKs, cloud services or a system
permission model FinnOS does not have. They are **not** stubbed:

- `ArkWeb` / `@ohos.web.webview` — no web engine embedder in the clean-room UI stack.
- `DistributedDataObject` / `distributedKVStore` — no distributed soft-bus.
- Telephony, NFC, Bluetooth, Wi-Fi direct, location — no modem/radio stack.
- Camera, audio/video capture, sensors, MapKit — no hardware/service bridge.
- Account, Push, Payment, Share, GameController kits — need cloud/device services.
- CryptoArchitectureKit and cryptographic signature enforcement — no
  OpenHarmony signing root is shipped; `finn-ohos` inspects ABI and bytecode
  checksums but does not validate a vendor signature.

`finn-ohos diagnose` classifies every `@kit.*` / `@ohos.*` import it finds in
`module.json` and `modules.abc` into three buckets, printed as:

```
Supported kits:                    <n>
  + @ohos.util
Missing hardware/proprietary kits: <n>
  ! @kit.CameraKit
Unknown kit imports:               <n>
  ? @ohos.somethingUnrecognised
```

The first bucket is backed by the modules above. The second contains known
hardware/proprietary kits and makes the package non-runnable. The third is an
honest "we cannot classify this import" warning, never a claim that it works.
`run_ohos_target()` and `install_ohos_package()` refuse to run or install a
package that imports a missing hardware kit, naming the kit in the error.
Non-host ABI `.so` files are reported as ABI mismatches and are never loaded;
`diagnose` marks the package non-runnable for them.

## Verifying this page

```sh
make -C tests/unit test_ohos && ./tests/unit/test_ohos
make test-unit
src/ohos/finn-ohos status
src/ohos/finn-ohos diagnose path/to/app.hap
```

The unit suite exercises every module function, the NAPI surface, ArkUI
containers, real resource resolution, ABI mismatch handling, the ES module
opcodes (`0x7e`, `0xfe/0x09`, packed lexicals) and the clean failure of a
`@kit.CameraKit` package through the CLI.

The VM verification used the real `Calc_Demo.hap`, `Clock_Demo.hap`,
`ScreenShot.hap` and `FilePicker.hap`: all four now `diagnose` as compatible
(exit `0`), and `ScreenShot`/`FilePicker` launch through `finn-session` and
render their honest ArkUI fallback cards while logging each unlinked app
import as skipped. The fallback card is deliberate — a real screen capture
implementation is not bridged.

Five more real HAPs from `openharmony/applications_hap` were installed and
launched on the aarch64 dev VM: `Music_Demo.hap`
(`ohos.samples.distributedmusicplayer`), `Note.hap` (`com.ohos.note`),
`CalendarData.hap` (`com.ohos.calendardata`), `Contacts.hap`
(`com.ohos.contacts`) and `AudioPicker.hap` (its manifest is
`com.ohos.filepicker`). All diagnose-compatible (exit `0`), all stayed alive
after `finn-session --launch`, and their windows render the same honest
manifest/bytecode card (`bundle (MainAbility)`, `API n | m Panda classes`).
The card is what `finn-ohos` can truthfully show: the packages ship compiled
Panda bytecode (`ets/modules.abc`) rather than a declarative page JSON tree,
and the ArkUI DSL-to-widget generator inside that bytecode is not part of this
clean-room VM. `load_declarative_page("pages/Index")` therefore fails, and
`AbcRuntime::run_ability_lifecycle` mounts the card with the real inspected
facts instead of inventing an app UI.

Two real Panda ISA gaps found in those HAPs were fixed in `src/ohos/`:
`0x7a` (`stownbyname`, which made `Contacts.hap`'s `onCreate` abort) and
`0x23`/`0x24` (`istrue`/`isfalse`, which made its `onWindowStageCreate` abort).
Both are implemented with their documented format and strict semantics and are
covered by `test_own_property_store_opcode`. `Contacts.hap` now reaches
`onWindowStageCreate` and logs every unlinked app import as skipped, but still
stops at a call on an `undefined` value produced by a skipped Promise-style
module chain (`@ohos.window.getTopWindow(...).then(...)`); that is the honest
wall for that app's lifecycle, not an opcode gap.
