# Command-line tools

Build with `TOOLS=ON`; see the [build guide](building.md).

Malformed PGN reports the expected input and the text encountered, for example
`Expected a promotion piece; found end of input.` for an unfinished promotion.

The engine is built as `build/5dchess`, and the general command-line utility is built as `build/5dtools`. For commands that consume a game, provide 5DPGN on standard input and press Control-D to complete it. Current utility commands include:

-  `print [--at <turn>]`: print the selected state of the game
-  `stats [--at <turn>] [--format text|json]`: display position, timeline, and HC-volume statistics
-  `count [--at <turn>] [--format text|json] [<policy>] [<max>]`: display number of available moves capped by `<max>`
-  `all [--at <turn>] [<policy>] [<max>]`: display all legal moves capped by `<max>`
-  `checkmate [--at <turn>] [<policy>]`: determine whether the selected state is checkmate/stalemate
-  `diff [--at <turn>]`: compare the output of two algorithms
-  `perftest [<policy>]`: on each intermediate state, print 1 if an action is available, 0 otherwise
-  `hc-profile [options]`: profile final positions and main-line searches with internal C++ timing
-  `rollout [options]`: run and report random rollout simulations
-  `replay-log <log> [seed]`: replay and time a protocol failure log

Search policies are `balanced` (default), `naive`, `stable`, `iterative`, and
`fine`. The `fine` policy creates a fine tree rooted at the selected position.
For `checkmate` and `perftest`, it searches for one witness action; `count` and
`all` traverse complete action branches up to the requested limit. Printed
actions use the same PGN format as the other policies. The explored tree is
retained during the search, so enumeration can require more memory.

For `print`, `stats`, `count`, `all`, `checkmate`, and `diff`, `--at 0` and
`--at 0b` select the initial
position. A value such as `--at 16b` selects the state immediately after
turn `16b` is submitted. A count equal to its maximum is the cap sentinel;
for example, `max=10001` and a result of `10001` means 10,000 or more
actions.

Legal-action searches account for timelines reactivated by branching. An
action may leave a later board untouched when a reactivated timeline puts the
apparent present on the opponent's turn, including when the action itself
advances that timeline to the opponent's turn.

Checkmate and stalemate are distinguished by simulating a pass only on active
boards at the present. Inactive boards and boards ahead of the present are not
advanced for this check. PGN check symbols use the same rule from the opponent's
perspective after each move; during an unfinished action, the opponent's present
is the turn following the apparent present if that still belongs to the mover.

## HC search profiling

Use a Release build for performance comparisons. Run either or both workloads:

```sh
build/5dtools hc-profile \
  --checkmate test/pgn/slow/*.5dpgn \
  --perftest test/pgn/online/026.5dpgn test/pgn/online/031.5dpgn \
  --policy balanced --warmup-rounds 3 --rounds 5 --format json
```

`--checkmate` measures each game's final position. `--perftest` measures the
initial position and every submitted position on the last-variation main line,
matching the existing perftest convention. File lists end at the next option.
Policies are `balanced`, `stable`, `iterative`, `fine`, and `naive`.

Games are parsed once. Warmups and measured rounds run in the same process;
warmup samples are discarded. Each timer covers finding a legal action or
proving exhaustion, including search-context setup/cleanup and terminal
classification. Parsing, replay, action validation, and printing are excluded.
This does not run the separate softmate searches in `get_mate_type()`.

JSON includes individual samples, per-game round totals, and mean, median, p95
(nearest rank), and maximum times in milliseconds. The perftest mean weights
each position sample equally. Preparation, replay, invalid-action, and
round-consistency failures are recorded explicitly; incomplete results return
exit code 1. Invalid command options return 2.

Press Ctrl+C to print a partial report immediately and exit with code 130.
Only completed, validated measured searches are included; the unfinished search
is omitted. Cases and workload summaries indicate whether all requested rounds
finished. JSON also records `interrupted: true`. The launcher saves this partial
report to its usual output path and prints the available timing summaries.

For a fixed workload, use [the launcher](../../script/profile_hc.py) and
[its manifest](../../script/profile_hc_games.json):

```sh
python script/profile_hc.py
python script/profile_hc.py --policy stable --workload checkmate --rounds 3
```

The manifest records `build/5dtools`, all four current slow fixtures, 50 fixed
online games, and the default rounds. Paths in the manifest are relative to
the repository root; the launcher works from any directory. It invokes C++
once and saves JSON under `logs/hc-profiles/`, including the effective command,
manifest, and executable/input hashes. It does not resample games or build the
executable. Use `--executable` for another build and `--output` for another
result path. `--timeout <seconds>` bounds the entire native invocation, not
individual searches; an exceeded limit saves an explicit timeout result and
returns 124. Neither command is registered in CTest.

## Castling in custom positions

Castling works on smaller boards and with rooks away from the corners. Mark
the king and participating rook as unmoved in the starting FEN (`K*` and `R*`
for White, `k*` and `r*` for Black). They must share a rank, with file coordinates
that differ by at least two and no pieces between them. Neither
piece needs to be on a back rank or an edge file.

The king moves two squares toward the rook, and the rook moves onto the square
the king crosses. If they start two files apart, the king finishes on the
rook's original square. For example, on a 4×4 board, a king on b1 and rook on
d1 finish on d1 and c1. Adjacent kings and rooks cannot castle together.
The king cannot castle out of, through, or into a physical attack, and the
completed action must satisfy the usual multiverse check restrictions.

## Supporting utilities

Convert a 5DUCI position command to full 5DPGN:

```sh
python scripts/uci2pgn.py 'position startpos moves (0T1)e2e3 submit'
```

Open [the line comparator](../../tools/compare.html) in a browser to compare pasted text.
