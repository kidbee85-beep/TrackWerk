TrackWerk 1.2.0 for Linux / Debian 13

TrackWerk is Kid Bunka's independent PocketTracker fork. Original project: https://github.com/conanizer/pockettracker . TrackWerk: https://github.com/kidbee85-beep/TrackWerk . Original copyright and all component notices are retained.

Run from the application menu or type trackwerk. Keyboard: arrows navigate; K or Enter = A, J or Escape = B, Space = Start. Hold a function button for context help; Left Shift (SELECT) opens the paged guide; U/I are the shoulder modifiers. In-app controls may be remapped. Consult the bundled upstream README and Studio notes for complete controls.

User data: trackwerk --print-data-dir. TRACKWERK_HOME wins, then POCKETTRACKER_HOME. Otherwise XDG_DATA_HOME/TrackWerk (usually ~/.local/share/TrackWerk). If only an existing PocketTracker directory is present it remains in use; nothing is moved or deleted. Samples, Projects, Songs, Presets and Recordings are user files. Package upgrades/removal/purge replace program files only. Factory presets are copied only when missing; customized files are never replaced. No private sample library is included.

This is a native Linux desktop build with PT_HANDHELD=OFF. It uses the distribution's actual SDL2, sound server and device permissions. External USB input/output requires system support. Direct iPhone audio is experimental and not supplied as a working feature. No phone helper, USB configuration or kernel component is installed.

Useful commands: trackwerk --version; trackwerk --help; trackwerk --print-data-dir. The native program accepts [project.ptp] [media-base-dir] [app-root]. There is NO --render command in this build. For an automated smoke test use the documented environment POCKETTRACKER_QUIT_AFTER_MS=3000, PT_CONSOLE=1 and an isolated TRACKWERK_HOME. A dummy SDL audio/video test proves program startup only, never actual desktop audio.

Public release, VM acceptance and hardware acceptance are distinct. See the release verification notes for actual test scope. Application code is frozen at b24ad7abe43f6784cb8ef34dee7661db0d02dade. Package version: 1.2.0-1; native runtime version: 1.2.0.
