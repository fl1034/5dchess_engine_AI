# Building

Run the build commands below from the repository root unless stated otherwise.

## Requirements

- CMake 3.20 or newer.
- A compiler and standard library with C++23 support.
- Python 3.11 or newer for Python bindings and scripts.
- Emscripten for WebAssembly builds.

The release workflow builds with GCC 13 on Linux, Apple Clang with Xcode 26 on macOS, and Visual Studio 2022 on Windows. Older toolchains may lack required standard-library features such as `std::jthread` and `std::stop_token`.

## Native programs

```sh
cmake -S . -B build -DENGINE=ON -DTOOLS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Use Release builds for search performance. Compiler optimizations have a substantial effect on this project.

Single-configuration generators put the executables at `build/5dchess` and `build/5dtools`. Multi-configuration generators such as Visual Studio put them under `build/Release/`; Windows executables have an `.exe` suffix. Adjust executable paths in examples accordingly.

### Build options

| Option | Product |
|---|---|
| `ENGINE` | 5DUCI engine executable |
| `TOOLS` | Command-line utilities |
| `TEST` | Native tests |
| `PYMODULE` | Python binding |
| `EMMODULE` | WebAssembly binding and static web interface |

When `TOOLS` or `TEST` is enabled, PGN fixtures are copied from `test/pgn/` to the build directory’s `pgn/` folder, including subfolders.

All options default to off. With every option disabled, CMake builds only the core C++ library. Enable both `TOOLS` and `TEST` to include tools-related tests. See [Contributing](../../CONTRIBUTING.md#testing) for test commands.

## IDE setup

Generate an Xcode project:

```sh
cmake -S . -B build-xcode -DTOOLS=ON -DTEST=ON -G Xcode
```

Or a Visual Studio 2022 project:

```cmd
cmake -S . -B build-vs -DTOOLS=ON -DTEST=ON -G "Visual Studio 17 2022"
```

## Python module

Initialize the submodules in an existing checkout:

```sh
git submodule update --init --recursive
```

For a fresh checkout:

```sh
git clone --recurse-submodules https://github.com/ftxi/5dchess_engine.git
cd 5dchess_engine
```

The `extern/pybind11` submodule is needed for the binding; `extern/client` provides the graphics interface used by the Python host.

Use Python 3.11 or newer; Python 3.12 is tested. A virtual environment is
optional. Use the same interpreter to install dependencies, build the binding,
and run scripts/tests. For the [graphics interface](https://github.com/SuZero-5DChess/5dchess_client)
and web API tests, install Flask and Flask-SocketIO in that interpreter:

```sh
python -m pip install flask flask-socketio
```

From the repository root, configure and build using your selected Python
(the following example uses a POSIX shell):

```sh
cmake -S . -B build -DPYMODULE=ON -DENGINE=ON -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build build --config Release --parallel
python -m unittest discover -s scripts/test -v
```

You can replace `python` with an absolute interpreter path, such as
`/opt/anaconda3/bin/python`. CMake discovers Python automatically when
`Python_EXECUTABLE` is omitted, but its cached selection can differ from the
shell's `python`. Passing the path explicitly keeps the binding and runtime
aligned. Reconfigure and rebuild when changing interpreters. `ENGINE=ON` builds
the executable used by autoplay and the real-engine smoke test.

To use it, go to the base directory of this project and run `python scripts/host.py`. Then, visit `http://127.0.0.1:5000` with your favourite browser.

## WebAssembly

