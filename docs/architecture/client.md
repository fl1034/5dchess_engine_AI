# Client layer

The client layer in [`src/client/game.h`](../../src/client/game.h) and [`game.cpp`](../../src/client/game.cpp) provides a `game` object for interactive use. It combines the [core game logic](core.md) with history navigation, individual move editing, comments, and PGN import/export. Python and WebAssembly bindings expose it to interfaces.

## Game history and variations

A `game` owns a tree of `gnode<comments_t>` nodes and keeps a pointer to the currently selected node. The node implementation lives in [`src/core/gametree.h`](../../src/core/gametree.h); each node records an action, an optional resulting state, child variations, and comments supplied by the client.

`visit_parent()` moves back through submitted actions. `get_child_actions()` lists
continuations. The child operations have separate responsibilities:

- `has_child(action)` checks membership without formatting or returning node pointers.
- `visit_child(action)` follows an existing continuation and returns false without
  mutation if it is missing. It never creates a child.
- `add_child(action)` validates and inserts a new continuation without navigating
  or clearing draft moves and redo history. It returns false for duplicate or
  invalid actions.
- `delete_child(action)` removes a recorded continuation and its descendants.

`get_historical_actions()` returns the actions on the path from the root to the
current node. Python and WebAssembly expose the same child-operation names.
Callers that previously relied on `visit_child` to create a branch must now add
it explicitly before visiting; `submit()` still inserts new submitted actions.

Each node keeps an owning vector in presentation/PGN order and a secondary
`std::set` of child pointers compared by action contents. Internal `find_child`
returns the child pointer or null through a transparent lookup by action.
`ext_move::operator<` orders moves by coordinates and promotion.
`action::operator<` uses that ordering to compare canonical move sequences;
for siblings, their shared parent state uniquely determines `branching_index`.
Lookup takes logarithmically many action comparisons. Listing and vector erasure
remain linear, and deletion also destroys the removed subtree.

Insertion updates both containers with rollback if vector allocation fails.
Cloning rebuilds the index against the cloned nodes. Actions stay immutable while
indexed. Each action has exactly one child in both containers.
`gnode::add_child` returns null on duplicate insertion and preserves the existing
child; `game::add_child` returns false for the same case.

PGN loading resolves duplicate canonical actions with **last occurrence wins**.
The incoming subtree is fully constructed and validated before the loader uses
`find_child`, `delete_child`, and `add_child` to replace the earlier branch.
Replacement discards the earlier comments and all descendants, and appends the
survivor at the later occurrence's position. Other siblings retain their order.
This applies recursively, including actions whose independently ordered moves
canonicalize to the same sequence. No merging is performed.

The recursive loader returns the last surviving endpoint to select after import,
avoiding navigation pointers into replaced subtrees. Failed loading leaves the
caller's existing game intact.

## Moves within a turn

The client maintains a separate cache of states and moves for the turn being edited. `get_unmoved_state()` returns its starting state, and `get_current_state()` returns the preview state when one exists, otherwise the state at the cache cursor.

`apply_move()` normalizes promotion and checks whether the move can be applied using the core logic. On success, it appends the resulting state and move, discarding cached entries after the cursor. `undo()` and `redo()` move through the cache. These operations are distinct from navigating submitted actions in the game tree.

`can_submit()` asks the core whether the current position can complete a turn. `get_submit_action()` returns the canonical submittable action through the current cache cursor, excluding redo moves and previews. `submit()` inserts that action when missing and visits its child node, preserving existing comments and descendants when found. It caches the validated submitted state for a new child. Moving to a history node refreshes the move cache for that node.

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

`apply_move()`, `undo()`, `redo()`, `submit()`, `visit_parent()`, and continuation
insertion discard previews before proceeding, even when the operation fails.
`visit_child()` discards previews only on successful navigation; a missing child
leaves the preview, draft, and history unchanged.
There is no preview-commit method: applying the move normally recalculates it
from the real state. Previewing and discarding preserve existing redo history.

## Interface data and notation

The client exposes boards, playable pieces, move destinations, checks, phantom boards, match status, and promotion options. Interfaces use these queries to display positions and offer interactions without implementing the game rules themselves.

`game::from_pgn()` loads metadata, comments, and variations from [Branched 5DPGN](../branched-5dpgn.md), resolving and validating moves through the core. `show_pgn()` exports the game, with options controlling notation, inclusion of the complete game tree, and metadata. Comments are attached to history nodes; game headers are stored in `metadata`.

## Deleting continuations

`gnode::delete_child(const action &)` erases one matching direct child and owns
recursive destruction of its descendants. It returns false when absent and
preserves the order of remaining children. `game::delete_child(const action &)`
exposes this operation at the current node. Successful deletion discards the
preview but preserves the current node, draft moves, undo/redo cache, and legal
move availability. Missing children return false without mutation.

The WebAssembly binding exposes singular `delete_child`, `visit_child`,
`has_child`, `add_child`, and `get_submit_action`. Pending deletion and restoration are entirely in the UI;
C++ stores no tombstones or restoration history.
