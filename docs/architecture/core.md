# Core game logic

This article explains the game logic in `src/core/` and the supporting utilities in `src/misc/`: state representation, coordinates, move generation, check detection, and notation. See [client architecture](client.md) for game history and interface-facing operations.

## Code structure

The following image roughly captures the structure of the code:
```
  ┌──────────────┐
  │ gametree     │
  │     ╷  ╷ ... │
  └─────┼──┼─────┘
        │  └───────────────────┐
  ┌─────┴────────┐      ┌──────┴────────┐
  │ status 0     │      │ status 1      │  ...     in heap
  │     ╷        │      │      ╷        │
  └─────┼────────┘      └──────┼────────┘
        │                      │
  ┌─────┴────────┐      ┌──────┴────────┐
  │ multiverse 0 │      │ multiverse 1  │
  │     ╷╷╷      │      │    ╷╷╷╷       │
  └─────┼┼┼──────┘      └────┼┼┼┼───────┘
        |||                  |||└──────────┐
        │││   ┌──────────────┘│└─────┐     |       in heap
        │││   │      ┌────────┴────┐ │     │
        │└┼───┼──────┤board (1T1)w │ │     │           ...
        │ └───┼────┐ └─────────────┘ │     │
   ┌────┴─────┴─┐  │ ┌─────────────┐ │┌────┴───────┐
   │board (0T0)b│  └─┤board (0T1)w ├─┘│board (0T1)b│   ...
   └────────────┘    └─────────────┘  └────────────┘


```
I try to follow the terminology described in <https://github.com/adri326/5dchess-notation>. The classes defined are inspired from that. For now, a more comprehensive standard for notation is described in [pgn-bnf.txt](../pgn-bnf.txt).

+ The game tree is represented by `gnode<T>` (defined in `src/core/gametree.h`). Nodes store actions, optional states, child variations, and caller-supplied information. The [client layer](client.md) uses this tree to manage game history and navigation.

+ A `state` object (defined in `src/core/state.h`) stores a `multiverse` directly by value and some additional information such as who is playing. The purpose for that is to distinguish between whether an action is being submitted or not.

+ A `multiverse` object (defined in `src/core/multiverse.h`) is a concrete value type containing a 2-dimensional list of `std::shared_ptr<const board>` snapshots. Copies duplicate timeline containers and metadata while sharing existing boards; applying a move constructs new result boards. Odd/even rules are stored in a `timeline_layout` value (defined in `src/core/timeline_layout.h`), constructed with `timeline_layout::odd()` or `timeline_layout::even()`. Its private enum is not exposed. `mirror(l)` reflects a timeline across the center: `-l` for odd layouts and `-l - 1` for even layouts. The same rule handles negative timeline labels and active-range calculation.

+ A `board` object (defined in `src/core/board.h`) stores piece and color information in bitboards, together with an unmoved-piece mask.

Construct a universe with an explicit layout, for example:

```cpp
multiverse m(timeline_layout::odd(), {{0, 1, false, "k3/4/4/K*3"}}, 4, 4);
state copied(m);             // Copies timeline storage; existing boards stay shared.
state moved(std::move(m));   // Transfers timeline storage without allocation.
```

`state(const multiverse &)` may throw while copying. `state(multiverse &&)`,
state move construction, and state swap are nonthrowing. The variant factory
`create_multiverse_from_variant_setup()` returns a multiverse by value, so a
factory result moves directly into a state. Constructing the universe itself
still allocates and validates input and may throw.

`state::get_board()` and `multiverse::get_board()` return
`std::shared_ptr<const board>` for read-only access with shared ownership.
`get_board_ptr()` provides borrowed read-only access instead. The owning getter
retains its existing error behavior for invalid indices; the borrowed getter
returns `nullptr` for an absent turn. `board::replace_piece()` and
`board::move_piece()` also return `std::shared_ptr<const board>` for new result
boards, leaving the source unchanged.

Standalone `board` values remain editable with `set_piece()`. Construct an edited
board separately before inserting or appending it to a multiverse. Insertion
accepts `std::shared_ptr<const board>`; callers must not mutate a published board
through a retained mutable alias. Constructing it with
`std::make_shared<const board>(draft)` publishes a separate immutable copy.

The `board` FEN constructor parses square contents directly into its bitboards
and unmoved mask. Unspecified squares, including padding outside the declared
dimensions, start as walls. Explicit `*` markers populate the unmoved mask after
placing the corresponding pieces; ordinary `set_piece()` edits continue to
clear that square's unmoved flag. Board construction does not depend on PGN
parsing: `pgnparser` extracts FEN strings and timeline/turn metadata, while the
board constructor interprets the square notation.

