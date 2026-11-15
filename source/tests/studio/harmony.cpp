#include "ui/harmony.h"
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "ui/studio.h"

#include <cmath>
#include <filesystem>
#include "songcore/effects.h"
#include "studio/synth.h"
#include <cassert>
#include <fstream>
#include <memory>
#include <set>

using namespace pt::ui;

int main(int argc, char** argv) {
    assert(argc == 2);
    std::filesystem::create_directories(argv[1]);
    HarmonyState state;
    std::set<std::array<int,8>> distinct;
    for (int style = 0; style < 6; ++style) {
        for (int preset = 0; preset < 8; ++preset) {
            state.style = style;
            state.preset = preset;
            harmony_load(state, false);
            std::array<int,8> pitches{};
            for (int i = 0; i < 4; ++i) {
                pitches[i*2] = state.slots[i].note;
                pitches[i*2+1] = state.slots[i].chord >> 4;
            }
            distinct.insert(pitches);
        }
    }
    assert(distinct.size() == 48);
    state.slots[0] = {51, 0x74, true};
    harmony_load(state, true);
    assert(state.slots[0].note == 51 && state.slots[0].chord == 0x74);
    state.instrument = 0;
    state.strum = 2;
    songcore::Phrase phrase(0);
    phrase.steps[0].fx2Type = songcore::FX_ARC;
    assert(!harmony_fill_phrase(state, phrase));
    assert(phrase.steps[0].fx2Type == songcore::FX_ARC);
    phrase = songcore::Phrase(0);
    assert(harmony_fill_phrase(state, phrase));
    assert(phrase.steps[0].fx1Type == songcore::FX_CHD && phrase.steps[0].fx1Value == 0x74);
    assert(phrase.steps[0].fx2Value == 2 && phrase.steps[4].fx1Type == songcore::FX_CHD);

    assert(studio::variation_count() == 16);
    std::set<int> codes;
    for (int model = 0; model < studio::model_count(); ++model) {
        for (int v = 0; v < 16; ++v) {
            const int code = studio::preset_code(model, v);
            assert(codes.insert(code).second && studio::valid_code(code));
            assert(studio::uri_code(studio::uri(code).c_str()) == code);
            assert(studio::preset_model(code) == model && studio::preset_variation(code) == v);
            assert(studio::preset_name(code) != "UNKNOWN");
            for (const int value : studio::preset_macros(code)) assert(value >= 0 && value <= 255);
        }
    }
    // Render the four new variations through the real oscillator dispatch.
    studio::Synth oscillator;
    for (int model = 0; model < studio::model_count(); ++model) {
        for (int variation = 12; variation < 16; ++variation) {
            oscillator.reset();
            oscillator.start(studio::preset_code(model, variation));
            for (int frame = 0; frame < 384; ++frame)
                assert(std::isfinite(oscillator.next(220.f, 48000.f)));
        }
    }
    auto engine = std::make_unique<AudioEngine>();
    engine->setDeviceSampleRate(48000);
    songcore::SongcoreHost host(engine.get(),48000);
    auto project = songcore::make_default_project();
    project.instruments[0].instrumentType = songcore::InstrumentType::EXTERNAL;
    assert(host.push_project(songcore::serialize_project(project)));
    AppState app;
    app.project = &host.edit_project();
    app.theme = theme_studio();
    app.currentScreen = ScreenType::PHRASE;
    app.currentPhrase = 0;
    app.studioOpen = true;
    app.studioTab = 8;
    app.currentInstrument = 0;
    StdFileSystem files(argv[1]);
    InputDispatcher dispatcher(app,host,files);
    harmony_tick(app,host,*engine,20);
    app.harmony.cursor = 12;
    harmony_button({Button::A,ButtonAction::PRESSED,{}},app,host,*engine);
    assert(app.harmony.confirm && !app.harmony.write);
    harmony_button({Button::A,ButtonAction::PRESSED,{}},app,host,*engine);
    assert(app.harmony.write);
    dispatcher.apply_harmony_phrase();
    assert(host.project().phrases[0].steps[0].fx1Type == songcore::FX_CHD);
    assert(!app.harmony.write);
    app.studioOpen = false;
    assert(dispatcher.on_phrase_history(Button::DPAD_UP, false));
    assert(host.project().phrases[0].steps[0].note == songcore::Note::EMPTY());
    app.studioOpen = true;
    Canvas view;
    app.harmony.cursor = 5;
    for (const int slot : {0,2,1,3}) {
        app.harmony.slot = slot;
        harmony_button({Button::A,ButtonAction::PRESSED,{}},app,host,*engine);
        harmony_button({Button::A,ButtonAction::RELEASED,{}},app,host,*engine);
    }
    assert(harmony_tick(app,host,*engine,100) && app.harmony.sparkUntil > 100);
    assert(app.harmony.sparkKind == 0);
    for (const int slot : {3,1,2,0}) {
        app.harmony.slot = slot;
        harmony_button({Button::A,ButtonAction::PRESSED,{}},app,host,*engine);
        harmony_button({Button::A,ButtonAction::RELEASED,{}},app,host,*engine);
    }
    assert(harmony_tick(app,host,*engine,120) && app.harmony.sparkKind == 1);
    assert(harmony_tick(app,host,*engine,2000) && !app.harmony.sparkUntil);
    draw_studio(view,app);
    std::ofstream image(std::string(argv[1]) + "/Harmony.ppm",std::ios::binary);
    image << "P6\n640 480\n255\n";
    for (int i=0;i<640*480;++i) {
        const auto pixel=view.pixels()[i];
        const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};
        image.write(rgb,3);
    }
    std::vector<float> fixture(24000);
    for (int i=0;i<int(fixture.size());++i) fixture[i]=0.15f*std::sin(i*.043f);
    const auto path=std::filesystem::absolute(std::filesystem::path(argv[1])/"tone.wav");
    assert(songcore::write_wav_mono(path.string(),fixture,48000));
    project.instruments[0].instrumentType=songcore::InstrumentType::SAMPLER;
    project.instruments[0].sampleId=0;
    project.instruments[0].sampleFilePath=path.string();
    assert(host.push_project(songcore::serialize_project(project)));
    assert(host.load_media("").loaded==1);
    host.push_params();
    app.harmony.slot=0;
    harmony_button({Button::A,ButtonAction::PRESSED,{}},app,host,*engine);
    assert(app.harmony.held);
    float block[1024]{};
    double energy=0.0;
    for(int i=0;i<8;++i){
        engine->processLiveBlock(block,512,2,48000);
        for(float sample:block){ assert(std::isfinite(sample)); energy+=double(sample)*sample; }
    }
    assert(energy>1e-4);
    harmony_button({Button::A,ButtonAction::RELEASED,{}},app,host,*engine);
    for(int i=0;i<16;++i) engine->processLiveBlock(block,512,2,48000);
    assert(!app.harmony.held && engine->getActiveVoiceCount()==0);
    std::puts("PASS sampler chord audition emits PCM and releases the preview lane");
    std::puts("PASS 48 progressions, locks, safe write and Undo, 1968 preset IDs, new preset audio and two hidden animations");
}
