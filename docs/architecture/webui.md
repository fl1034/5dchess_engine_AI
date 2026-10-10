# Web UI architecture

The static WebAssembly analyzer lives in [`ui/`](../../ui/). Its browser entry
point is `index.html`; the main thread manages DOM controls and canvas rendering,
while a module worker owns the compiled game object. The Python-hosted interface
in `extern/client` is a separate application. Build and local-server instructions
remain in [Building](../usage/building.md#webassembly).

## File structure

| File or directory under `ui/` | Responsibility |
| --- | --- |
| `index.html` | Panel/dialog markup, inline SVG icon definitions, piece asset mapping, theme names, import map, and the `main.js` entry point |
| `scripts/main.js` | Connects UI callbacks and board interactions to worker messages; applies responses to the display |
| `scripts/ui.js` | Desktop controls, dialogs, keyboard shortcuts, comment editing, settings, continuation selection, and export requests |
| `scripts/mobile.js` | Moves the same controls into the compact shell and manages its open sheet |
| `scripts/metadata.js` | Formats headers and controls metadata visibility and expansion |
| `scripts/worker.js` | Loads the Wasm module, owns the game and private next hint, handles commands, and assembles display data |
| `scripts/canvas.js` | Camera animation, viewport sizing, display density, mouse/touch gestures, and render scheduling |
| `scripts/draw.js` | Chess-specific board geometry, visible-board filtering, pieces, labels, highlights, arrows, and focus points |
| `scripts/piece.js` | Loads piece SVGs and prepares raster LOD caches |
| `scripts/color.js` | Reads theme colors for canvas drawing and resolves CSS color values |
| `scripts/parse.js` | Parses board FEN into cells for rendering |
| `scripts/hash.js` | Hashes actions and history paths for remembering navigation choices |
| `scripts/filename.js` | Builds export filenames from game metadata |
| `styles/`, `assets/`, `fonts/` | Layout/style rules, piece SVGs, and bundled label fonts |
| `wasm/` | Generated `engine.js` loader and `engine.wasm`; also contains a binding smoke script |
| `test/` | Dependency-free Node regressions; see [test instructions](../../ui/test/README.md) |

The Emscripten CMake build copies `ui/` into its build directory and emits the
compiled module into that copy's `ui/wasm/`. The HTML import map resolves the
main-thread modules' short import names. The worker uses relative imports.

## User, main thread, and worker communication

`main.js` initializes the compact layout, creates a chess canvas, and starts
`worker.js` with `new Worker(..., { type: 'module' })`. The worker imports
`wasm/engine.js` and creates the compiled module. It reports `engine_version`
and `ready`; after `ready`, the main thread sends the saved settings and loads
the standard turn-zero position.

The worker accesses C++ through [`emmodule.cpp`](../../emmodule.cpp), which
binds [`game`](../../src/client/game.h) and converts board coordinates, actions,
headers, and query results to JavaScript objects. These objects cross the
worker boundary through `postMessage`; the Wasm game itself stays in the worker.
The frontend consumes [Branched 5DPGN](../branched-5dpgn.md) strings for import,
export, and displayed notation rather than implementing chess legality.

| User interaction / main-thread command | Worker work and response |
| --- | --- |
| Select a piece: `gen_move_if_playable` | Queries legal destinations and returns `moves`; the main thread highlights them |
| Select a destination: `apply_move` | Applies the draft move, updates button availability, and returns rendered-position data |
| Submit, Undo, Redo: `submit`, `undo`, `redo` | Mutates the game's turn-editing state and returns updated buttons and boards |
| Previous/Next: `prev`, `next` | Navigates submitted history; Next includes the selected action |
| Hint: `hint` | Reveals the cached action, prepares another unseen action, and refreshes continuations and button state |
| Import: `load` | Parses PGN, replaces the game on success, and refreshes the position, headers, comments, status, and continuations |
| Comment update: `update_comment` | Updates the node's comments and returns the HUD state |
| Settings: `update_settings` | Merges worker settings; notation changes refresh continuation strings |
| Export: `export` | Formats PGN and returns `update_pgn` with the request identifier |
| Display refresh: `view` | Reassembles visible game data without changing the game |

The `data` response contains boards, present, focus, board size, highlights,
phantom boards, and phantom-check arrows. The worker parses each board's FEN;
`main.js` supplies the resulting data to `draw.js`. Separate responses update
controls: `update_buttons` carries availability, `update_select` carries action
objects and their formatted strings, and `update_hud_status` carries status,
headers, and comments. Errors are reported with `alert`.

Continuation text and actions remain paired: the DOM displays `pgn` strings,
while `main.js` keeps the corresponding action array for Next. The worker
remembers previously visited continuations using hashes of the history path.
A formatting-only refresh requests preservation of the current selection.

### Status and hidden hints

When entering a node, the worker calls `suggest_action()` and privately stores
its optional result in JavaScript. That call searches for a legal action absent
from the node's children but does not insert it. In C++, `game` stores
`std::optional<bool> nonterminal`: unknown until searched, true when a legal
witness exists, or false when none exists. Node navigation resets this flag;
partial move editing does not. `get_match_status()` searches only when the flag
is unknown, then distinguishes checkmate from stalemate through phantom checks
when it is false.

With a pending hint, the worker displays the node's move status without asking
for match status again. With no pending hint, it calls `get_match_status()`;
the flag avoids repeating the hypercuboid search. Having no unseen action can
still mean PLAYING when every legal action is already a child.

Hint calls `add_child_action()` with the stored action. This validates and adds
the continuation without navigating or clearing draft moves and redo history.
The worker then prepares the next suggestion and disables Hint when exhausted.
Only revealed hints appear in the game tree and exports. The node's move title
is retained across hint insertion so draft moves do not change whose turn is
reported. Comment changes reuse the pending hint and status cache.

## Assets and CSS

### Pieces, labels, and canvas density

The canvas renders at the display's pixel density, including after browser zoom
or moving between displays. Piece detail is selected using physical pixel size;
camera movement and pointer coordinates remain in CSS pixels. At smaller zoom
levels, pieces use raster images cached once in separate canvases at six sizes
(32, 16, 8, 4, 2, and 1 pixels), retaining level-of-detail rendering without
converting the cache to `ImageBitmap` objects.
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

### Theme data

The color theme in Settings applies to both the boards and the interface on
phones and desktops. Each theme defines the main action color, muted text,
panel backgrounds and text, softer surfaces, and borders. Submit uses the same
main color in both layouts. Desktop panels retain their translucency. Vintage
uses an olive palette. Plain keeps a neutral paper-and-graphite palette with
light gray multiverse cells, darker gray board frames, and stronger square
contrast. Dark uses a midnight palette: ink-blue multiverse cells, ice-blue
frames for White's boards, violet frames for Black's boards, and cool slate
squares. Both board frames remain distinct against either multiverse cell.
L/T labels use the opposite multiverse cell color in every theme, retaining
their existing appearance where they meet the grid boundaries.
Plain's interface uses Material 3 Monochrome; Dark's uses Tonal Spot with seed
`#4f6b91`, both generated at standard contrast using version 0.3.0 of
[Material Color Utilities](https://github.com/material-foundation/material-color-utilities).
The generated values are stored as CSS variables, without a runtime library.
Plain marks the present in muted gold, with amber White moves and blue Black
moves. Dark marks it in indigo, with cyan White moves and rose Black moves.
Generated moves use green/teal, and check uses the Material error color.
The shared palette is defined in
`ui/styles/color-schemes.css`; hover and pressed colors follow it automatically.

### Stylesheet responsibilities

The HTML loads styles in this order:

| Stylesheet | Responsibility |
| --- | --- |
| `color-schemes.css` | Board and semantic interface color variables for each theme |
| `main.css` | Font declarations, document/canvas sizing, and shared page rules |
| `panels.css` | Desktop HUD, navigation, continuation list, metadata, toolbar, and debug presentation |
| `popup.css` | Import/export/settings/help dialogs and form controls |
| `mobile.css` | Compact shell, sheets, touch controls, and overrides for reparented controls/dialogs |

Canvas colors are resolved from CSS variables through `color.js`; changing a
theme refreshes the canvas palette as well as the DOM. SVG piece files are mapped
by piece code in `index.html`; interface icons use inline SVG symbols referenced
with `<use>`.

## Behavior of UI components

### Board canvas

The generic camera and gesture layer lives in `canvas.js`; `draw.js` adds board
coordinates, flipping, focus, and chess-specific rendering. Draft moves,
historical moves, selectable destinations, ordinary checks, and phantom checks
are drawn as separate highlights and arrows supplied by the worker. Phantom
visibility is controlled locally and requests refreshed display data.

### Desktop panels and metadata

Game information, status, and navigation share a top-aligned grid with 24-pixel
outer margins and gaps. Metadata stays at the top left and the 200-pixel-wide
navigation panel stays at the top right. Status and the bottom toolbar share
the full viewport's horizontal center; the toolbar sits 24 pixels above the
bottom edge. The canvas remains full width beneath these floating panels.

The HUD is at most 440 pixels wide, narrowing to 244 pixels at a viewport width
of 1100 pixels to leave a 24-pixel gap beside expanded metadata. Its width and
center stay unchanged when metadata is hidden, shown, or expanded. Metadata
expands from its fixed top-left anchor to 380 pixels wide; oversized details
scroll. The expanded navigation panel is 500 pixels tall and fits within the
minimum 600-pixel viewport height; its continuation list scrolls internally.

The metadata summary displays White, Black, and recorded Result. Expansion
reveals Event, Site, Date, and Round. Full values wrap; summaries truncate long
names. Missing player names become “Unknown Player”, missing details become a
dash, and missing or unfinished results use a muted slash in the expanded view.
The recorded Result is independent of the current node's match status.

Metadata is hidden when Event, Round, White, Black, Site, Date, and Result are
all missing or unknown; an unfinished `*` Result does not count. The saved
**Show metadata panel** setting also controls visibility and hides the phone's
game-details button when disabled. Expansion persists until folded, and loading
a game replaces the displayed headers.

Expand/fold controls have fading hover/focus highlights: a corner highlight for
metadata and centered shallow highlights for HUD and navigation (480 × 50 and
320 × 48 pixels respectively). Hint has its own neutral surface above the
continuations and highlights independently of the selected option.

### Continuations and move notation

The worker formats child actions through `get_child_actions(flags)`. The saved
Game setting **Use shortened notation for moves** defaults to enabled and adds
`SHOW_SHORT` to the capture/promotion flags. Toggling it refreshes the summary
and list while preserving the selected action and position. Export's **Short
Notation** preference is independent.

The compact summary is a single line constrained by the viewport, with CSS
ellipsis for overflow. The expanded list retains full text and wraps even
unbroken move strings. Selecting an option changes the selected continuation;
Next submits its action to the worker for navigation.

### Comments

The HUD's editable comment area tracks the current node's comment, draft edits,
and Update/Discard actions. Worker updates refresh the HUD text; saving sends
`update_comment`. Automatic comment expansion is suppressed in compact mode.
The compact panel heading and matching accessibility labels use “Comment”.
Its header icon reflects open/closed state and whether text exists, including
an unfinished edit. Changing the active compact sheet does not discard edits.

### Compact shell and dialogs

The static analyzer uses floating desktop panels only when the browser viewport
is at least 1100 CSS pixels wide, at least 600 CSS pixels high, and has a
width/height ratio of at least 4/5. All other viewports use the compact shell.
For example, 1280 × 1440 uses desktop panels, while 1400 × 1800 uses compact mode.
The shared media query controls panel placement and compact interaction behavior;
resizing across the boundary reuses the same controls and preserves game state.

On phones, the move/status display stays at the top and Previous, Undo, Submit,
Redo, and Next stay at the bottom with icons above their labels. **Choose** in
the continuation summary opens the continuations panel, where you can choose a
recorded action or request a Hint before using Next. The footer summary stays
on one line within the screen width and truncates long continuations with an
ellipsis; the expanded continuations panel shows the full text. The comment button in the
header, immediately left of the game-information button, opens the comment panel.
Its outline/fill indicates whether the panel is closed/open, and dots indicate
whether the current comment contains text, including an unfinished edit.
The three-dot icon at the right of the header opens the action menu without a
visible heading. It always uses filled dots, with a background highlight while
the menu is open. There
is no separate bottom menu row, giving the canvas more room. Only one panel opens
at a time. All four panels share a circular floating cross button with a theme-colored
soft background, a defined border, a raised shadow, and a 44-pixel touch
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

### Import, export, and settings

Import reads pasted, selected, or dropped text and sends a `load` command.
Parsing and game creation occur in the worker. The main thread handles file
selection and display errors.

`ui.js` owns dialog state and saved preferences. Settings callbacks forward
changes to the worker or apply them locally, such as theme, metadata visibility,
and automatic comment expansion. The continuation-notation preference is sent
with startup settings before loading the initial game.

Export requests carry identifiers so the UI can accept the matching PGN preview.
The worker builds formatting flags and calls `show_pgn()` with the selected
scope and metadata options. Mobile Options changes are applied together when
closed; unchanged options skip recalculation. Copy and Download remain disabled
until the latest requested preview is ready. Desktop keeps an explicit Refresh
workflow and editable filename/extension controls.

#### Export filenames

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
