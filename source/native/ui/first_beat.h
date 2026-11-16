#pragma once

// First beat: an optional guided start for someone new to trackers (TrackWerk 1.2). It runs in its own
// new project and walks through nine small steps, from the SONG screen to a saved and reloaded beat.
// Each step is ticked off by what the player really does — the project, the screen, the transport —
// never by a timer. A small card shows the step where the scope sits (on the Studio over its footer);
// it takes no input. Help (SELECT) is the way in and out: START there starts or ends it, L steps back.
//
// The card names the same logical buttons everywhere (A, B, START, R + arrows); on a touch screen it
// adds that those are the on-screen buttons. Nothing here needs a network or a sample file: the sound
// comes from a built-in Studio synth.

#include <array>
#include <cstdint>
#include <string>

namespace songcore { struct Project; }

namespace pt::ui {

class Canvas;
struct AppState;

// Top-down, the way a tracker is built: SONG holds chains, a chain holds phrases, a phrase holds notes.
// (With NAV = SONG, the default, R + Right only enters a CHAIN or PHRASE that is really there.)
enum class FirstBeatStep {
    SONG_CHAIN,  // SONG: A puts a chain in
    CHAIN_PHRASE,// CHAIN: A puts a phrase in
    GO_PHRASE,   // reach the PHRASE screen
    SOUND,       // give instrument 00 a built-in synth sound
    NOTES,       // four notes in the phrase
    PLAY,        // hear it: START
    CHANGE,      // change a note
    REMOVE,      // delete a note
    SAVE,        // save the tutorial project
    RELOAD,      // load it again
    DONE,
};
inline constexpr int FIRST_BEAT_STEPS = static_cast<int>(FirstBeatStep::DONE);   // counted steps

struct FirstBeat {
    bool active = false;
    FirstBeatStep step = FirstBeatStep::SONG_CHAIN;
    std::string projectName;              // the tutorial project's own name (FIRSTBEAT, FIRSTBEAT2 …)
    // Taken when a step begins, so "change" and "delete" compare with how the phrase was then.
    std::array<int, 16> notesAtStep{};    // note value per step of the current phrase, -1 = none
    int notesCountAtStep = 0;
    int phraseAtStep = -1;                // the phrase those notes belong to; another one never counts
    int loadsAtStep = 0;                  // AppState::projectLoads when the step began
    // After a step back the card stays on that step until the player acts: the project, the screen
    // or the transport changes. Otherwise a step already done would tick off again at once.
    bool reviewing = false;
    int reviewVersion = 0;
    int reviewScreen = 0;
    bool reviewPlaying = false;
};

// The texts of one step: a title, what to do, and the buttons (all ≤ 44 characters).
struct FirstBeatText { const char* title; const char* todo; const char* keys; };
FirstBeatText first_beat_text(FirstBeatStep step);

// Why the tutorial may not start now (a project change would cut it off), or nullptr: an unsaved
// edit in the open Sample Editor (RAM only), an input
// recording, a live resample being taken or saved, a granular take, the Source Sampler recording,
// rendering or busy. Help itself never stops any of them.
const char* first_beat_blocked(const AppState& s);

// The Sample Editor fills the screen and does not draw the status line: a refused start because of
// its unsaved edit is said in its own message line (SampleEditorState::toolMessage) instead. Saving
// or opening a sample clears it; nothing is saved or discarded for the player.
inline constexpr const char* FIRST_BEAT_SAMPLE_HINT = "FIRST BEAT: SAVE OR OVERWRITE THIS EDIT FIRST";
// Tells a refusal where the player can see it: the status line, and on the Sample Editor its line.
void first_beat_tell_blocked(AppState& s, const char* why);

// Remembers the phrase and the load count for the step about to start.
void first_beat_snapshot(AppState& s);
// Is the current step done? `playing` is the transport (the host's, read back each frame).
bool first_beat_step_done(const AppState& s, bool playing);
// Ticks off every step that is done, in order; true when the step changed. Reaching DONE records it in
// the settings (firstBeatDone). Call once per frame (and after each input in tests).
bool first_beat_poll(AppState& s, bool playing);
// One step back (L in help); the step's snapshot is taken again.
void first_beat_step_back(AppState& s);
// The note count of the phrase being edited (AppState::currentPhrase; step_has_note).
int first_beat_note_count(const AppState& s);

// The invitation beside SELECT HELP: shown while no chain is in the song, the tutorial is not running
// and was never finished (settings.firstBeatDone).
bool first_beat_invite(const AppState& s);

// The coach card, drawn by TrackerLayout over the scope (or the Studio footer) while active.
void draw_first_beat(Canvas& c, const AppState& s);
// The line help shows at its foot: how to start, end or step back.
void draw_first_beat_help_strip(Canvas& c, const AppState& s);

}  // namespace pt::ui
