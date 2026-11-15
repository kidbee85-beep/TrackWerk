// PERFORM: its buttons (ui/perform.h). R + D-pad, START and SELECT take the generic paths.

#include "ui/dispatch/dispatch_common.h"
#include "ui/perform.h"

namespace pt::ui {

GestureResult InputDispatcher::perform_screen(Gesture g) {
    auto& cursor = s_.perform.cursor;
    const auto act = [&](PerformAction action) {
        const PerformResult r = perform_action(s_, host_, action);
        if (r.edited) mark_modified();
        if (!r.status.empty()) set_status(s_, r.status, true);
        return GestureResult::TAKEN;
    };
    switch (g) {
        case Gesture::DPAD_UP:
        case Gesture::DPAD_DOWN:   cursor = (cursor + 4) % songcore::PERFORM_SLOTS; return GestureResult::TAKEN;
        case Gesture::DPAD_LEFT:   cursor = (cursor / 4) * 4 + (cursor % 4 + 3) % 4; return GestureResult::TAKEN;
        case Gesture::DPAD_RIGHT:  cursor = (cursor / 4) * 4 + (cursor % 4 + 1) % 4; return GestureResult::TAKEN;
        case Gesture::A:           return act(PerformAction::TAP);
        case Gesture::A_UP:        return act(PerformAction::VALUE_UP);
        case Gesture::A_DOWN:      return act(PerformAction::VALUE_DOWN);
        case Gesture::A_LEFT:      return act(PerformAction::VALUE_LEFT);
        case Gesture::A_RIGHT:     return act(PerformAction::VALUE_RIGHT);
        case Gesture::B_LEFT:      return act(PerformAction::KIND_PREVIOUS);
        case Gesture::B_RIGHT:     return act(PerformAction::KIND_NEXT);
        case Gesture::B_UP:        return act(PerformAction::INSTRUMENT_NEXT);
        case Gesture::B_DOWN:      return act(PerformAction::INSTRUMENT_PREVIOUS);
        case Gesture::L_LEFT:      return act(PerformAction::SUB_LEFT);
        case Gesture::L_RIGHT:     return act(PerformAction::SUB_RIGHT);
        case Gesture::L_A:         return act(PerformAction::TRACK_TOGGLE);
        default:                   return GestureResult::PASS;
    }
}

}  // namespace pt::ui
