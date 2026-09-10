# Flight Deck Phase 1 information architecture intent

## Shell intent

Flight Deck uses a rounded navigation rail, a rounded workspace, an optional
compact status strip, and a persistent readiness card. The rail routes into
the existing page IDs; it is not a second navigation, configuration, or
device-state model. Phase 1 embeds the current page bodies deliberately, so
the new shell remains development-gated until later page redesign phases make
the end-to-end experience coherent.

## First-class areas

| Area | Current responsibility | Flight Deck primary user question | Keep primary | Later progressive disclosure | Must never disappear |
| --- | --- | --- | --- | --- | --- |
| Overview | Signal path, readiness, live mapper instrumentation | “Can I use my HOTAS correctly right now, and what is active?” | Mapping state, selected controller, vJoy readiness, profile, actionable health | Rates, latency, detailed telemetry | Start/stop state, warnings, physical/output distinction |
| Devices & setup | Controller selection, verification, repair, and calibration entry | “What hardware is connected and correctly configured?” | Device identity, connected/verified state, recommended action | HID identity, repair plan/result details, capability detail | Ambiguity, offline/selected state, vJoy/HidHide status, safe repair confirmation |
| Axes | Per-axis routing, transform, live input/output, and curve entry | “What does each physical control currently do?” | Physical label, target, enabled/routed state, live value | Domain, limits, deadzone, hysteresis, inversion, conflict mechanics | Fixed/unavailable state, output validity, profile scope, Curve and Adaptive Response entries |
| Buttons | Button and POV routing with live state and learn flows | “What does each physical button or hat control do?” | Physical control, target, pressed/output state | Conflict choices, native/virtual POV implementation, bulk mapping | Disabled state, profile/mapping control consumption, learn/quick-map |
| Profiles | Manual/automatic profile choice, categories, portability | “What configuration becomes active while I play?” | Effective profile, source, active category, activation state | Category management, portability preview, compatibility detail | Hold/toggle precedence, game association, imports/exports, controller/layout compatibility |
| Curve editor | Advanced response-curve authoring and inspection | “How does this control respond through its range?” | Selected axis, configured curve, live marker | Points, presets, comparisons, audition, interpolation, undo/redo | Compiled curve relationship and profile scope |
| Adaptive Response | Bounded predictive response configuration and evidence | “How much predictive response is active, and how is this axis behaving?” | Enabled/scope/preset, active axis, effective response and safety state | Preset authoring, source/test lab, advanced tuning and telemetry | Safe-off behavior, context precedence, custom-preset dependencies, diagnostics |
| Automation | Rule list, global health, and full rule editor | “What will HOTAS BF6 change automatically?” | Master state, valid/active rule summary and health | Requirements/effects builder, priority, timing, drafts | Invalid-rule protection, momentary/toggle/timed behavior, all effect types |
| Diagnostics | Detailed raw and derived state inspection | “What is broken, unhealthy, or unusual?” | Attention/fault summary and Copy Diagnostics path | Raw axes/buttons/POVs, event log, timing, device capabilities | Hardware/setup/validation details that support repair and support |
| Settings | App, mapping, device-hiding, maintenance, update preferences | “How should the application itself behave?” | Safe frequently-used application controls | Device hiding, output layout, maintenance, update and destructive actions | Existing persistence semantics, confirmations, tray behavior, update handoff |

## Navigation and status rules

- Phase 1 routes Overview, Devices & setup, Axes, Buttons, Profiles, Adaptive
  Response, Automation, Diagnostics, Settings, and Curve editor to the real
  existing page IDs. No route is fabricated or removed.
- The readiness card shows only existing backend values: mapper state,
  connected-controller count, vJoy status, and effective profile. It does not
  invent game, device, or telemetry data.
- The full controller-setup, learning, conflict, import, and confirmation
  dialogs remain reachable through their current page flows until their own
  Flight Deck redesigns are deliberately specified.
