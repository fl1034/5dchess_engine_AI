# Web UI tests

The dependency-free Node tests cover phone taps, drags, pinch release, and canvas
resizing when panels reserve screen space, and display-density changes without
shifting camera bounds or pointer coordinates, plus export filename metadata
precedence, date validation, sanitization, and byte limits. Piece-cache tests
check independent raster LODs, cache reuse, and the SVG-to-raster zoom boundary.
Navigation tests cover sparse/empty grid movement, nearest-board distance,
Space dispatch and focus-view tolerance, hold acceleration and cancellation, key repeat,
and editing/native-control/modifier guards. Camera tests check exact settling
with zero velocity, synchronized position/logarithmic-zoom progress on retargeting, frame subdivision, and
animation convergence at unit zoom, duration calculation, exponential response,
and zero/tiny movements. Comment tests cover editable
block boundaries, blank lines, Shift+Enter, and browser caret placeholders.

Run from the repository root:

```sh
node --test ui/test/*.mjs
```

These tests exercise canvas, filename, and piece-cache behavior without a browser.
Flip tests cover coordinate and click agreement on rectangular boards, rendering
alignment of pieces, squares, labels, highlights and arrows, and changing the
file-order preference without moving boards or the camera.
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

For board orientation, click Flip with **Do not reverse files when flipped** off:
files should run right to left and ranks from 1 at the top to 8 at the bottom on
an 8 × 8 board. Toggle the checkbox under Settings → Interface while flipped;
files should return to left-to-right order without moving boards or the camera.
Verify piece selection and arrows in both modes, then reload to confirm the
checkbox preference persists. Unflipped boards should always show files left to
right.

For comment saving, type several lines using Enter, including an empty line, then
click Update Comment. Verify the worker response retains each line and repeat with
Shift+Enter. Make another edit and discard it; the saved line breaks must remain.
Check both desktop and compact editors. While editing, the panel has an opaque
theme tint; clicking the canvas or a panel control restores its normal surface.

For held navigation, hold a direction for two seconds and verify that motion
accelerates. Release it, focus a comment editor, or blur the window; repeated
steps must stop without a queue of future destinations. Press Space, wait more than ten
seconds, then press it again: the same board must remain centered with surrounding
boards visible. Camera motion must land exactly without a final visible snap.

Space should widen whenever the camera is at a normal Focus view, including a
view reached through F or the Focus button. During the first Space animation,
press it again immediately to widen around the same destination; a third press
returns to normal Focus. Pan away or change zoom and verify Space focuses the
nearest visible board again. Check desktop and compact layouts.

For dragging, move slowly in each of the four directions, then release. The
camera must follow that direction throughout the drag and settling tail, without
reverse movement. Repeat with touch, and interrupt an opposite keyboard camera
movement with a drag. Regression tests cover repeated targets, direction changes,
and takeover from the rendered camera.

For combined movement and zoom, press Space or Focus during an existing camera
movement. Position and logarithmic zoom must start and slow together. Automated
camera tests verify shared progress and the remaining-error derivative after
retargeting, along with exact settling.

For diagonal navigation, press W+A in both key orders within 40 ms and verify
the same diagonal destination. Hold both keys, release one, then release the
other. Opposite keys should cancel per axis, and adding a key should preserve
acceleration. Repeat at distant zoom: the same hold should traverse more grid
steps while diagonal and cardinal movement have the same screen-speed budget.
Tests cover diagonal rays, sparse batched movement, chord order, release behavior,
opposite keys, and zoom/diagonal speed normalization.
