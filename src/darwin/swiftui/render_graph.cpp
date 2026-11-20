// Real render graph and descriptor coverage for the SwiftUI slice.
//
// See render_graph.hpp for the honesty contract and the derived ABI facts.
// This file never invents a view: every registered thunk either applies a real
// NSView/CALayer operation or leaves its witness slot null.

#include "render_graph.hpp"

#include "abi_metadata.hpp"

#include "../objc_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace finn::darwin::swiftui {
namespace {

using finn::darwin::ObjcId;
using finn::darwin::objc_send_name;

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// ---------------------------------------------------------------------------
// Descriptor kinds and the captured symbol table.
//
// Each row is a symbol that Nightfall and/or Rectangle import and that the
// minimal slice does not already own.  The list was captured with
// `finn-darwin diagnose` on the aarch64 VM, demangled with Xcode swift-demangle
// 6.4, and filtered to SwiftUI/Combine descriptor symbols.
// ---------------------------------------------------------------------------
enum class SymbolKind {
    NOMINAL_STRUCT,
    NOMINAL_ENUM,
    NOMINAL_CLASS,
    PROTOCOL,
    CONFORMANCE,
    WITNESS,
    METHOD,
    METADATA_ACCESSOR,
    METADATA,
    ASSOCTYPE,
    INIT_EMPTY_MARKER,   // zero-stored-property value type init()
    INIT_EDGE_INSETS,    // EdgeInsets.init(_all: CGFloat)
};

struct SymbolRow {
    const char* symbol;
    SymbolKind kind;
};

static const SymbolRow kSymbols[] = {
    // Initializers whose ABI and semantics are fully derivable: empty marker
    // value types and the four-optional EdgeInsets value.
    {"_$s7SwiftUI10EdgeInsetsV4_allAC12CoreGraphics7CGFloatV_tcfC", SymbolKind::INIT_EDGE_INSETS},
    {"_$s7SwiftUI15MenuPickerStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI16PlainButtonStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI17OpacityTransitionVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI17SwitchToggleStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI19BorderedButtonStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI19CheckboxToggleStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI21BorderlessButtonStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI21RadioGroupPickerStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI27RoundedBorderTextFieldStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
    {"_$s7SwiftUI28BorderedProminentButtonStyleVACycfC", SymbolKind::INIT_EMPTY_MARKER},
  // NOMINAL_STRUCT: 78 symbols
  {"_$s7Combine10PublishersO16RemoveDuplicatesVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7Combine10PublishersO4DropVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7Combine10PublishersO9ReceiveOnVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI10GridLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI10ScrollViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI10_ShapeViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI11_ClipEffectVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI12_FrameLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI13_HStackLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI13_StrokedShapeVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI13_TaskModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI13_ZStackLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI14MoveTransitionVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI14ObservedObjectVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI14_PaddingLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI15MenuPickerStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI15ModifiedContentVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI15StrokeShapeViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI15_RotationEffectVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16GroupedFormStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16PlainButtonStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16RoundedRectangleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16SubscriptionViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16TagValueTraitKeyVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16_FixedSizeLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16_FlexFrameLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI16_OverlayModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI17EnvironmentValuesVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI17OpacityTransitionVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI17SwitchToggleStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI18LocalizedStringKeyVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI18TransitionTraitKeyVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI18_AnimationModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI18_AspectRatioLayoutVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19BorderedButtonStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19CheckboxToggleStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19HorizontalAlignmentVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19_BackgroundModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19_ConditionalContentVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI19_GridAnchorModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI20_HoverRegionModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI21BorderlessButtonStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI21RadioGroupPickerStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI21_ContentShapeModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI21_TraitWritingModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI24_BackgroundStyleModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI25_AlignmentWritingModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI25_AppearanceActionModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI27RoundedBorderTextFieldStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI28BorderedProminentButtonStyleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI30_EnvironmentKeyWritingModifierVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI4FontVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI4FormVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI4GridVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI5ColorVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI5GroupVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI5ImageVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI5LabelVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI5StateVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6CircleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6HStackVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6PickerVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6SliderVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6SpacerVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6ToggleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI6ZStackVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7AnyViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7BindingVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7CapsuleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7DividerVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7ForEachVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7GridRowVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI7SectionVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI8BindableVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI9EmptyViewVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI9LazyStateVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI9RectangleVMn", SymbolKind::NOMINAL_STRUCT},
  {"_$s7SwiftUI9TextFieldVMn", SymbolKind::NOMINAL_STRUCT},

  // NOMINAL_ENUM: 5 symbols
  {"_$s7SwiftUI11ControlSizeOMn", SymbolKind::NOMINAL_ENUM},
  {"_$s7SwiftUI13TextAlignmentOMn", SymbolKind::NOMINAL_ENUM},
  {"_$s7SwiftUI4FontV6DesignOMn", SymbolKind::NOMINAL_ENUM},
  {"_$s7SwiftUI5ImageV21TemplateRenderingModeOMn", SymbolKind::NOMINAL_ENUM},
  {"_$s7SwiftUI9LazyStateV7StorageOMn", SymbolKind::NOMINAL_ENUM},

  // NOMINAL_CLASS: 4 symbols
  {"_$s7Combine14AnyCancellableCMn", SymbolKind::NOMINAL_CLASS},
  {"_$s7Combine25ObservableObjectPublisherCMn", SymbolKind::NOMINAL_CLASS},
  {"_$s7SwiftUI11AnyLocationCMn", SymbolKind::NOMINAL_CLASS},
  {"_$s7SwiftUI19NSHostingControllerCMn", SymbolKind::NOMINAL_CLASS},

  // PROTOCOL: 4 symbols
  {"_$s7Combine11CancellableMp", SymbolKind::PROTOCOL},
  {"_$s7Combine16ObservableObjectMp", SymbolKind::PROTOCOL},
  {"_$s7SwiftUI11AlignmentIDMp", SymbolKind::PROTOCOL},
  {"_$s7SwiftUI19NSViewRepresentableMp", SymbolKind::PROTOCOL},

  // CONFORMANCE: 39 symbols
  {"_$s7Combine10PublishersO16RemoveDuplicatesVy_xGAA9PublisherAAMc", SymbolKind::CONFORMANCE},
  {"_$s7Combine10PublishersO4DropVy_xGAA9PublisherAAMc", SymbolKind::CONFORMANCE},
  {"_$s7Combine10PublishersO9ReceiveOnVy_xq_GAA9PublisherAAMc", SymbolKind::CONFORMANCE},
  {"_$s7Combine14AnyCancellableCSHAAMc", SymbolKind::CONFORMANCE},
  {"_$s7Combine14AnyCancellableCSQAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI11_ClipEffectVyxGAA12ViewModifierAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI14LabeledContentVyxq_GAA4ViewA2aERzAaER_rlMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI14MoveTransitionVAA0D0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI15ModifiedContentVyxq_GAA4ViewA2aERzAA0E8ModifierR_rlMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI16GroupedFormStyleVAA0dE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI16PlainButtonStyleVAA09PrimitivedE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI16SubscriptionViewVyxq_GAA0D0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI16TagValueTraitKeyVyxGAA05_VieweF0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI16_OverlayModifierVyxGAA04ViewD0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI17OpacityTransitionVAA0D0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI17SwitchToggleStyleVAA0dE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI18_AnimationModifierVyxGAA04ViewD0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI19BorderedButtonStyleVAA09PrimitivedE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI19CheckboxToggleStyleVAA0dE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI19_ConditionalContentVyxq_GAA4ViewA2aERzAaER_rlMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI19_GridAnchorModifierVAA04ViewE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI21BorderlessButtonStyleVAA09PrimitivedE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI21_ContentShapeModifierVyxGAA04ViewE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI21_TraitWritingModifierVyxGAA04ViewE0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI27RoundedBorderTextFieldStyleVAA0efG0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI28BorderedProminentButtonStyleVAA09PrimitiveeF0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI30_EnvironmentKeyWritingModifierVyxGAA04ViewF0AAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI4FormVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI4GridVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI5LabelVyxq_GAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI6ButtonVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI6HStackVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI6PickerVyxq_q0_GAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI6ToggleVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI6ZStackVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI7ForEachVyxq_q0_GAA4ViewA2aER0_rlMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI9RectangleVAA5ShapeAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI9TextFieldVyxGAA4ViewAAMc", SymbolKind::CONFORMANCE},
  {"_$s7SwiftUI9TupleViewVyxGAA0D0AAMc", SymbolKind::CONFORMANCE},

  // WITNESS: 14 symbols
  {"_$s7Combine14AnyCancellableCAA0C0AAWP", SymbolKind::WITNESS},
  {"_$s7Combine25ObservableObjectPublisherCAA0D0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI12_FrameLayoutVAA12ViewModifierAAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI13_TaskModifierVAA04ViewD0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI14_PaddingLayoutVAA12ViewModifierAAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI15MenuPickerStyleVAA0dE0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI16_FlexFrameLayoutVAA12ViewModifierAAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI21RadioGroupPickerStyleVAA0eF0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI25_AlignmentWritingModifierVAA04ViewE0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI25_AppearanceActionModifierVAA04ViewE0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI4TextVAA4ViewAAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI5ImageVAA4ViewAAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI7AnyViewVAA0D0AAWP", SymbolKind::WITNESS},
  {"_$s7SwiftUI9EmptyViewVAA0D0AAWP", SymbolKind::WITNESS},

  // METHOD: 14 symbols
  {"_$s7Combine16ObservableObjectP16objectWillChange0ceF9PublisherQzvgTq", SymbolKind::METHOD},
  {"_$s7SwiftUI11AlignmentIDP12defaultValue2in12CoreGraphics7CGFloatVAA14ViewDimensionsV_tFZTq", SymbolKind::METHOD},
  {"_$s7SwiftUI11AlignmentIDP16_combineExplicit10childValue_4intoy12CoreGraphics7CGFloatV_SiAISgztFZTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP04makeC07context0C4TypeQzAA0cD7ContextVyxG_tFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP06updateC0_7contexty0C4TypeQz_AA0cD7ContextVyxGtFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP09dismantleC0_11coordinatory0C4TypeQz_11CoordinatorQztFZTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP12sizeThatFits_6nsView7contextSo6CGSizeVSgAA08ProposedI4SizeV_0C4TypeQzAA0cD7ContextVyxGtFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP14_layoutOptionsyAA013_PlatformViewd6LayoutF0V0C4TypeQzFZTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP15makeCoordinator0F0QzyFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP19_identifiedViewTree2inAA011_IdentifiedfG0O0C4TypeQz_tFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP21_overrideLayoutTraits_3foryAA01_fG0Vz_0C4TypeQztFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP21_overrideSizeThatFits_2in6nsViewySo6CGSizeVz_AA09_ProposedF0V0C4TypeQztFTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP24_modifyBridgedViewInputsyyAA01_gH0VzFZTq", SymbolKind::METHOD},
  {"_$s7SwiftUI19NSViewRepresentableP35_invalidatesSizeOnConstraintChangesSbvgZTq", SymbolKind::METHOD},

  // METADATA_ACCESSOR: 37 symbols
  {"_$s7Combine14AnyCancellableCMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI11ControlSizeOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI13_TaskModifierVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI14LabeledContentVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI14MoveTransitionVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI14SubmitTriggersVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI14ViewDimensionsVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI15MenuPickerStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI15ModifiedContentVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI16GroupedFormStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI16PlainButtonStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI16RoundedRectangleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI16TagValueTraitKeyVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI17OpacityTransitionVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI17SwitchToggleStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI18LocalizedStringKeyV0D13InterpolationVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI18RoundedCornerStyleOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI19BorderedButtonStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI19CheckboxToggleStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI19_ConditionalContentV7StorageOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI19_ConditionalContentVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI21BorderlessButtonStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI21RadioGroupPickerStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI21_TraitWritingModifierVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI23PopoverAttachmentAnchorOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI27RoundedBorderTextFieldStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI28BorderedProminentButtonStyleVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI4FontV6DesignOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI5ImageV12ResizingModeOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI5ImageV21TemplateRenderingModeOMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI6ButtonVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI6HStackVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI6VStackVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI7DividerVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI8BindableVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI9LazyStateVMa", SymbolKind::METADATA_ACCESSOR},
  {"_$s7SwiftUI9TupleViewVMa", SymbolKind::METADATA_ACCESSOR},

  // METADATA: 9 symbols
  {"_$s7SwiftUI14_PaddingLayoutVN", SymbolKind::METADATA},
  {"_$s7SwiftUI19_GridAnchorModifierVN", SymbolKind::METADATA},
  {"_$s7SwiftUI25_AlignmentWritingModifierVN", SymbolKind::METADATA},
  {"_$s7SwiftUI4TextVN", SymbolKind::METADATA},
  {"_$s7SwiftUI5ImageVN", SymbolKind::METADATA},
  {"_$s7SwiftUI6SpacerVN", SymbolKind::METADATA},
  {"_$s7SwiftUI7AnyViewVN", SymbolKind::METADATA},
  {"_$s7SwiftUI9EmptyViewVN", SymbolKind::METADATA},
  {"_$s7SwiftUI9RectangleVN", SymbolKind::METADATA},

  // ASSOCTYPE: 1 symbols
  {"_$s7Combine16ObservableObjectP0C19WillChangePublisherAC_AA0F0Tn", SymbolKind::ASSOCTYPE},
};

// ---------------------------------------------------------------------------
// Real descriptor objects.
//
// The first 24 bytes of a Swift 6.4 nominal descriptor are
// { flags:u32, parent:rel32, name:rel32, access:rel32, fields:rel32,
//   generics:rel32 }.  This was read from a linked swiftc-6.4 dylib, and it
// matches the repository's SwiftNominalTypeDescriptor layout.  Flags are
// 0x50 | discriminator (0x51 struct, 0x52 enum, 0x50 class).
// ---------------------------------------------------------------------------
struct NominalBlock {
    SwiftNominalTypeDescriptor desc;
    char name[112];
};
static_assert(offsetof(SwiftNominalTypeDescriptor, name) == 0x08);

const void* make_nominal(const char* symbol, unsigned discriminator) {
    auto* block = static_cast<NominalBlock*>(std::calloc(1, sizeof(NominalBlock)));
    if (!block) return nullptr;
    std::strncpy(block->name, symbol + 1, sizeof(block->name) - 1);
    block->desc.flags = 0x50u | (discriminator & 0x3u);
    block->desc.name = static_cast<RelativePointer>(
        reinterpret_cast<char*>(block->name) -
        reinterpret_cast<char*>(&block->desc.name));
    return &block->desc;
}

struct ProtocolBlock {
    SwiftProtocolDescriptor desc;
    char name[112];
};

const void* make_protocol(const char* symbol) {
    auto* block = static_cast<ProtocolBlock*>(std::calloc(1, sizeof(ProtocolBlock)));
    if (!block) return nullptr;
    std::strncpy(block->name, symbol + 1, sizeof(block->name) - 1);
    // Swift 6.4 protocol descriptor flags are 0x00010043 (compiled snippet).
    block->desc.flags = 0x00010043;
    block->desc.name = static_cast<RelativePointer>(
        reinterpret_cast<char*>(block->name) -
        reinterpret_cast<char*>(&block->desc.name));
    return &block->desc;
}

// A real Swift method descriptor: { flags:u32, name:rel32, <name bytes> }.
struct MethodBlock {
    std::uint32_t flags;
    RelativePointer name;
    char bytes[160];
};

const void* make_method(const char* symbol) {
    auto* block = static_cast<MethodBlock*>(std::calloc(1, sizeof(MethodBlock)));
    if (!block) return nullptr;
    std::strncpy(block->bytes, symbol + 1, sizeof(block->bytes) - 1);
    block->flags = 0x0000u;  // a plain method requirement
    block->name = static_cast<RelativePointer>(
        reinterpret_cast<char*>(block->bytes) -
        reinterpret_cast<char*>(&block->name));
    return block;
}

// ---------------------------------------------------------------------------
// Value witness table for our opaque view tokens.  The token is a pointer to a
// process-lifetime bridge record; copy/destroy of the opaque Swift value is a
// no-op, which is the same contract abi_metadata.cpp uses for the minimal
// slice.  No behaviour is claimed beyond that.
// ---------------------------------------------------------------------------
void rg_noop(void*, void*, const void*) {}
void rg_noop1(void*) {}

SwiftValueWitnessTable make_rg_vwt() {
    SwiftValueWitnessTable vwt{};
    vwt.destroy = reinterpret_cast<void*>(&rg_noop1);
    vwt.initializeWithCopy = reinterpret_cast<void*>(&rg_noop);
    vwt.assignWithCopy = reinterpret_cast<void*>(&rg_noop);
    vwt.initializeWithTake = reinterpret_cast<void*>(&rg_noop);
    vwt.assignWithTake = reinterpret_cast<void*>(&rg_noop);
    vwt.deinitialize = reinterpret_cast<void*>(&rg_noop1);
    vwt.size = reinterpret_cast<void*>(std::uintptr_t{24});
    vwt.stride = reinterpret_cast<void*>(std::uintptr_t{32});
    vwt.extraInhabitant = reinterpret_cast<void*>(std::uintptr_t{32});
    return vwt;
}

const SwiftValueWitnessTable& rg_vwt() {
    static const SwiftValueWitnessTable v = make_rg_vwt();
    return v;
}

struct MetadataBlock {
    const SwiftValueWitnessTable* vwt;
    SwiftStructMetadata meta;
};

const void* make_metadata(const char* symbol) {
    auto* box = static_cast<MetadataBlock*>(std::calloc(1, sizeof(MetadataBlock)));
    if (!box) return nullptr;
    auto* desc_block = static_cast<NominalBlock*>(
        const_cast<void*>(make_nominal(symbol, 1)));
    if (!desc_block) {
        std::free(box);
        return nullptr;
    }
    box->vwt = &rg_vwt();
    box->meta.kind = static_cast<std::uintptr_t>(MetadataKind::Struct);
    box->meta.desc = &desc_block->desc;
    return &box->meta;
}

// ---------------------------------------------------------------------------
// Opaque view tokens and real NSView/CALayer operations.
//
// This module owns its token registry so that it links without the separate
// state_and_binding translation unit (which defines colliding registration
// entry points).  Every token maps to a real NSView, or to no view at all.
// ---------------------------------------------------------------------------
struct RgRect {
    double x;
    double y;
    double width;
    double height;
};

struct RgViewRecord {
    ObjcId view = nullptr;
};

std::mutex& rg_views_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<void*, RgViewRecord>& rg_views() {
    static std::unordered_map<void*, RgViewRecord> m;
    return m;
}

bool responds(ObjcId obj, const char* sel) {
    return obj != nullptr && finn::darwin::finn_objc_respondsToSelector(
                                 obj, finn::darwin::finn_sel_registerName(sel));
}

ObjcId new_view(double x, double y, double w, double h) {
    ObjcClass* cls = finn::darwin::finn_objc_getClass("NSView");
    if (!cls) return nullptr;
    ObjcId view = finn::darwin::finn_objc_alloc_object(cls, 0);
    if (!view || !responds(view, "initWithFrame:")) return view;
    return objc_send_name<ObjcId>(view, "initWithFrame:", RgRect{x, y, w, h});
}

void* rg_token_create() {
    return new (std::nothrow) RgViewRecord();
}

bool rg_bind(void* token, ObjcId view) {
    if (!token) return false;
    std::lock_guard<std::mutex> lk(rg_views_mutex());
    rg_views()[token].view = view;
    return view != nullptr;
}

ObjcId rg_view(void* token) {
    if (!token) return nullptr;
    std::lock_guard<std::mutex> lk(rg_views_mutex());
    auto it = rg_views().find(token);
    return it == rg_views().end() ? nullptr : it->second.view;
}

void rg_token_destroy(void* token) {
    if (!token) return;
    std::lock_guard<std::mutex> lk(rg_views_mutex());
    rg_views().erase(token);
}

RgRect rg_view_frame(ObjcId view) {
    if (!responds(view, "frame")) return {0.0, 0.0, 0.0, 0.0};
    return objc_send_name<RgRect>(view, "frame");
}

bool rg_apply_frame(ObjcId view, double width, double height) {
    if (!responds(view, "setFrame:")) return false;
    RgRect frame = rg_view_frame(view);
    if (std::isfinite(width) && width >= 0.0) frame.width = width;
    if (std::isfinite(height) && height >= 0.0) frame.height = height;
    objc_send_name<void>(view, "setFrame:", frame);
    return true;
}

bool rg_apply_padding(ObjcId view, double top, double leading,
                      double bottom, double trailing) {
    if (!responds(view, "setFrame:")) return false;
    RgRect frame = rg_view_frame(view);
    frame.x -= leading;
    frame.y -= bottom;
    frame.width += leading + trailing;
    frame.height += top + bottom;
    frame.width = std::max(0.0, frame.width);
    frame.height = std::max(0.0, frame.height);
    objc_send_name<void>(view, "setFrame:", frame);
    return true;
}

ObjcId rg_layer(ObjcId view) {
    if (!responds(view, "setWantsLayer:") ||
        !finn::darwin::finn_objc_getClass("CALayer")) {
        return nullptr;
    }
    objc_send_name<void>(view, "setWantsLayer:", static_cast<signed char>(1));
    ObjcId layer = objc_send_name<ObjcId>(view, "layer");
    return responds(layer, "setCornerRadius:") ? layer : nullptr;
}

bool rg_apply_background(ObjcId view, double red, double green,
                         double blue, double alpha) {
    ObjcId layer = rg_layer(view);
    ObjcClass* color_class = finn::darwin::finn_objc_getClass("NSColor");
    if (!layer || !color_class) return false;
    ObjcId color = objc_send_name<ObjcId>(
        reinterpret_cast<ObjcId>(color_class),
        "colorWithRed:green:blue:alpha:", red, green, blue, alpha);
    if (!color) return false;
    objc_send_name<void>(layer, "setBackgroundColor:", color);
    objc_send_name<void>(layer, "setMasksToBounds:", static_cast<signed char>(1));
    finn::darwin::finn_objc_release(color);
    return true;
}

bool rg_apply_corner_radius(ObjcId view, double radius) {
    ObjcId layer = rg_layer(view);
    if (!layer) return false;
    objc_send_name<void>(layer, "setCornerRadius:", radius);
    objc_send_name<void>(layer, "setMasksToBounds:", static_cast<signed char>(1));
    return true;
}

void* rg_modifier_frame(void* token, double width, double height) {
    return rg_apply_frame(rg_view(token), width, height) ? token : nullptr;
}
void* rg_modifier_padding(void* token, double top, double leading,
                          double bottom, double trailing) {
    return rg_apply_padding(rg_view(token), top, leading, bottom, trailing) ? token : nullptr;
}
void* rg_modifier_background(void* token, double red, double green,
                             double blue, double alpha) {
    return rg_apply_background(rg_view(token), red, green, blue, alpha) ? token : nullptr;
}
void* rg_modifier_corner_radius(void* token, double radius) {
    return rg_apply_corner_radius(rg_view(token), radius) ? token : nullptr;
}

void* rg_empty_view() {
    ObjcId view = new_view(0.0, 0.0, 0.0, 0.0);
    if (!view) return nullptr;
    void* token = rg_token_create();
    return rg_bind(token, view) ? token : nullptr;
}

void* rg_zstack(void* const* children, std::size_t count) {
    ObjcId stack = new_view(0.0, 0.0, 0.0, 0.0);
    if (!stack || !responds(stack, "addSubview:")) return nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId child = rg_view(children[i]);
        if (child) objc_send_name<void>(stack, "addSubview:", child);
    }
    void* token = rg_token_create();
    return rg_bind(token, stack) ? token : nullptr;
}

// ---------------------------------------------------------------------------
// Initializers with fully derivable ABI and semantics.
//
// A Swift value type with no stored properties occupies one byte and is zero
// initialised: AAPCS64 passes its indirect result buffer in x8 and returns the
// buffer address in x0, which a C function returning a small struct follows.
// EdgeInsets has four CGFloat (Double) fields, an HFA returned in v0..v3 from a
// single Double argument in v0.
// ---------------------------------------------------------------------------
struct RgEdgeInsetsValue {
    double top;
    double leading;
    double bottom;
    double trailing;
};

// Swift 6.4 emits a bare `ret` for an empty value type's init (verified with a
// compiled snippet), so the honest implementation is a no-op with no result.
extern "C" void rg_init_empty_marker() {}

extern "C" RgEdgeInsetsValue rg_init_edge_insets(double all) {
    return RgEdgeInsetsValue{all, all, all, all};
}

// ---------------------------------------------------------------------------
// Real render-graph requirement thunks.
//
// Swift appends hidden generic-metadata arguments to witness calls; our C
// callees read only the arguments they need and ignore the rest.  Every value
// passed through these thunks is an opaque bridge token, never an invented
// Swift view struct.
// ---------------------------------------------------------------------------

// ViewModifier.body(content:) for the layout modifiers we implement.  `self`
// is a token whose bound NSView carries the modifier record; the modifier is
// applied to `content` through the real bridge and the content token is
// returned.  Modifiers with no render effect leave content untouched.
struct ModifierRecord {
    enum class Op { None, Frame, Padding, Background, CornerRadius } op = Op::None;
    double p[4] = {};
};

std::mutex& modifiers_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<void*, ModifierRecord>& modifiers() {
    static std::unordered_map<void*, ModifierRecord> m;
    return m;
}

void set_modifier(void* token, ModifierRecord record) {
    std::lock_guard<std::mutex> lk(modifiers_mutex());
    modifiers()[token] = record;
}

ModifierRecord get_modifier(void* token) {
    std::lock_guard<std::mutex> lk(modifiers_mutex());
    auto it = modifiers().find(token);
    return it == modifiers().end() ? ModifierRecord{} : it->second;
}

extern "C" void* rg_witness_viewmodifier_body(void* self, void* content) {
    if (!content) return nullptr;
    const ModifierRecord rec = get_modifier(self);
    switch (rec.op) {
        case ModifierRecord::Op::Frame:
            return rg_modifier_frame(content, rec.p[0], rec.p[1]);
        case ModifierRecord::Op::Padding:
            return rg_modifier_padding(content, rec.p[0], rec.p[1],
                                            rec.p[2], rec.p[3]);
        case ModifierRecord::Op::Background:
            return rg_modifier_background(content, rec.p[0], rec.p[1],
                                               rec.p[2], rec.p[3]);
        case ModifierRecord::Op::CornerRadius:
            return rg_modifier_corner_radius(content, rec.p[0]);
        case ModifierRecord::Op::None:
            // A modifier with no render effect is an identity on the token.
            return rg_view(content) ? content : nullptr;
    }
    return nullptr;
}

// View.body for primitive views: the token already is the materialised view.
extern "C" void* rg_witness_view_body(void* self) {
    return rg_view(self) ? self : nullptr;
}

// EmptyView.body materialises a real empty NSView through the bridge.
extern "C" void* rg_witness_emptyview_body(void*) {
    return rg_empty_view();
}

// NSViewRepresentable requirements, backed by real AppKit bridge operations.
extern "C" void* rg_witness_nsview_make(void*) {
    return rg_empty_view();
}

extern "C" void rg_witness_nsview_update(void* self, void* nsview) {
    const ModifierRecord rec = get_modifier(self);
    ObjcId view = static_cast<ObjcId>(nsview);
    switch (rec.op) {
        case ModifierRecord::Op::Frame:
            (void)rg_apply_frame(view, rec.p[0], rec.p[1]);
            break;
        case ModifierRecord::Op::Padding:
            (void)rg_apply_padding(view, rec.p[0], rec.p[1], rec.p[2], rec.p[3]);
            break;
        case ModifierRecord::Op::Background:
            (void)rg_apply_background(view, rec.p[0], rec.p[1], rec.p[2], rec.p[3]);
            break;
        case ModifierRecord::Op::CornerRadius:
            (void)rg_apply_corner_radius(view, rec.p[0]);
            break;
        case ModifierRecord::Op::None:
            break;
    }
}

extern "C" RgRect rg_witness_nsview_size_that_fits(void*, void* nsview) {
    return rg_view_frame(static_cast<ObjcId>(nsview));
}

extern "C" void* rg_witness_nsview_coordinator(void*) {
    // A coordinator has no backing view: return a real token with no view,
    // never a fabricated object that pretends to be a bridged view.
    return rg_token_create();
}

extern "C" void rg_witness_nsview_dismantle(void*, void* nsview) {
    ObjcId view = static_cast<ObjcId>(nsview);
    if (view && finn::darwin::finn_objc_respondsToSelector(
                    view, finn::darwin::finn_sel_registerName("removeFromSuperview"))) {
        objc_send_name<void>(view, "removeFromSuperview");
    }
}

// ---------------------------------------------------------------------------
// Witness table construction.  A slot is a real thunk where we have a real
// implementation, and null otherwise.  Null is reported, never filled with a
// plausible-looking no-op that would claim a capability we lack.
// ---------------------------------------------------------------------------
SwiftWitnessTable* make_witness_table(const char* symbol) {
    auto* wt = static_cast<SwiftWitnessTable*>(std::calloc(1, sizeof(SwiftWitnessTable)));
    if (!wt) return nullptr;

    // ViewModifier conformances appear with two mangled spellings: the direct
    // "ViewModifierAAWP" and the abbreviated "04ViewD0AAWP"/"04ViewE0AAWP".
    const bool is_viewmodifier = std::strstr(symbol, "ViewModifierAAWP") != nullptr ||
                                 std::strstr(symbol, "04ViewD0AAWP") != nullptr ||
                                 std::strstr(symbol, "04ViewE0AAWP") != nullptr;
    const bool is_view = std::strstr(symbol, "AA4ViewAAWP") != nullptr ||
                         std::strstr(symbol, "VAA0D0AAWP") != nullptr;
    const bool is_empty = std::strstr(symbol, "9EmptyViewV") != nullptr;
    const bool is_nsview_rep = std::strstr(symbol, "NSViewRepresentable") != nullptr;

    if (is_viewmodifier) {
        // ViewModifier's sole requirement is body(content:).
        wt->witnesses[0] = to_ptr(&rg_witness_viewmodifier_body);
    } else if (is_view) {
        wt->witnesses[0] = to_ptr(is_empty ? &rg_witness_emptyview_body
                                           : &rg_witness_view_body);
    } else if (is_nsview_rep) {
        // makeNSView, updateNSView, sizeThatFits, makeCoordinator,
        // dismantleNSView — all backed by real NSView operations.
        wt->witnesses[0] = to_ptr(&rg_witness_nsview_make);
        wt->witnesses[1] = to_ptr(&rg_witness_nsview_update);
        wt->witnesses[2] = to_ptr(&rg_witness_nsview_size_that_fits);
        wt->witnesses[3] = to_ptr(&rg_witness_nsview_coordinator);
        wt->witnesses[4] = to_ptr(&rg_witness_nsview_dismantle);
    }
    // Combine Cancellable/Publisher requirements are left null: cancelling a
    // subscription and delivering combines are not implemented here, and a
    // plausible-looking empty function would claim a capability we lack.
    // Every other witness slot stays null: no fake capability.
    return wt;
}

// ---------------------------------------------------------------------------
// Real view composition on the AppKit-on-Qt6 bridge.
// ---------------------------------------------------------------------------
void* bind_new_view(double x, double y, double w, double h) {
    ObjcId view = new_view(x, y, w, h);
    if (!view) return nullptr;
    void* token = rg_token_create();
    if (!token) return nullptr;
    return rg_bind(token, view) ? token : nullptr;
}

// Assign sequential frames to children inside a container.  `horizontal`
// chooses the axis; z stacking overlaps all children at the origin.
void* make_stack(void* const* children, std::size_t count, bool horizontal,
                 double spacing) {
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId child = rg_view(children[i]);
        if (!child) continue;
        RgRect f = rg_view_frame(child);
        total += horizontal ? f.width : f.height;
    }
    total += spacing * (count > 1 ? static_cast<double>(count - 1) : 0.0);

