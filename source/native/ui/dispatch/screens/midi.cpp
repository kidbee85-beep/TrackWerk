// The MIDI screen and its mapping list: their buttons.

#include "ui/dispatch/dispatch_common.h"

#include "ui/navigation.h"

#include <algorithm>

namespace pt::ui {

// ─── The buttons ─────────────────────────────────────────────────────────────────────────────────

// B is the only way out of either: neither is on the nav grid.
GestureResult InputDispatcher::midi_screen(Gesture g) {
    switch (g) {
        case Gesture::A: midi_action();                  return GestureResult::TAKEN;
        case Gesture::B: leave_to(s_.midiReturnScreen);  return GestureResult::TAKEN;
        default:         return GestureResult::PASS;
    }
}

GestureResult InputDispatcher::midi_map_screen(Gesture g) {
    switch (g) {
        case Gesture::A: midi_map_action();                 return GestureResult::TAKEN;
        case Gesture::B: leave_to(s_.midiMapReturnScreen);  return GestureResult::TAKEN;

        // A mapping's GROUP and PARAMETER cells open the destination picker rather than stepping.
        case Gesture::A_UP:
        case Gesture::A_DOWN:
            if (!on_map_dest_cell()) return GestureResult::PASS;
            open_map_picker();
            return GestureResult::TAKEN;

        default: return GestureResult::PASS;
    }
}

// ─── What A does ─────────────────────────────────────────────────────────────────────────────────

void InputDispatcher::midi_action() {
    switch (static_cast<MidiRow>(s_.midiCursorRow)) {
        case MidiRow::PANIC: {
            // ⚠️ Not only the notes this app owes: Sustain off and All Notes Off on all sixteen
            // channels, through the sender's lock (ExternalConsumer::panic_all_channels). SENT only when
            // messages reached a port that did not break.
            if (!port_open()) { host_.midi_out().panic(); s_.midiStatusText = midi_no_port_reason(); break; }
            midiTestOffAtMs_ = 0;   // the panic ended the test note too
            const int sent = host_.midi_out().panic_all_channels();
            s_.midiStatusText = sent > 0 ? "PANIC SENT" : "SEND FAILED: REPLUG IT";
            break;
        }

        case MidiRow::TEST: {
            // Through the sender's lock and tracked (a PANIC or a port change ends it), held 250 ms so a
            // slow attack is heard; the note-off comes from run_due_midi_test_off, never by waiting.
            if (!port_open()) { s_.midiStatusText = midi_no_port_reason(); break; }
            cancel_midi_test();   // a test still sounding ends first
            const int ch = std::clamp(s_.settings.midiTestChannel, 0, 15);
            // C-4, mf. The sender owns the owed off (ExternalConsumer::test_note_on): any STOP, play
            // start, PANIC, port swap or song note on that key takes it back, so the timer can never
            // end a note the song plays.
            if (!host_.midi_out().test_note_on(static_cast<uint8_t>(ch), 60, 100)) {
                s_.midiStatusText = "SEND FAILED: REPLUG IT";
                break;
            }
            midiTestOffAtMs_ = now_ms_ + 250;
            midiTestPort_ = s_.midiOutOpenName;
            const std::string chText = (ch + 1 < 10 ? "0" : "") + std::to_string(ch + 1);
            s_.midiStatusText = "TEST C-4 CH " + chText + " SENT";
            break;
        }

        // The door into the mapping list: only the cursor needs putting back inside the list.
        case MidiRow::MAPPING: {
            clamp_midi_map_cursor();
            s_.midiMapReturnScreen = s_.currentScreen;
            go_to_screen(s_, {ScreenType::MIDI_MAP, s_.previousColumn});
            break;
        }

        // OUTPUT / OFFSET / PROG CHG are A+DPAD cells — the app-wide rule that single A is for actions.
        default:
            break;
    }
}

// Why nothing could be sent, in the words of the fix: no device at all, OUTPUT switched off, or a
// saved device that is not plugged in.
std::string InputDispatcher::midi_no_port_reason() const {
    const bool anyDevice = static_cast<int>(s_.midiDeviceNames.size()) > MIDI_FIRST_PORT;
    if (!anyDevice) return "NO MIDI DEVICE FOUND";
    if (s_.settings.midiOutDevice.empty() || s_.settings.midiOutDevice == MIDI_PORT_OFF)
        return "OUTPUT IS OFF: PICK ONE";
    return "OUTPUT NOT CONNECTED";
}

// The TEST note's end, 250 ms after it began (or at once with `force`). The off is sent only while the
// sender still owes it (see test_note_on), under the sender's lock.
void InputDispatcher::run_due_midi_test_off(bool force) {
    if (midiTestOffAtMs_ == 0 || (!force && now_ms_ < midiTestOffAtMs_)) return;
    midiTestOffAtMs_ = 0;
    // ⚠️ Never a late off to another device: a hotplug that moved AUTO to a new port drops it (the old
    // port's panic on its close already ended the note).
    if (!port_open() || s_.midiOutOpenName != midiTestPort_) {
        host_.midi_out().drop_test_note();
        return;
    }
    host_.midi_out().end_test_note();
}

// Leaving the test early: backgrounded, focus or controller lost, the MIDI screen left, another
// project. Idempotent; ends only the test note, never panics a playing song.
void InputDispatcher::cancel_midi_test() {
    if (midiTestOffAtMs_ == 0 && !host_.midi_out().test_note_owed()) return;
    midiTestOffAtMs_ = std::max<long long>(midiTestOffAtMs_, 1);
    run_due_midi_test_off(/*force=*/true);
}

void InputDispatcher::clamp_midi_map_cursor() {
    const songcore::Project& p    = host_.project();
    const int                rows = midi_map_row_count(p);   // always ≥ 1 — the ADD row
    s_.midiMapCursorRow    = std::clamp(s_.midiMapCursorRow, 0, rows - 1);
    s_.midiMapCursorColumn = midi_map_clamp_column(p, s_.midiMapCursorRow, s_.midiMapCursorColumn);
}

void InputDispatcher::midi_map_action() {
    // Only the ADD row answers a bare A; the cells above are A+DPAD cells.
    songcore::Project& p = host_.edit_project();
    if (s_.midiMapCursorRow != static_cast<int>(p.midiMappings.size())) return;
    if (static_cast<int>(p.midiMappings.size()) >= songcore::MIDI_MAP_MAX) return;

    // A new row points at the first destination (track 1's fader) over its whole range.
    songcore::MidiMapping& m = p.midiMappings.emplace_back();
    m.controller = 0;
    m.dest       = static_cast<uint8_t>(songcore::MAP_DESTS[0].id);
    m.scopeIndex = 0;
    m.rangeMin   = songcore::MAP_DESTS[0].min;
    m.rangeMax   = songcore::MAP_DESTS[0].max;

    // The cursor stays put, so it is now on the new mapping with the ADD row one below.
    mark_modified();
}

// ─── The cursor and the edit ─────────────────────────────────────────────────────────────────────

CursorContext InputDispatcher::midi_context() const {
    MidiState ms{*s_.project, s_.settings, s_.midiDeviceNames, s_.midiInDeviceNames};
    ms.lastCcChannel  = s_.midiInCcChannel;
    ms.clockBpm       = s_.midiClockBpm;
    ms.cursorRow      = s_.midiCursorRow;
    ms.cursorColumn   = s_.midiCursorColumn;
    ms.deviceIndex    = s_.midiDeviceIndex;
    ms.inDeviceIndex  = s_.midiInDeviceIndex;
    ms.autoOffsetMs   = s_.midiAutoOffsetMs;
    ms.caps           = s_.caps;
    return midi_.cursor_context(ms);
}

// ⚠️ MIDI edits BOTH: PROG CHG is a Project field (dirties the song, like TEMPO); OUTPUT, INPUT
// and OFFSET are settings.json's and must not.
bool InputDispatcher::midi_edit(const InputAction& action) {
    Project& p = host_.edit_project();
    const MidiInputResult r =
        midi_.handle_input(p, s_.settings, s_.midiCursorRow, s_.midiCursorColumn,
                           s_.midiDeviceNames, s_.midiInDeviceNames, action);
    // The side effects the module cannot perform — it has no port.
    if (r.deviceChanged)   apply_midi_device();
    if (r.inDeviceChanged) apply_midi_in_device();
    if (r.offsetChanged)   host_.set_midi_offset_ms(
                               midi_offset_in_force(s_.settings, s_.midiAutoOffsetMs));
    if (r.syncChanged)     host_.set_midi_sync_out(s_.settings.midiSyncOut);
    if (r.controlChannelChanged)
        host_.set_midi_control_channel(s_.settings.midiControlChannel);
    return r.projectModified;
}

CursorContext InputDispatcher::midi_map_context() const {
    const Project& p = *s_.project;
    MidiMapState mm{p};
    mm.cursorRow    = s_.midiMapCursorRow;
    mm.cursorColumn = s_.midiMapCursorColumn;
    return midiMap_.cursor_context(mm);
}

// ⚠️ Mappings are the SONG's, so every edit dirties it. Nothing is pushed: a mapping moves no
// parameter until a knob turns.
bool InputDispatcher::midi_map_edit(const InputAction& action) {
    Project& p = host_.edit_project();
    const MidiMapInputResult r = midiMap_.handle_input(p, s_.midiMapCursorRow,
                                                       s_.midiMapCursorColumn, action);
    // A delete leaves the cursor one past the end — the ADD row, where it should land.
    if (r.rowDeleted) clamp_midi_map_cursor();
    return r.modified;
}

}  // namespace pt::ui
