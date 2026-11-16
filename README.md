# QWidget media player

The application opens a main window with an embedded QWidget player and automatically plays `asset/test.mp4`
using Qt Multimedia's FFmpeg backend, with audio, play/pause, replay, and seeking.

Player code lives in `src/player`: `player.h` declares the `Player` class and
`player.cpp` implements its media player, widget controls, and playback behavior.
`src/window.h` and `src/window.cpp` define the main window, which owns the
embedded player and sets the window title and initial size.
`src/main.cpp` only configures the backend and starts the application.

Requires Qt 6.5 or newer with Widgets, Multimedia, MultimediaWidgets, and the
FFmpeg media plugin and its runtime libraries.

Build and run with `./build_and_run.sh` (pass `-DCMAKE_PREFIX_PATH=/path/to/Qt`
if Qt is not already discoverable), or use CMake directly:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/black_qt_window
```

On Windows, the executable is `build/bin/black_qt_window.exe`. Run from a Qt
environment with Qt's `bin` directory on `PATH`, or deploy dependencies with
Qt's `windeployqt build/bin/black_qt_window.exe` before launching.

The build copies the video into `build/bin/asset/test.mp4`, so launching the
executable does not depend on the current working directory. When distributing
the app, keep that `asset` directory beside the executable.
