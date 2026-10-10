# Web UI tests

The dependency-free Node tests cover phone taps, drags, pinch release, and canvas
resizing when panels reserve screen space, and display-density changes without
shifting camera bounds or pointer coordinates, plus export filename metadata
precedence, date validation, sanitization, and byte limits. Piece-cache tests
check independent raster LODs, cache reuse, and the SVG-to-raster zoom boundary:

Run from the repository root:

```sh
node --test ui/test/*.mjs
```

These tests exercise canvas, filename, and piece-cache behavior without a browser.
Piece-cache tests use a simulated canvas; actual raster pixels need browser checks. Layout and game-control
integration should also be checked in the built WebAssembly interface at phone
portrait, phone landscape, tablet, and desktop sizes.

From `ui/`, run `node --test test/*.mjs`.
