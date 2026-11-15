#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/chords.h"
#include "songcore/project_io.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/scene_picker.h"
#include "ui/scene_layout.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"
#include "ui/theme_io.h"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

using namespace pt::ui;

namespace {

double luminance(Argb color) {
    const auto channel = [&](int shift) {
        const double v = ((color >> shift) & 255) / 255.0;
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * channel(16) + .7152 * channel(8) + .0722 * channel(0);
}

double contrast(Argb first, Argb second) {
    const double a = luminance(first), b = luminance(second);
    return (std::max(a,b) + .05) / (std::min(a,b) + .05);
}

int count_pixels(const Canvas& c, int x, int y, int width, int height, Argb ink) {
    int total = 0;
    for (int row = y; row < y + height; ++row)
        for (int column = x; column < x + width; ++column)
            total += c.pixels()[row * DESIGN_W + column] == ink;
    return total;
}

void save_view(const Canvas& c, const std::filesystem::path& path) {
    std::ofstream image(path, std::ios::binary);
    image << "P6\n640 480\n255\n";
    for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) {
        const auto pixel = c.pixels()[i];
        const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
        image.write(rgb, 3);
    }
    assert(image.good());
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto dir = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(dir);
    StdFileSystem files((dir / "data").string());
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host(engine.get(), 48000);
    auto initial = songcore::make_default_project();
    initial.tracks[0].chainRefs = {0};
    initial.chains[0].phraseRefs[0] = 0;
    initial.chains[0].phraseRefs[4] = 0;
    initial.chains[0].phraseRefs[8] = 0;
    initial.chains[0].transposeValues[4] = 7;
    initial.chains[0].transposeValues[8] = 244;
    initial.phrases[0].steps[0].note = songcore::Note::C4();
    initial.phrases[0].steps[0].instrument = 0;
    initial.phrases[0].steps[0].fx1Type = songcore::FX_CHD;
    initial.phrases[0].steps[0].fx1Value = 3;
    initial.phrases[0].steps[4].note = songcore::note_from_midi(67);
    initial.phrases[0].steps[4].instrument = 0;
    assert(host.push_project(songcore::serialize_project(initial)));

    const auto unchangedSong = songcore::serialize_project(host.project());
    AppState state;
    state.project = &host.edit_project();
    state.theme = theme_default();
    state.currentScreen = ScreenType::SETTINGS;
    state.settingsCursorRow = static_cast<int>(SettingsRow::THEME);
    state.settingsCursorColumn = 1;
    InputDispatcher input(state, host, files);

    const auto untouched = serialize_theme(state.theme);
    input.on_button_a();
    input.on_button_a();
    assert(state.scenePicker == 0);
    input.on_dpad_right();
    input.on_dpad_down();
    assert(state.scenePicker == 5);
    assert(serialize_theme(state.theme) == untouched);
    input.on_button_b();
    assert(state.scenePicker == -1 && state.themeEditor.isOpen && serialize_theme(state.theme) == untouched);
    input.on_button_a();
    input.on_button_a();
    assert(state.scenePicker == -1 && state.theme.sceneChrome == 1);
    input.on_button_b();
    assert(!state.themeEditor.isOpen);

    const auto key = [&](Button button, ButtonAction action) {
        assert(studio_button({button,action,{}},state,host,*engine,files));
    };
    open_studio_appearance(state);
    key(Button::A, ButtonAction::PRESSED);
    key(Button::A, ButtonAction::RELEASED);
    assert(state.scenePicker == 0);
    key(Button::DPAD_LEFT, ButtonAction::PRESSED);
    assert(state.scenePicker == kSceneCount - kScenesPerFamily);
    key(Button::A, ButtonAction::PRESSED);
    key(Button::A, ButtonAction::RELEASED);
    assert(state.scenePicker == -1 && state.theme.sceneChrome == kSceneCount - kScenesPerFamily + 1);
    key(Button::B, ButtonAction::RELEASED);

    state.currentScreen = ScreenType::PHRASE;
    state.currentPhrase = 0;
    state.cursorRow = 0;
    state.cursorColumn = 1;
    state.settings.fontStyle = 15;
    state.settings.abxyIndex = 2;
    std::array<float,WAVEFORM_SIZE> scope{};
    for (int i = 0; i < WAVEFORM_SIZE; ++i)
        scope[i] = float(std::sin(i * .027) * (.45 + .18 * std::sin(i * .008)));
    state.waveform = scope.data();
    TrackerLayout layout;
    Canvas canvas;
    std::set<std::string> uniqueNames;
    std::set<std::pair<bool,bool>> compositions;
    auto& sample = state.sampleEditor;
    sample.sampleName = "Break test";
    sample.sampleFilePath = "Break test.wav";
    sample.sampleRate = 48000;
    sample.totalFrames = 192000;
    sample.selectionStart = 38400;
    sample.selectionEnd = 144000;
    sample.cursorRow = 8;
    sample.fileMarkers = {42000, 81920, 128400};
    for (int i = 0; i < 620; ++i) {
        const double beat = i % 77;
        const float transient = float(std::exp(-beat / 19) * (0.65 + 0.3 * std::cos(i * 1.17)));
        sample.waveformData.push_back(-transient * .91f);
        sample.waveformData.push_back(transient);
        if (i % 4 == 0) {
            sample.overviewData.push_back(-transient * .91f);
            sample.overviewData.push_back(transient);
        }
    }
    const auto view = [&](const char* name,int scene) {
        if (scene == 0 || scene == 5 || scene == 8 || scene == 12 ||
            scene == 16 || scene == 20 || scene == 24 || scene == 28)
            save_view(canvas,dir / (std::string(name) + "-" + std::to_string(scene) + ".ppm"));
    };
    for (int i = 0; i < kSceneCount; ++i) {
        state.theme = scene_preset(i, VisualizerType::SCOPE);
        const Theme& theme = state.theme;
        assert(uniqueNames.insert(theme.name).second);
        assert(theme.sceneChrome == i + 1 && theme.fontOverride >= 0);
        assert(contrast(theme.textValue,theme.rowEvery4th) >= 4.5);
        assert(contrast(theme.textParam,theme.rowEvery4th) >= 3.0);
        assert(contrast(theme.rowCursor,theme.background) >= 4.5);
        assert(contrast(theme.rowSelection,theme.rowEvery4th) >= 4.5);
        Theme restored;
        assert(parse_theme(serialize_theme(theme),restored));
        assert(serialize_theme(restored) == serialize_theme(theme));
        assert(save_settings(files,state.settings,theme));
        SettingsValues controls;
        assert(load_settings(files,controls,restored));
        assert(serialize_theme(restored) == serialize_theme(theme));
        assert(controls.fontStyle == state.settings.fontStyle && controls.abxyIndex == state.settings.abxyIndex);
        const auto composition = scene_layout(theme);
        compositions.insert({composition.railLeft, composition.scopeBottom});
        assert(theme.fontOverride == 0 || theme.fontOverride >= 7);
        const int boardX = composition.railLeft ? 131 : 10;
        const int boardY = composition.scopeBottom ? SCREEN_SPACER : EDITOR_Y;
        state.currentScreen = ScreenType::PHRASE;
        layout.draw(canvas,state);
        assert(canvas.font_style() == theme.fontOverride);
        // The cursor must be drawn on the same selected step after moving the entire editor.
        assert(count_pixels(canvas,boardX + 48,boardY + 56,42,20,theme.rowCursor) > 80);
        const int scopeY = composition.scopeBottom ? DESIGN_H - SCREEN_SPACER - OscilloscopeModule::HEIGHT : SCREEN_SPACER;
        assert(count_pixels(canvas,10,scopeY,620,70,theme.vizWave) > 250);
        view("phrase",i);
        state.currentScreen = ScreenType::CHAIN;
        layout.draw(canvas,state);
        view("chain",i);
        state.currentScreen = ScreenType::SAMPLE_EDITOR;
        layout.draw(canvas,state);
        const int startMarker = 10 + (composition.splitSampler ? 406 : 620) / 5;
        assert(count_pixels(canvas,startMarker,103,1,80,theme.textTitle) > 60);
        view("sample",i);
        assert(sample.selectionStart == 38400 && sample.selectionEnd == 144000);
        state.studioOpen = true;
        state.studioTab = 3;
        layout.draw(canvas,state);
        view("punch",i);
        state.studioOpen = false;
        state.currentScreen = ScreenType::PHRASE;
        state.scenePicker = i;
        layout.draw(canvas,state);
        view("picker",i);
        state.scenePicker = -1;
    }
    assert(compositions.size() == 4);
    Theme legacy;
    assert(parse_theme(R"({"name":"LEGACY","fontOverride":99,"sceneChrome":-3})",legacy));
    assert(legacy.fontOverride == -1 && legacy.sceneChrome == 0);
    assert(parse_theme(R"({"name":"UNSCII / BLOCK PARTY","fontOverride":4,"sceneChrome":1})",legacy));
    assert(legacy.fontOverride == kScenePresets[0].font);
    assert(parse_theme(R"({"name":"UNSCII / BLOCK PARTY","fontOverride":6,"sceneChrome":1})",legacy));
    assert(legacy.fontOverride == 6);
    legacy.fontOverride = 4;
    assert(parse_theme(serialize_theme(legacy),legacy));
    assert(legacy.fontOverride == 4);
    assert(songcore::serialize_project(host.project()) == unchangedSong);
    std::puts("PASS: 40 scene presets, legible typography migration, four tracker arrangements, waveform markers, Scene browser, and Studio layouts.");
}
