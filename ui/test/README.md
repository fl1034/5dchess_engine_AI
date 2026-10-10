# Web UI tests

The dependency-free Node tests cover phone taps, drags, pinch release, and canvas
resizing when panels reserve screen space, and display-density changes without
shifting camera bounds or pointer coordinates, plus export filename metadata
precedence, date validation, sanitization, and byte limits. Piece-cache tests
check independent raster LODs, cache reuse, and the SVG-to-raster zoom boundary.
Navigation tests cover sparse/empty grid movement, nearest-board distance,
double-Space, key repeat, and editing/native-control/modifier guards. Camera
tests also check animation convergence at unit zoom. Comment tests cover editable
block boundaries, blank lines, Shift+Enter, and browser caret placeholders.

Run from the repository root:

```sh
node --test ui/test/*.mjs
```

These tests exercise canvas, filename, and piece-cache behavior without a browser.
Piece-cache tests use a simulated canvas; actual raster pixels need browser checks. Layout and game-control
integration should also be checked in the built WebAssembly interface at phone
portrait, phone landscape, tablet, and desktop sizes.

From `ui/`, run `node --test test/*.mjs`.

For panel keyboard checks, build with `cd build-wasm && ./build.sh` and serve
`build-wasm/ui/`. With both desktop panels collapsed, repeatedly press Tab:
the comment editor, hidden Update/Discard actions, and Hint/continuation options
must be skipped, and the status title must not shift vertically. Expand each
panel and verify its controls become reachable. In compact mode, open Comment
and Continuations and verify those same controls remain accessible.

For camera shortcuts, check F, Space, double-Space, Shift+Space, and WASD in the
built UI, including flipped timelines, empty gaps, rapid presses during animation,
and typing in an expanded comment editor. Clicking Submit/Next should return
focus to the canvas; keyboard activation should retain button focus. Dialogs
and the compact tools menu must suppress camera shortcuts.

For comment saving, type several lines using Enter, including an empty line, then
click Update Comment. Verify the worker response retains each line and repeat with
Shift+Enter. Make another edit and discard it; the saved line breaks must remain.
Check both desktop and compact editors. While editing, the panel has an opaque
theme tint; clicking the canvas or a panel control restores its normal surface.
