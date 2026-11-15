# TrackWerk build tools

For the 1.1 common Android/Linux source, start with ../BUILD-TRACKWERK.txt. No Android overlay is applied.
The Miyoo rebuild helpers use explicit --source, --app-zip, --zig, --cmake, --sdl-source and --work inputs;
use the matching 1.1 First Install ZIP and the exact mmiyoo SDL source asset.
The source version is read from app/build.gradle.kts. Stable release: 1.1.0 / Android 101003.

update-menu-art.py is a historical 0.9 launcher helper. Use the menu tools inside current release packages
for TrackWerk 1.1. In particular, the Miyoo update ZIP includes fix_miyoo_names.py for legacy Onion metadata.
Run a plan first, then apply it with a backup as described in START-HERE and MENU-NAME-UPDATE.txt.