Requires [emscripten](https://emscripten.org).

```sh
emcmake cmake -S . -B build-wasm -DEMMODULE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

The static website is generated in the `build-wasm/ui/`.

The canvas renders at the display's pixel density, including after browser zoom
or moving between displays. Piece detail is selected using physical pixel size;
camera movement and pointer coordinates remain in CSS pixels.
Timeline and turn labels use upright Playfair with lining numerals. The bundled
`PlayfairLining-Regular.woff2` is a static instance at weight 400, optical size 14,
and width 100. Its digit character mappings use the upstream `lnum` glyphs so
canvas text renders lining numerals without relying on CSS OpenType settings.
Timeline labels align the visible top of `L` exactly with the upper edge of the
multiverse grid square, using the font’s measured text bounds.

Board row and column labels use locally bundled Playfair Italic at regular weight
with a small-text optical size of 14. Rank digits use tabular old-style figures
(`onum` + `tnum`), baked into the digit mappings of
`PlayfairItalicTabularVF.woff2` for canvas rendering. Column labels
share a baseline with room for descenders such as `g` inside the squares.
Row and column labels align their visible left edges with a 0.05em inset inside
each square. Row labels retain a 0.1em top inset, accounting for the italic
numerals' different heights and overhangs.
Playfair is bundled in `ui/fonts/` with its SIL Open Font License, which permits
commercial use. The font and license come from the
[official Playfair repository](https://github.com/clauseggers/Playfair/tree/b869b3a82f7188b4e790f9ef1ebe2d11630bde8f).

The floating game-information panel displays the loaded game’s White and Black headers and recorded Result. Use its bottom-right expand/fold button to reveal Event, Site, Date, and Round in the same panel. The button has a soft gray corner highlight on hover or keyboard focus; the HUD toggle uses the same fading gray treatment beneath its centered icon. Player names stay side by side with evenly distributed gaps around the result, and full values wrap in the expanded view; long summaries use ellipses. Missing names appear as dark-gray “Unknown Player” placeholders, missing details as a dash, and an unknown or unfinished Result is replaced by a muted slash between the players in the expanded view (omitted in the folded summary). The panel stays open until folded and updates when another game is loaded. On narrow windows it sits below overlapping controls; oversized details scroll within the viewport. The recorded Result is independent of the current position’s move/status display.
### Phone layout

The static analyzer uses a compact layout at widths up to 600 CSS pixels, and on
short landscape screens up to 960 pixels wide and 500 pixels high. Larger
windows retain the floating desktop panels.

On phones, the move/status display stays at the top and Previous, Undo, Submit,
Redo, and Next stay at the bottom with icons above their labels. **Choose** in
the continuation summary opens the continuations panel, where you can choose a
recorded action or request a Hint before using Next. The comment button in the
header, immediately left of the game-information button, opens the comment panel.
Its outline/fill indicates whether the panel is closed/open, and dots indicate
whether the current comment contains text, including an unfinished edit.
The three-dot icon at the right of the header opens the action menu without a
visible heading. It always uses filled dots, with a background highlight while
the menu is open. There
is no separate bottom menu row, giving the canvas more room. Only one panel opens
at a time. All four panels share a circular floating cross button with a muted
green background, a defined green border, a raised shadow, and a 44-pixel touch
target. It sits above the panel in portrait and beside it in landscape, so the
action menu starts directly with its buttons. The other panels keep their useful
headings. Tapping the highlighted header icon or **Choose** again also closes its
panel. The tag button in the header opens the loaded game's metadata; its outlined icon becomes
filled while Game details is open. Comments keep the existing
Update/Discard behavior; switching panels does not discard an unfinished edit.
Automatic comment expansion is suppressed in the compact layout.

Panels reserve canvas space below the boards in portrait and beside them on short
landscape screens. The canvas preserves its center and relative zoom when the
available space changes. **Focus** stays in the board area's lower-right corner,
moving above the cross button when a portrait panel is open, and fits the focused board within that area; repeated presses cycle the focus
points. A newly loaded game is focused automatically on phones. Drag to pan,
pinch to zoom, and tap to select pieces and destinations. Releasing a drag or
pinch does not select a piece.

Import, export, settings, and help dialogs fill the phone viewport with the same
warm background as the navigation bars. A compact header places the title beside
a flat green **Close** text button with a 44-pixel touch target, subtle pressed
feedback, and a keyboard focus outline; content scrolls below it. Export has a single
**Options / Copy / Download** footer. Options opens a full-screen view on phones
and contains all export settings. Toggling settings does not calculate PGN.
Closing Options saves changed selections and requests one preview update;
closing without changes skips recalculation. Export returns immediately, showing
“Updating preview…” while keeping the previous preview visible. Copy and Download
are unavailable until the latest preview is ready. Rotating or resizing back to a
desktop-sized window restores the original controls without resetting the game.

### Export filenames

Mobile Download and the desktop default filename use the loaded game's headers:
`{event}_round_{round}_{white}_vs_{black}_{yyyymmdd}`. Missing Round omits
`_round_{round}`; missing Event omits both Event and Round. One missing player
becomes `anonymous`, while two missing players omit the match portion. Without
Event or players, the prefix is Site, or `game` when Site is missing too. Empty
values and `?` placeholders count as missing.

A complete, valid Date header is used; otherwise the browser's current UTC date
is used. Spaces, control characters, and characters unsafe in Windows filenames
become underscores; Unicode is preserved and normalized. Values that sanitize
to nothing count as missing. Reserved device names are prefixed with `game_`.
Long metadata is shortened to keep the entire filename within 180 UTF-8 bytes,
preserving the date and extension. Export options do not affect the default
filename. Desktop retains its editable filename and extension fields, Mate and
Short Notation controls, More Options panel, Save as File button, and explicit
Refresh workflow; closing desktop More Options does not regenerate the preview.

### Serving the browser interface

Note that simply double-clicking index.html will likely fail to initialize the JavaScript components due to [CORS (Cross-Origin Resource Sharing)](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CORS) restrictions enforced by modern browsers when using the `file://` protocol.

To run the application correctly, you must serve the directory via a local web server. Use one of the following methods after running `cd build-wasm`:

If you have python installed:
```sh
python -m http.server 8080 --directory ui/
```
If the emsdk is already sourced in your environment:
```sh
emrun ui/
```
If you prefer [darkhttpd](https://github.com/emikulic/darkhttpd):
```sh
darkhttpd ui/
```
