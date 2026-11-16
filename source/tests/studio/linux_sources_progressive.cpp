// ptlinux-progressive-check: an archive MP3 heard while it downloads (TrackWerk 1.2, Sources part B),
// against Codex' STUFE2-SOURCES-PRUEFBARE-GRENZEN and STUFE2-ID3-BEFUND, on the real Linux backend and
// a local server (linux_sources_progressive_fixture.py) that holds the rest of each file:
//  - real PCM before HTTP EOF; a render of the heard part before EOF is a complete WAV, byte-equal to
//    the same region rendered after the download finished
//  - OUT past the loaded audio is refused, never cut short; seek stays inside what is loaded
//  - Pause keeps downloading; Stop ends the download (the server sees the hang-up) and keeps the
//    loaded part playable; another track ends the old download, nothing of it reaches the new one
//  - chunked VBR without a length, a 300 KiB ID3 tag, a format change mid-file (the loaded part
//    stays, the reason is said), bytes that are no MP3 (refused, nothing loaded)
//  - PT_SOURCE_PROGRESSIVE=0 keeps the download-then-decode path working
//  - --disk-limit: a cache write fails part-way (file-size limit): said, loaded part stays playable
// No hardware, no public network.
// usage: run through linux_sources_progressive_fixture.py
#include "linux/source-sampler.h"
#include "linux/source-live-mp3.h"
#include "songcore/project_io.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <thread>

using json = songcore::json;
using namespace std::chrono_literals;
namespace fsys = std::filesystem;

namespace {

fsys::path g_out;

void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
json state(pt::platform::SourceSampler& s) { return json::parse(s.state_json()); }
void send(pt::platform::SourceSampler& s, const char* action, json args = json::object()) {
    require(s.command(action, args.dump()), std::string("command refused: ") + action);
}
json wait(pt::platform::SourceSampler& s, const std::function<bool(const json&)>& pred, const char* what, int ms = 10000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        auto j = state(s);
        if (pred(j)) return j;
        std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error(std::string("timeout: ") + what + " / " + s.state_json().substr(0, 600));
}
bool exists(const std::string& name) { return fsys::exists(g_out / name); }
void allow(const std::string& id) { std::ofstream(g_out / ("allow-" + id)) << "go"; }
// Audio through the real hook, as the shell's mixer pulls it; returns the energy heard.
double listen(pt::platform::SourceSampler& s, int ms) {
    float pcm[960];
    double energy = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        std::fill(std::begin(pcm), std::end(pcm), 0.f);
        s.render_audio(pcm, 480, 48000);
        for (const float x : pcm) energy += double(x) * x;
        std::this_thread::sleep_for(10ms);
    }
    return energy;
}
int index_of(const json& j, const std::string& id) {
    const auto& r = j.at("results");
    for (size_t i = 0; i < r.size(); ++i) if (r[i].value("id", "") == id) return int(i);
    throw std::runtime_error("no fixture item " + id);
}
// Opens the item and starts listening; returns once audio is heard (or `audible` is false).
void start(pt::platform::SourceSampler& s, const std::string& id) {
    const auto results = wait(s, [](const json& j) { return j.at("results").size() >= 6; }, "results");
    send(s, "open", {{"index", index_of(results, id)}});
    wait(s, [](const json& j) { return j.value("status", "") == "tracks" || !j.at("tracks").empty(); }, "tracks");
    send(s, "listen", {{"index", 0}});
}
std::string wav_data(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    require(bytes.size() > 44 && bytes.compare(36, 4, "data") == 0, "rendered WAV has a plain 44-byte header");
    return bytes.substr(44);
}
std::string render(pt::platform::SourceSampler& s, int64_t inMs, int64_t outMs) {
    send(s, "set_region", {{"in_ms", inMs}, {"out_ms", outMs}});
    wait(s, [&](const json& j) { return j.value("in_ms", -1) == inMs && j.value("out_ms", -1) == outMs; }, "region");
    send(s, "render", {{"request_id", 7}});
    const auto j = wait(s, [](const json& j) { return j.value("clip_ready", false) || !j.value("error", "").empty(); }, "render");
    require(j.value("clip_ready", false), "render failed: " + j.value("error", ""));
    return wav_data(j.value("clip_path", ""));
}

