TrackWerk 1.2.0 native Linux packaging

The desktop profile uses PT_HANDHELD=OFF and distribution SDL2.
For cross builds provide TW_SOURCE_TREE (source/ folder), TW_DEBIAN_SDL_SDK (Debian13 SDK with amd64/usr and arm64/usr), TW_DESKTOP_WORK (empty build folder), and toolchain paths for local Zig0.14.1/CMake.
Run build-desktop.py for arm64 or amd64, then make-deb.py with the same architecture and revision1. Source/package versions are1.2.0 /1.2.0-1. The preserved source digest prevents packaging another application revision.
The synthetic example contains no external samples. No private signing material is included.
