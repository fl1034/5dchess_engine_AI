# Client layer

The client layer in [`src/client/game.h`](../../src/client/game.h) and [`game.cpp`](../../src/client/game.cpp) provides a `game` object for interactive use. It combines the [core game logic](core.md) with history navigation, individual move editing, comments, and PGN import/export. Python and WebAssembly bindings expose it to interfaces.

## Game history and variations

A `game` owns a tree of `gnode<comments_t>` nodes and keeps a pointer to the currently selected node. The node implementation lives in [`src/core/gametree.h`](../../src/core/gametree.h); each node records an action, an optional resulting state, child variations, and comments supplied by the client.

`visit_parent()` moves back through submitted actions. `get_child_actions()` lists continuations, while `visit_child()` selects an existing continuation or creates a new one. `get_historical_actions()` returns the actions on the path from the root to the current node.

## Moves within a turn

The client maintains a separate cache of states and moves for the turn being edited. `get_unmoved_state()` returns its starting state, and `get_current_state()` returns the preview state when one exists, otherwise the state at the cache cursor.

`apply_move()` normalizes promotion and checks whether the move can be applied using the core logic. On success, it appends the resulting state and move, discarding cached entries after the cursor. `undo()` and `redo()` move through the cache. These operations are distinct from navigating submitted actions in the game tree.

`can_submit()` asks the core whether the current position can complete a turn. `submit()` gathers cached moves into an action and visits its child node. Moving to a history node refreshes the move cache for that node.

## Temporary move previews

`preview_move(move)` replaces any previous preview with the legal successor of
`now->first`, returning whether calculation succeeded. An invalid move clears the
previous preview. The preview lives outside the move cache and game tree;
`discard_preview()` releases it and silently does nothing when no preview exists.
Both methods are exposed by the WebAssembly binding.

Board-display queries read `get_current_state()`: boards, present, timeline status,
checks, phantom boards/checks, board size, and movable-piece highlights.
Playability and destination generation also read the preview state. Actual move
application and `can_submit()` use `now->first`; submission discards the preview
and submits only the recorded draft moves.
Cached moves, undo/redo availability, history, suggestions, and PGN remain based
on the real position. The worker supplies the temporary move's arrows separately.

`apply_move()`, `undo()`, `redo()`, `submit()`, history navigation, and continuation
insertion discard previews before proceeding, even when the operation fails.
There is no preview-commit method: applying the move normally recalculates it
from the real state. Previewing and discarding preserve existing redo history.

## Interface data and notation

The client exposes boards, playable pieces, move destinations, checks, phantom boards, match status, and promotion options. Interfaces use these queries to display positions and offer interactions without implementing the game rules themselves.

`game::from_pgn()` loads metadata, comments, and variations from [Branched 5DPGN](../branched-5dpgn.md), resolving and validating moves through the core. `show_pgn()` exports the game, with options controlling notation, inclusion of the complete game tree, and metadata. Comments are attached to history nodes; game headers are stored in `metadata`.
