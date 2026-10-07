# Online Game Database

These 62 games were imported from the local `logs/games.toml` collection (`decks.5dc`), with `logs/human-game-27-two-timelines.5dpgn` restored at its original position, 27. Filenames and `Round` headers use the restored one-based order. `Event` and `Site` identify the source collection; original player names, dates, and online URLs were not available in these sources.

Games 01–26 correspond to TOML indices 0–25. Game 27 uses a custom two-timeline starting position and preserves its two 5DFEN boards. Games 28–62 correspond to TOML indices 26–60. All other games use `Variant "Standard - Turn Zero"`.

Game 52 restores a missing space between `(-1T20)Bc3a5+` and `(1T20)Qe4xd5` on turn 23. The moves themselves are unchanged.

When `TOOLS` or `TEST` is enabled, the build copies `test/pgn/` to `build/pgn/`. Native test executables are placed in `build/test/`, including with multi-configuration generators. `run_games` locates the copied fixtures relative to its executable, so it can be launched from any working directory.

The `run_games` native test loads every `.5dpgn` file in this folder through the client game parser. This folder is separate from the cwmtt fixtures attributed in the parent directory's `README.txt`.
