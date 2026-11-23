// SPDX-License-Identifier: GPL-3.0-or-later
// Files from a newer TrackWerk: the version rule, the message, a refused load that leaves the open song
// alone, and the numbers every saved song carries.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace songcore;

int main() {
    // The rule: refused only when the file needs a newer reader than this app.
    assert(check_format(json::object(), SONG_FORMAT) == FormatCheck::OK);                          // an old file
    assert(check_format(json{{"version", 1}, {"minReader", 1}}, SONG_FORMAT) == FormatCheck::OK);
    assert(check_format(json{{"version", 2}, {"minReader", 1}}, SONG_FORMAT) == FormatCheck::OK);  // newer, still readable
    assert(check_format(json{{"version", 2}, {"minReader", 2}}, SONG_FORMAT) == FormatCheck::TOO_NEW);
    assert(check_format(json{{"version", 2}}, SONG_FORMAT) == FormatCheck::TOO_NEW);                // minReader = version
    assert(check_format(json{{"minReader", -1}}, SONG_FORMAT) == FormatCheck::DAMAGED);
    assert(check_format(json{{"version", "2"}}, SONG_FORMAT) == FormatCheck::DAMAGED);

    // The message names the version only when it is short and plain.
    assert(too_new_message(json{{"app", "1.4.0"}}) == "MADE WITH TRACKWERK 1.4.0 - UPDATE TO OPEN");
    assert(too_new_message(json{{"app", "<b>x</b>"}}) == "MADE WITH A NEWER TRACKWERK");
    assert(too_new_message(json::object()) == "MADE WITH A NEWER TRACKWERK");

    // A newer song is refused and the open song stays as it was.
    auto engine = std::make_unique<AudioEngine>();
    SongcoreHost host{engine.get(), 48000};
    host.new_project();
    host.edit_project().name = "OPEN";
    const auto path = std::filesystem::temp_directory_path() / "ptnewer-files-song.ptp";
    std::ofstream(path, std::ios::binary) << R"({"version":2,"minReader":2,"app":"1.4.0","name":"NEWER"})";
    assert(!host.load_project_file(path.string(), path.parent_path().string()));
    assert(host.project().name == "OPEN");
    std::filesystem::remove(path);

    // Every saved song says which format wrote it and which reader it needs.
    const json saved = json::parse(serialize_project(host.project()));
    assert(saved.value("version", -1) == SONG_FORMAT.version);
    assert(saved.value("minReader", -1) == SONG_FORMAT.minReader);

    std::cout << "newer files: all passed\n";
    return 0;
}