// The bounded MP3 decoder on its own, fed like a slow network: 4096-byte blocks.
uint64_t decode_blocks(const std::string& bytes, size_t block, size_t firstSplit = 0) {
    pt::platform::source_live::Mp3Decoder decoder;
    auto sink = [](const float*, size_t, int, int) {};
    size_t at = 0;
    if (firstSplit) { decoder.feed(reinterpret_cast<const unsigned char*>(bytes.data()), firstSplit, sink); at = firstSplit; }
    for (; at < bytes.size(); at += block)
        decoder.feed(reinterpret_cast<const unsigned char*>(bytes.data()) + at, std::min(block, bytes.size() - at), sink);
    decoder.finish(sink);
    return decoder.frames();
}
std::string file(const fsys::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
std::string tag(size_t size, bool badSyncsafe = false) {
    std::string t = "ID3";
    t += char(3); t += char(0); t += char(0);
    t += char((size >> 21) & 127); t += char((size >> 14) & 127); t += char((size >> 7) & 127); t += char(size & 127);
    if (badSyncsafe) t[8] = char(0x80);
    return t + std::string(size, '\0');
}
void decoder_checks() {
    const std::string plain = file(g_out / "plain.mp3"), tagged = file(g_out / "id3-300k.mp3");
    const uint64_t frames = decode_blocks(plain, 4096);
    require(frames > 40 * 44100 - 2000, "the plain MP3 decodes (" + std::to_string(frames) + " frames)");
    // Codex: a valid 300 KiB tag must not count against the 256 KiB search for audio.
    require(decode_blocks(tagged, 4096) == frames, "300 KiB ID3 tag, 4096-byte blocks: same frames");
    require(decode_blocks(tagged, 4096, 3) == frames, "the 10-byte ID3 header split across blocks");
    require(decode_blocks(tag(4 * 1024 * 1024 - 10) + plain, 65536) == frames, "a tag just under 4 MiB");
    bool refused = false;
    try { decode_blocks(tag(4 * 1024 * 1024 + 1) + plain, 65536); } catch (const std::runtime_error&) { refused = true; }
    require(refused, "a tag over 4 MiB is refused, not scanned");
    refused = false;
    try { decode_blocks(tag(1000, true) + plain, 4096); } catch (const std::runtime_error&) { refused = true; }
    require(refused, "invalid syncsafe bytes are refused");
    // Garbage still stops after 256 KiB of audio bytes (the bound itself is unchanged).
    refused = false;
    try { decode_blocks(std::string(400 * 1024, char(0x55)), 4096); } catch (const std::runtime_error&) { refused = true; }
    require(refused, "400 KiB without an MP3 frame is refused");
    std::cout << "PASS decoder: 300 KiB ID3 in 4096-byte blocks and split header decode fully; >4 MiB tag, bad syncsafe, no-MP3 refused\n";
}

int normal_run() {
    decoder_checks();
    pt::platform::SourceSampler s((g_out / "data").string());
    send(s, "search", {{"provider", "archive"}, {"genre", "jazz"}, {"query", "fixture"}});

    // ── Held download: audible, renderable and bounded before EOF ──
    start(s, "held");
    double energy = 0;
    const auto began = std::chrono::steady_clock::now();
    while (energy < .5 && std::chrono::steady_clock::now() - began < 5s) energy += listen(s, 50);
    require(energy > .5 && !exists("eof-held"), "audio before the held HTTP EOF");
    // (The state is a snapshot published every 80 ms: wait for it rather than read it once.)
    auto j = wait(s, [](const json& j) { return j.value("downloading", false) && !j.value("busy", true) && j.value("loaded_ms", 0) > 1000; },
                  "downloading and usable");
    require(j.value("total_ms", 0) == 40000 && j.value("loaded_ms", 0) > 1000 && j.value("loaded_ms", 0) < 40000, "loaded part reported");
    require(j.at("capabilities").value("progressive_mp3", false), "capability reported");
    const int64_t loaded = j.value("loaded_ms", 0);
    const std::string early = render(s, 1000, 3000);
    require(early.size() == size_t(88200) * 4, "two seconds, complete, before EOF (" + std::to_string(early.size()) + " bytes)");
    require(!exists("eof-held"), "rendered before EOF");
    // OUT past the loaded audio: refused with the reason, markers unchanged.
    send(s, "set_region", {{"in_ms", 1000}, {"out_ms", 30000}});
    j = wait(s, [](const json& j) { return !j.value("error", "").empty(); }, "refusal");
    require(j.value("error", "").find("past the loaded audio") != std::string::npos, "says why: " + j.value("error", ""));
    require(j.value("in_ms", 0) == 1000 && j.value("out_ms", 0) == 3000, "markers kept");
    send(s, "seek", {{"position_ms", 35000}});
    j = wait(s, [](const json& j) { return j.value("status", "") == "ready"; }, "seek");
    require(j.value("position_ms", 0) <= loaded + 50, "seek stays in the loaded part");
    // Pause keeps downloading.
    send(s, "pause");
    wait(s, [](const json& j) { return !j.value("playing", true); }, "paused");
    allow("held");
    j = wait(s, [](const json& j) { return !j.value("downloading", true); }, "download finished while paused");
    require(exists("eof-held") && j.value("loaded_ms", 0) > 39500 && !j.value("playing", true), "full file while paused");
    require(render(s, 1000, 3000) == early, "the heard part renders byte-identical after EOF");
    // The end of a finished download ends playback.
    send(s, "seek", {{"position_ms", 39000}});
    send(s, "play");
    wait(s, [](const json& j) { return j.value("playing", false); }, "playing");
    double tail = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (state(s).value("playing", false) && std::chrono::steady_clock::now() - t0 < 4s) tail += listen(s, 50);
    require(!state(s).value("playing", true) && tail > 0, "plays to the end and stops");
    std::cout << "PASS held: audible before EOF, 2 s rendered before EOF byte-equal after, OUT past loaded refused, pause keeps loading\n";

    // ── A 300 KiB ID3 tag, then Stop mid-download ──
    start(s, "id3");
    energy = 0;
    const auto b2 = std::chrono::steady_clock::now();
    while (energy < .5 && std::chrono::steady_clock::now() - b2 < 5s) energy += listen(s, 50);
    require(energy > .5 && !exists("eof-id3"), "MP3 behind a 300 KiB ID3 tag plays");
    send(s, "stop");
    j = wait(s, [](const json& j) { return !j.value("busy", true) && !j.value("downloading", true); }, "stopped");
    require(j.value("message", "").find("Download cancelled") != std::string::npos, "Stop says it ended the download");
    const int64_t kept = j.value("loaded_ms", 0);
    require(kept > 0, "loaded part kept");
    const auto b3 = std::chrono::steady_clock::now();
    while (!exists("gone-id3") && !exists("eof-id3") && std::chrono::steady_clock::now() - b3 < 5s) std::this_thread::sleep_for(10ms);
    require(exists("gone-id3") && !exists("eof-id3"), "the held connection was closed by Stop");
    allow("id3");
    std::this_thread::sleep_for(300ms);
    require(state(s).value("loaded_ms", 0) == kept, "nothing more arrives after Stop");
    send(s, "seek", {{"position_ms", 0}});
    send(s, "play");
    wait(s, [](const json& j) { return j.value("playing", false); }, "replay");
    require(listen(s, 300) > .1, "the loaded part still plays");
    send(s, "pause");
    std::cout << "PASS id3 + stop: 300 KiB tag plays, Stop closes the connection and keeps the loaded part\n";

    // ── Chunked VBR without a length, replaced by another track mid-download ──
    start(s, "vbr");
    energy = 0;
    const auto b4 = std::chrono::steady_clock::now();
    while (energy < .5 && std::chrono::steady_clock::now() - b4 < 5s) energy += listen(s, 50);
    require(energy > .5 && state(s).value("total_ms", -1) == 0, "chunked VBR of unknown length plays");
    start(s, "second");
    j = wait(s, [](const json& j) { return j.value("title", "") == "Second source" && !j.value("downloading", true) && j.value("loaded_ms", 0) > 0; }, "second track");
    const auto b5 = std::chrono::steady_clock::now();
    while (!exists("gone-vbr") && !exists("eof-vbr") && std::chrono::steady_clock::now() - b5 < 5s) std::this_thread::sleep_for(10ms);
    require(exists("gone-vbr") && !exists("eof-vbr"), "the old download was ended while held");
    allow("vbr");
    j = state(s);
    require(j.value("loaded_ms", 0) > 7500 && j.value("loaded_ms", 0) < 8500 && j.value("title", "") == "Second source",
            "only the new track's audio (" + std::to_string(j.value("loaded_ms", 0)) + " ms)");
    std::cout << "PASS vbr + switch: chunked VBR plays, a new track ends the old download, only its own audio\n";

    // ── A format change mid-file: the loaded part stays, the reason is said ──
    start(s, "format");
    allow("format");
    // (Bound to this track's title: the snapshot can still show the previous one for a moment.)
    j = wait(s, [](const json& j) { return j.value("title", "") == "Format change" && !j.value("downloading", true) && j.value("loaded_ms", 0) > 0; }, "format change");
    require(j.value("status", "") == "partial" && j.value("message", "").find("Download stopped") != std::string::npos &&
            j.value("message", "").find("format") != std::string::npos, "says the format changed: " + j.value("message", ""));
    require(j.value("loaded_ms", 0) > 7500 && j.value("loaded_ms", 0) < 8600, "the first part is kept");
    std::cout << "PASS format change: first part kept, reason shown\n";

    // ── Not an MP3 ──
    start(s, "broken");
    j = wait(s, [](const json& j) { return !j.value("error", "").empty() && j.value("status", "") == "error"; }, "broken refused");
    require(!j.value("busy", true) && !j.value("downloading", true) && j.value("loaded_ms", 0) == 0, "nothing loaded, not busy");
    require(j.value("error", "").find("MP3") != std::string::npos, "says it is no MP3: " + j.value("error", ""));
    std::cout << "PASS broken: refused without audio, worker free\n";

    // ── The old path on request ──
    setenv("PT_SOURCE_PROGRESSIVE", "0", 1);
    start(s, "second");
    j = wait(s, [](const json& j) { return j.value("title", "") == "Second source" && j.value("playing", false); }, "fallback");
    require(!j.value("downloading", true) && j.value("loaded_ms", 0) > 7500 && listen(s, 300) > .1, "download-then-decode still plays");
    unsetenv("PT_SOURCE_PROGRESSIVE");
    send(s, "stop");
    std::cout << "PASS fallback: PT_SOURCE_PROGRESSIVE=0 keeps the old path\n";
    return 0;
}

int disk_run() {
    // ~1.5 MiB of cache (about 9 s of 44.1 kHz stereo PCM16), then every write fails like a full card.
    std::signal(SIGXFSZ, SIG_IGN);
    rlimit limit{1536 * 1024, 1536 * 1024};
    require(setrlimit(RLIMIT_FSIZE, &limit) == 0, "file-size limit");
    pt::platform::SourceSampler s((g_out / "disk").string());
    send(s, "search", {{"provider", "archive"}, {"genre", "jazz"}, {"query", "fixture"}});
    start(s, "held");
    const auto j = wait(s, [](const json& j) { return !j.value("downloading", true) && j.value("loaded_ms", 0) > 0; }, "disk full", 15000);
    require(j.value("message", "").find("SD card may be full") != std::string::npos, "says the card is full: " + j.value("message", ""));
    require(j.value("loaded_ms", 0) > 3000 && j.value("loaded_ms", 0) < 12000 && !j.value("busy", true), "the written part stays");
    send(s, "seek", {{"position_ms", 0}});
    send(s, "play");
    wait(s, [](const json& j) { return j.value("playing", false); }, "play after full");
    require(listen(s, 300) > .1, "and plays");
    send(s, "stop");
    std::cout << "PASS disk full: a failed cache write is said, the written part stays playable\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "usage: through linux_sources_progressive_fixture.py");
        g_out = argv[1];
        if (argc == 3 && std::string(argv[2]) == "--disk-limit") return disk_run();
        const int code = normal_run();
        std::cout << "ptlinux-progressive-check: PASS\n";
        return code;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