    ObjcId stack = new_view(0.0, 0.0, horizontal ? total : 240.0,
                            horizontal ? 48.0 : total);
    if (!stack || !responds(stack, "addSubview:")) return nullptr;

    double cursor = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        ObjcId child = rg_view(children[i]);
        if (!child) continue;
        RgRect f = rg_view_frame(child);
        if (horizontal) {
            f.x = cursor;
            f.y = 0.0;
            cursor += f.width + spacing;
        } else {
            f.x = 0.0;
            f.y = total - cursor - f.height;
            cursor += f.height + spacing;
        }
        if (responds(child, "setFrame:")) {
            objc_send_name<void>(child, "setFrame:", f);
        }
        objc_send_name<void>(stack, "addSubview:", child);
    }

    void* token = rg_token_create();
    if (!token) return nullptr;
    return rg_bind(token, stack) ? token : nullptr;
}

// ---------------------------------------------------------------------------
// Registration state, exposed for the report and the self-test.
// ---------------------------------------------------------------------------
struct Registration {
    std::mutex mu;
    std::unordered_map<std::string, void*> objects;
    std::size_t null_witnesses = 0;
    std::size_t real_witnesses = 0;
};

Registration& registration() {
    static Registration r;
    return r;
}

// ---------------------------------------------------------------------------
// Metadata accessors (Ma).  A distinct real function per slot returns that
// slot's metadata object; there is no shared mutable "current type".
// ---------------------------------------------------------------------------
std::vector<const void*>& accessor_metadata() {
    static std::vector<const void*> v;
    return v;
}

