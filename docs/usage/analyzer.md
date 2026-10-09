# Web game analyzer

Open the [online analyzer](https://ftxi.github.io/5dchess_engine/) to explore
positions and branching game histories in your browser. This guide covers the
static WebAssembly interface in `ui/`. For a local copy, follow the
[WebAssembly build instructions](building.md#webassembly) and
[serve the generated interface](building.md#serving-the-browser-interface).

Games use [Branched 5DPGN](../branched-5dpgn.md) to record moves, timelines,
comments, and variations.

## Boards and labels

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

## Move notation

Settings → Game → **Use shortened notation for moves** is enabled by default
and saved across reloads. It uses unambiguous short notation in the continuation
summary and expanded variations list. Disabling it shows full notation; changing
it preserves the selected continuation and current position. Export’s
**Short Notation** setting is independent.

## Game information and desktop panels

The floating game-information panel displays the loaded game’s White and Black headers and recorded Result. Use its bottom-right expand/fold button to reveal Event, Site, Date, and Round in the same panel. The button has a soft gray corner highlight on hover or keyboard focus; the HUD and continuation-panel toggles use wide, shallow fading gray highlights beneath their centered icons (480 × 50 pixels for the HUD and 320 × 48 pixels for the continuation panel). Player names stay side by side with evenly distributed gaps around the result, and full values wrap in the expanded view; long summaries use ellipses. Missing names appear as dark-gray “Unknown Player” placeholders, missing details as a dash, and an unknown or unfinished Result is replaced by a muted slash between the players in the expanded view (omitted in the folded summary).

The panel stays open until folded and updates when another game is loaded. It is hidden when Event, Round, White, Black, Site, Date, and Result are all missing or unknown (an unfinished `*` Result does not count). Settings → Interface → **Show metadata panel**, above **Show debug window**, controls its visibility and is saved across reloads. Disabling it also hides the game-details button on phones; enabling it only shows the panel when the loaded game has displayable metadata.

On desktop, game information, status, and navigation share a top-aligned row with 24-pixel outer margins and 16-pixel gaps (24 pixels on windows at least 1200 pixels wide). The status panel narrows to use the space between the other panels, keeping the move label and indicator on one line. Expanding game information keeps its top-left anchor fixed; on windows too narrow for all three panels, status sits beneath game information. Oversized details scroll within the viewport. The recorded Result is independent of the current position’s move/status display.

Hint reveals one new legal continuation at a time. The next suggestion is
prepared privately when a position is entered or a hint is revealed; it does not
appear in variations or exports until requested. Revealing a hint preserves
partially entered moves and redo history. Hint is disabled when no unseen legal
continuations remain, including when all legal actions are already listed.

On desktop, Hint uses a separate neutral background above the continuations. Hovering or focusing Hint highlights it independently of the selected continuation.

## Appearance

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

## Phone layout

The static analyzer uses a compact layout at widths up to 600 CSS pixels, and on
short landscape screens up to 960 pixels wide and 500 pixels high. Larger
windows retain the floating desktop panels.

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

## Export filenames

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