`board::to_string()` formats the bitboards directly as an 8 × 8 text grid,
using `. ` for empty squares, `##` for walls, and piece letters followed by a
space. This diagnostic output omits unmoved markers; `get_fen<true>()` preserves
them for serialization.

Move generators borrow their source state or multiverse. Consume or destroy them
before moving, swapping, modifying, or destroying that source. Recreate borrowed
`check_position` views after changing their source. Moving a state no longer
preserves the address of its multiverse through a heap allocation. A moved-from
state or multiverse can be destroyed or assigned a new value; queries and move
generation require a populated value.

In the code, there are two coordinate systems: LTCXY (which is the coordinate for storing moves) and UVXY (which is the coordinate for indexing boards). In both systems, X and Y ranges from 0 to 7. The difference is: L can be positive or negative while U must be greater than or equal to zero. TC are two axes but V is just one axis. The functions `l_to_u` `tc_to_v` `u_to_l` and `v_to_tc` (defined in `src/core/multiverse.cpp`) convert between these coordinates.

-----

Initializer of `vec4` object (defined in `src/misc/vec4.h`) follows the order `(x,y,t,l)`. It supports addition, subtraction, scalar multiplication and comparison. I will talk more about the implementation of this class below.

Movegen
========

For efficient move generation, this program uses bitboards (implemented in `src/core/bitboard.h`, etc.) and magic number hashing methods (implemented in `src/core/magic.h`). Note that magic numbers are only stored for rook/bishop movegen for classic chess. To adapt it for 5D Chess, I used copy masks (see `src/core/multiverse.cpp`).

Below was the python script used for generating code. It is no longer used though.
```python
from itertools import combinations, chain

def functions(domain, range):
    if len(domain) == 0:
        return [[]]
    else:
        x = domain.pop()
        fs = functions(domain, range)
        return [f+[(x,r)] for r in range for f in fs]

p = combinations(['x','y','t','l'], 4)
q = chain(*map(lambda x: functions(list(x), [1,-1]), p))

def show(f):
    for u in f:
        s = dict(u)
        for c in "xytl":
            s.setdefault(c, 0)
        print("vec4({x:2},{y:2},{t:2},{l:2})".format(**s), end=', ')

show(q)
```

Vector of four integers
=============

To implement `vec4` in a way that is very fast in addition, the program uses
a trick from <https://stackoverflow.com/questions/79464417>. In `src/misc/vec4.h`, it is
defined that the data of `vec4` is stored in a 32-bit integer, with
`X_BITS = Y_BITS = 4` and `T_BITS = L_BITS = 12`. The signed coordinate
ranges are -8 to 7 for X/Y and -2048 to 2047 for T/L; board squares still
use X/Y values from 0 to 7. The widths must sum to 32, and X/Y widths must
be greater than 3. See `src/misc/vec4.h` for the current definitions.

## Internal 5DFEN wall extension

This implementation uses `#` in a FEN rank to represent one `WALL_PIECE` square.
This is a project-specific extension, not part of the Branched 5DPGN standard.
Both the C++ board parser and the analyzer's JavaScript FEN parser accept it.
Each `#` advances the file position by one; consecutive walls are printed as
individual `#` characters. Digits compress empty squares only, and `*` is not
valid after a wall.

`board::get_fen(width, height)` prints
exactly the declared rectangle, from the highest rank to the lowest. Walls
inside that rectangle must be printed, including entire ranks of walls. Squares
outside the rectangle are omitted regardless of their contents. Standalone
boards do not store dimensions: the printer defaults to 8 × 8, and multiverse
serialization passes its stored dimensions explicitly. The bitboard printer's
`SHOW_UMOVE` template option preserves unmoved markers; its default omits them.

Short input ranks currently retain their existing behavior: unspecified
trailing squares become walls. Printing normalizes those squares to explicit
`#` characters. Rejecting short ranks is deferred. Game and PGN serialization
use the same size-aware printer, so interior walls and fully walled ranks
survive export and reloading.

The WebAssembly binding sends FEN strings for current and phantom boards. The
worker builds `board.parsed` using the JavaScript FEN parser; there is no separate
binding-generated square array. The renderer recognizes `#` as unavailable
board space and retains the board-border fill at those squares.

Moves & Actions
=============

There are altogether three kinds of moves:
+ A `full_move` object (defined in `src/core/action.h`) stores two coordinates: where the piece comes from and where it goes to. The name `full_move` is to disambiguate with `std::move()`, a C++ STL function.
+ An `ext_move` object (defined in `src/core/action.h`) stores a `full_move` plus which piece it promotes to. `NO_PIECE` means the choice is unspecified and should be resolved using the state's promotion rules. It remains `NO_PIECE` when promotion is disabled, and canonical non-promotion moves also store `NO_PIECE`.
+ A `semimove` object (defined in `src/core/semimove.h`) stores partial information about a move on a specific timeline. For physical moves, it stores the physical move itself; whereas for jumps, it stores either the leaving part or the arriving part, but not both. Not to play anything on this timeline is also a valid semimove (a `null_move`).