template <std::size_t I>
const void* metadata_accessor() {
    const auto& v = accessor_metadata();
    return I < v.size() ? v[I] : nullptr;
}

using AccessorFn = const void* (*)();
std::vector<AccessorFn>& accessor_fns() {
    static std::vector<AccessorFn> v;
    return v;
}

template <std::size_t I>
void add_accessor_fn() {
    accessor_fns().push_back(&metadata_accessor<I>);
}

template <std::size_t... I>
void build_accessor_fns(std::index_sequence<I...>) {
    (add_accessor_fn<I>(), ...);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public compositing primitives.
// ---------------------------------------------------------------------------
void* swiftui_rg_hstack(void* const* children, std::size_t count, double spacing) {
    return make_stack(children, count, true, spacing);
}

void* swiftui_rg_vstack(void* const* children, std::size_t count, double spacing) {
    return make_stack(children, count, false, spacing);
}

void* swiftui_rg_zstack(void* const* children, std::size_t count) {
    if (count == 0) return bind_new_view(0.0, 0.0, 0.0, 0.0);
    return rg_zstack(children, count);
}

void* swiftui_rg_frame(void* content, double width, double height) {
    if (!rg_view(content)) return nullptr;
    set_modifier(content, {ModifierRecord::Op::Frame, {width, height, 0.0, 0.0}});
    return rg_modifier_frame(content, width, height);
}

void* swiftui_rg_padding(void* content, double top, double leading,
                         double bottom, double trailing) {
    if (!rg_view(content)) return nullptr;
    set_modifier(content, {ModifierRecord::Op::Padding,
                           {top, leading, bottom, trailing}});
    return rg_modifier_padding(content, top, leading, bottom, trailing);
}

void* swiftui_rg_background(void* content, double red, double green,
                            double blue, double alpha) {
    if (!rg_view(content)) return nullptr;
    set_modifier(content, {ModifierRecord::Op::Background,
                           {red, green, blue, alpha}});
    return rg_modifier_background(content, red, green, blue, alpha);
}

void* swiftui_rg_corner_radius(void* content, double radius) {
    if (!rg_view(content)) return nullptr;
    set_modifier(content, {ModifierRecord::Op::CornerRadius,
                           {radius, 0.0, 0.0, 0.0}});
    return rg_modifier_corner_radius(content, radius);
}

void* swiftui_rg_foreach(void* const* children, std::size_t count) {
    return make_stack(children, count, false, 0.0);
}

std::size_t swiftui_rg_registered_symbol_count() {
    Registration& r = registration();
    std::lock_guard<std::mutex> lk(r.mu);
    return r.objects.size();
}


// Build a conformance record once its witness table and nominal descriptor
// siblings are available.  Every link is a real Swift relative pointer.
void* make_conformance(const char* symbol,
                       const std::unordered_map<std::string, void*>& objects) {
    const char* row_symbol = symbol;
    auto& reg_objects = objects;

                // Real conformance record pointing at a real conformance
                // descriptor and, where available, a real witness table.
                auto* desc = static_cast<SwiftConformanceDescriptor*>(
                    std::calloc(1, sizeof(SwiftConformanceDescriptor)));
                auto* record = static_cast<SwiftConformanceRecord*>(
                    std::calloc(1, sizeof(SwiftConformanceRecord)));
                if (desc && record) {
                    // An `Mc` symbol's `WP` sibling has the same prefix with the
                    // trailing "Mc" replaced by "WP".  When that table exists
                    // it really belongs to this conformance, so link the two
                    // with Swift relative pointers.  Otherwise leave the slot
                    // null: no witness table is claimed that we cannot back.
                    std::string wt_symbol(row_symbol);
                    if (wt_symbol.size() >= 2) {
                        wt_symbol.replace(wt_symbol.size() - 2, 2, "WP");
                    }
                    void* wt = nullptr;
                    auto it = reg_objects.find(wt_symbol);
                    if (it != reg_objects.end()) wt = it->second;

                    // The conforming type's `Mn` symbol is the longest nominal
                    // prefix of the `Mc` name.  Link it as the real typeRef
                    // when present; otherwise the field stays null.
                    void* type_meta = nullptr;
                    const std::string mc(row_symbol);
                    for (const auto& candidate : kSymbols) {
                        if (candidate.kind != SymbolKind::NOMINAL_STRUCT &&
                            candidate.kind != SymbolKind::NOMINAL_ENUM &&
                            candidate.kind != SymbolKind::NOMINAL_CLASS) {
                            continue;
                        }
                        std::string base(candidate.symbol);
                        if (base.size() < 3) continue;
                        base.resize(base.size() - 2);  // drop trailing "Mn"
                        if (mc.rfind(base, 1) == 1) {
                            auto tit = reg_objects.find(candidate.symbol);
                            if (tit != reg_objects.end()) {
                                type_meta = tit->second;
                                break;
                            }
                        }
                    }
                    desc->typeRef = type_meta
                        ? static_cast<RelativePointer>(
                              reinterpret_cast<char*>(type_meta) -
                              reinterpret_cast<char*>(&desc->typeRef))
                        : 0;
                    desc->witnessTable = wt
                        ? static_cast<RelativePointer>(
                              reinterpret_cast<char*>(wt) -
                              reinterpret_cast<char*>(&desc->witnessTable))
                        : 0;
                    record->conformance = static_cast<RelativePointer>(
                        reinterpret_cast<char*>(desc) -
                        reinterpret_cast<char*>(&record->conformance));
                    record->witnessTable = wt
                        ? static_cast<RelativePointer>(
                              reinterpret_cast<char*>(wt) -
                              reinterpret_cast<char*>(&record->witnessTable))
                        : 0;
                    return record;
                }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Registration.
// ---------------------------------------------------------------------------
void register_swiftui_render_graph(SymbolTable* table) {
    if (!table) return;

    // Registration is idempotent: clear counters and objects from any earlier
    // call (the harness registers once and the self-test registers again).
    {
        std::lock_guard<std::mutex> lk(registration().mu);
        registration().objects.clear();
        registration().null_witnesses = 0;
        registration().real_witnesses = 0;
    }

    // Build metadata objects first: accessors need them.
    std::vector<const char*> accessor_symbols;
    for (const auto& row : kSymbols) {
        if (row.kind == SymbolKind::METADATA_ACCESSOR) accessor_symbols.push_back(row.symbol);
    }
    accessor_metadata().clear();
    for (const char* s : accessor_symbols) accessor_metadata().push_back(make_metadata(s));

    // Generate one distinct accessor function per slot (up to 64).
    accessor_fns().clear();
    build_accessor_fns(std::make_index_sequence<64>{});
    std::size_t accessor_index = 0;

    Registration& reg = registration();
    for (const auto& row : kSymbols) {
        // Conformances are registered in a second pass: they link to witness
        // tables and nominal descriptors that must already be in reg.objects.
        if (row.kind == SymbolKind::CONFORMANCE) continue;
        void* object = nullptr;
        switch (row.kind) {
            case SymbolKind::NOMINAL_STRUCT:
                object = const_cast<void*>(make_nominal(row.symbol, 1));
                if (object) {
                    const auto* d = static_cast<const SwiftNominalTypeDescriptor*>(object);
                    register_nominal(row.symbol + 1, d);
                }
                break;
            case SymbolKind::NOMINAL_ENUM:
                object = const_cast<void*>(make_nominal(row.symbol, 2));
                if (object) {
                    const auto* d = static_cast<const SwiftNominalTypeDescriptor*>(object);
                    register_nominal(row.symbol + 1, d);
                }
                break;
            case SymbolKind::NOMINAL_CLASS:
                object = const_cast<void*>(make_nominal(row.symbol, 0));
                if (object) {
                    const auto* d = static_cast<const SwiftNominalTypeDescriptor*>(object);
                    register_nominal(row.symbol + 1, d);
                }
                break;
            case SymbolKind::PROTOCOL:
                object = const_cast<void*>(make_protocol(row.symbol));
                break;
            case SymbolKind::METHOD:
                object = const_cast<void*>(make_method(row.symbol));
                break;
            case SymbolKind::METADATA:
                object = const_cast<void*>(make_metadata(row.symbol));
                break;
            case SymbolKind::METADATA_ACCESSOR:
                if (accessor_index < accessor_fns().size()) {
                    object = to_ptr(accessor_fns()[accessor_index]);
                }
                ++accessor_index;
                break;
            case SymbolKind::INIT_EMPTY_MARKER:
                object = to_ptr(&rg_init_empty_marker);
                break;
            case SymbolKind::INIT_EDGE_INSETS:
                object = to_ptr(&rg_init_edge_insets);
                break;
            case SymbolKind::ASSOCTYPE: {
                auto* d = static_cast<NominalBlock*>(
                    const_cast<void*>(make_nominal(row.symbol, 2)));
                object = d;
                break;
            }
            case SymbolKind::WITNESS: {
                SwiftWitnessTable* wt = make_witness_table(row.symbol);
                object = wt;
                if (wt) {
                    std::size_t real = 0;
                    for (const void* w : wt->witnesses) if (w) ++real;
                    if (real == 0) ++reg.null_witnesses;
                    reg.real_witnesses += real;
                }
                break;
            }
            case SymbolKind::CONFORMANCE:
                break;  // built in the second pass below
        }
        if (!object) continue;
        (*table)[row.symbol] = object;
        {
            std::lock_guard<std::mutex> lk(reg.mu);
            reg.objects[row.symbol] = object;
        }
    }

    // Second pass: conformances link to objects built in the first pass.
    for (const auto& row : kSymbols) {
        if (row.kind != SymbolKind::CONFORMANCE) continue;
        void* object = make_conformance(row.symbol, reg.objects);
        if (!object) continue;
        (*table)[row.symbol] = object;
        std::lock_guard<std::mutex> lk(reg.mu);
        reg.objects[row.symbol] = object;
    }
}

// ---------------------------------------------------------------------------
// Self test.
// ---------------------------------------------------------------------------
bool swiftui_render_graph_self_test() {
    SymbolTable table;
    register_swiftui_render_graph(&table);

    std::size_t failed = 0;
    std::size_t checked = 0;
    Registration& reg = registration();
    {
        std::lock_guard<std::mutex> lk(reg.mu);
        std::fprintf(stderr,
                     "render_graph self-test: registered=%zu witness_slots_real=%zu "
                     "tables_without_implementation=%zu\n",
                     reg.objects.size(), reg.real_witnesses, reg.null_witnesses);
    }

    if (swiftui_rg_registered_symbol_count() != table.size()) {
        std::fprintf(stderr, "render_graph self-test: FAIL registration count differs\n");
        ++failed;
    }

    // Every descriptor must be non-null and every nominal name must match its
    // symbol (the name field is a relative pointer into the same allocation).
    for (const auto& row : kSymbols) {
        auto it = table.find(row.symbol);
        if (it == table.end() || !it->second) {
            std::fprintf(stderr, "render_graph self-test: FAIL missing %s\n", row.symbol);
            ++failed;
            continue;
        }
        ++checked;
        if (row.kind == SymbolKind::NOMINAL_STRUCT ||
            row.kind == SymbolKind::NOMINAL_ENUM ||
            row.kind == SymbolKind::NOMINAL_CLASS) {
            const auto* d = static_cast<const SwiftNominalTypeDescriptor*>(it->second);
            const char* name = reinterpret_cast<const char*>(&d->name) + d->name;
            if (std::strcmp(name, row.symbol + 1) != 0) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL nominal name for %s\n",
                             row.symbol);
                ++failed;
            }
            const unsigned kind = d->flags & 0x3u;
            const unsigned expected =
                row.kind == SymbolKind::NOMINAL_STRUCT ? 1u
                : row.kind == SymbolKind::NOMINAL_ENUM ? 2u : 0u;
            if (kind != expected || (d->flags & 0xf0u) != 0x50u) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL nominal flags for %s\n",
                             row.symbol);
                ++failed;
            }
        } else if (row.kind == SymbolKind::METHOD) {
            const auto* m = static_cast<const std::uint32_t*>(it->second);
            const auto rel = static_cast<std::int32_t>(m[1]);
            const char* name = reinterpret_cast<const char*>(&m[1]) + rel;
            if (std::strcmp(name, row.symbol + 1) != 0) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL method name for %s\n",
                             row.symbol);
                ++failed;
            }
        } else if (row.kind == SymbolKind::METADATA_ACCESSOR) {
            auto fn = reinterpret_cast<AccessorFn>(it->second);
            if (!fn || !fn()) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL accessor for %s\n",
                             row.symbol);
                ++failed;
            }
        }
    }

    // The real witness thunks must be present on the tables that claim them.
    for (const auto& row : kSymbols) {
        if (row.kind != SymbolKind::WITNESS) continue;
        auto it = table.find(row.symbol);
        if (it == table.end() || !it->second) continue;
        const auto* wt = static_cast<const SwiftWitnessTable*>(it->second);
        const bool claims_modifier = std::strstr(row.symbol, "ViewModifierAAWP") != nullptr;
        const bool claims_view = std::strstr(row.symbol, "AA4ViewAAWP") != nullptr ||
                                 std::strstr(row.symbol, "VAA0D0AAWP") != nullptr;
        if ((claims_modifier || claims_view) && !wt->witnesses[0]) {
            std::fprintf(stderr,
                         "render_graph self-test: FAIL witness[0] null for %s\n",
                         row.symbol);
            ++failed;
        }
    }

    // Conformance records must resolve their relative witness pointer to a
    // real object that is actually in the symbol table (and their typeRef to
    // a real nominal descriptor).  A dangling pointer would be a fake link.
    for (const auto& row : kSymbols) {
        if (row.kind != SymbolKind::CONFORMANCE) continue;
        auto it = table.find(row.symbol);
        if (it == table.end() || !it->second) continue;
        const auto* record = static_cast<const SwiftConformanceRecord*>(it->second);
        if (record->witnessTable != 0) {
            const void* resolved = reinterpret_cast<const char*>(&record->witnessTable) +
                                   record->witnessTable;
            bool found = false;
            for (const auto& entry : table) {
                if (entry.second == resolved) { found = true; break; }
            }
            if (!found) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL conformance witness link %s\n",
                             row.symbol);
                ++failed;
            }
        }
    }

    // The initialisers must return real values, not plausible placeholders.
    rg_init_empty_marker();  // a real no-op, matching Swift's empty init ABI
    const RgEdgeInsetsValue insets = rg_init_edge_insets(7.0);
    if (insets.top != 7.0 || insets.leading != 7.0 ||
        insets.bottom != 7.0 || insets.trailing != 7.0) {
        std::fprintf(stderr, "render_graph self-test: FAIL EdgeInsets init\n");
        ++failed;
    }

    // Render primitives: materialise real view tokens and apply the modifiers.
    // AppKit classes may be absent in a bare harness; in that case the render
    // checks are *skipped* and reported as such, never counted as a pass.
    ObjcClass* nsview_class = finn::darwin::finn_objc_getClass("NSView");
    if (!nsview_class) {
        std::fprintf(stderr,
                     "render_graph self-test: SKIP render checks (NSView class absent)\n");
    } else {
        void* a = bind_new_view(0.0, 0.0, 100.0, 20.0);
        void* b = bind_new_view(0.0, 0.0, 100.0, 20.0);
        if (!a || !b) {
            std::fprintf(stderr, "render_graph self-test: FAIL bind_new_view\n");
            ++failed;
        } else {
            void* stack = swiftui_rg_hstack(&a, 2, 8.0);
            if (!stack || !rg_view(stack)) {
                std::fprintf(stderr, "render_graph self-test: FAIL hstack\n");
                ++failed;
            }
            void* list = swiftui_rg_foreach(&a, 2);
            if (!list || !rg_view(list)) {
                std::fprintf(stderr, "render_graph self-test: FAIL foreach\n");
                ++failed;
            }
            RgRect before = rg_view_frame(rg_view(a));
            if (!swiftui_rg_frame(a, 160.0, 30.0)) {
                std::fprintf(stderr, "render_graph self-test: FAIL frame\n");
                ++failed;
            }
            RgRect after = rg_view_frame(rg_view(a));
            if (std::abs(after.width - 160.0) > 0.5) {
                std::fprintf(stderr,
                             "render_graph self-test: FAIL frame changed width "
                             "(before=%.1f after=%.1f)\n",
                             before.width, after.width);
                ++failed;
            }
            if (!swiftui_rg_padding(a, 4.0, 4.0, 4.0, 4.0) ||
                !swiftui_rg_background(a, 0.1, 0.2, 0.3, 1.0) ||
                !swiftui_rg_corner_radius(a, 6.0)) {
                std::fprintf(stderr, "render_graph self-test: FAIL modifier chain\n");
                ++failed;
            }
            rg_token_destroy(a);
            rg_token_destroy(b);
            rg_token_destroy(stack);
            rg_token_destroy(list);
        }
    }

    std::fprintf(stderr, "render_graph self-test: checked=%zu failed=%zu %s\n",
                 checked, failed, failed == 0 ? "PASS" : "FAIL");
    return failed == 0;
}

}  // namespace finn::darwin::swiftui
