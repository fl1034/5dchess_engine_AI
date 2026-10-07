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