The moves can be grouped into actions.
+ A `moveseq` object (defined in `src/core/action.h`) is just an array of `full_move`'s.
+ An `action` object (defined in `src/core/action.h`) stores a sequence of `ext_move`'s, and also make sure the moves are listed in a standard order.

Check Detection
=============
Check detection is not automatically triggered when applying moves or submitting.

For general check detection, use `state::find_checks(attacker)`. It generates every move by `attacker` that captures an enemy royal piece in the current state.

```cpp
const state &base = /* a state */;
const state &position = /* the state after applying moves in base */;
for (full_move move : position.find_checks(attacker))
{
    // Process a checking move.
}
```

When the position is assembled from boards that have already been constructed, use `check_position` to avoid copying a state and its timeline histories. It is a non-owning view: the constructor copies timeline metadata from a base state, and `add_board()` borrows each new endpoint board. The base state and added boards must remain alive and unchanged while the view is used. `HC_info` uses this interface to check a hypercuboid candidate without applying its moves to a temporary state.

The constructor's `l_min` and `l_max` define the inclusive range of timeline indices that the view may contain. Start with `base.get_lines_range()` and extend it to include every timeline on which a board will be added. A branching superphysical move may place its arrival board on a new timeline outside the base range.

```cpp
const full_move mv = /* a superphysical move */;
const state &base = /* state where the move is applied to */;
const int departure_l = mv.from.l();
const int arrival_l = /* resulting arrival_l */;
const board &departure_board = /* after the piece has gone */;
const board &arrival_board = /* after the piece has arrived */;

const auto [base_l_min, base_l_max] = base.get_lines_range();
const int l_min = std::min({base_l_min, departure_l, arrival_l});
const int l_max = std::max({base_l_max, departure_l, arrival_l});

check_position candidate(base, l_min, l_max);
candidate.add_board(
    departure_l,
    next_turn({mv.from.t(), player}),
    departure_board
);
candidate.add_board(
    arrival_l,
    next_turn({mv.to.t(), player}),
    arrival_board
);

// Above need to be done for all moves in the action to perform

if (auto checking_move = candidate.first_check(attacker, include_physical))
{
    // Process one checking move.
}
```

Use `checks()` instead when every checking move is needed. `check_position` only detects royal captures; it does not apply moves, validate actions, submit turns, or determine which timelines are playable.

#### Check detection on phantom boards

To distinguish checkmate from stalemate when a position has no valid actions, perform check detection after advancing every eligible timeline to its phantom board.

The original way to obtain the boolean result constructs a phantom state and searches it with the general move generator:

```cpp
const bool in_check = position.phantom()
    .find_checks(!position.get_present().second)
    .first()
    .has_value();
```

The check-only interface is:

```cpp
const bool in_check = position.has_phantom_check();
```

Both expressions answer the same question. The original form constructs an owning phantom state and calls `state::find_checks()`. `has_phantom_check()` instead creates a borrowed `check_position` with `check_position::for_phantom()` and stops after finding one royal capture, avoiding the copied multiverse and general move generation. Use the original form when the phantom state or its checking moves are needed for further processing; use `has_phantom_check()` when only the boolean result is needed.

Checkmate Detection
=============
There are two checkmate detection program:
1. hc (hypercuboid algorithm), using method from [here](https://github.com/penteract/cwmtt), adapted to c++ with improvements. Some improvements are suggested by the original author.
2. naive, plain DFS searching pruning states with checks/moves not in order.

From my testing, hc has a better worse case performance than naive, especially when the search space is large while available actions are sparse, e.g. when the situation is almost checkmate. However, naive usually perform better when options are abundant.

The hypercuboid algorithm is implemented in `src/core/hypercuboid.cpp`. It is used in checkmate detection, action generation in `src/core/gametree.h` and semimove generation in `src/core/finetree.h`.

Parsing and Printing
=========
The program supports parsing from branched 5dpgn format, whose specification is described in [Branched 5DPGN](../branched-5dpgn.md). The syntax tree is declared in `src/core/ast.h` and the parsing algorithm is implemented in `src/core/pgnparser.h`.

Recall that a full_move in this program only stores the coordinates where it comes from and goes to. Its `to_string()` output is context-free. `lan(state)` and `pgn(state, options)` use the game state for piece and timeline information; the notation implementation lives in `src/core/action.cpp`.
