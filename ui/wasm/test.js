// test.js
/* This file, together with the `package.json` file in the same folder
are **not** a part of UI. Instead, they are a standalone test script. */
import createModule from './engine.js';
import { readFile } from 'node:fs/promises';
const { parse_FEN } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/parse.js', import.meta.url), 'utf8')));

async function runTest() {
  try {
    const engine_wasm = await createModule();
    console.log('Module loaded!');

    const walls = engine_wasm.from_pgn(`[Mode "5D"]
[Result "0-1"]
[Date "2023.05.13"]
[Time "19:08:21 (+02:00)"]
[Size "8x8"]
[White "Narik"]
[Black "Mauer01"]
[Event "Custom Variant tourney match 2"]
[Game "1"]
[Board "custom"]
[r*dbqk*bdr*/p*p*p*p*p*p*p*p*/1dDDdD1/222/222/1DddDd1/P*P*P*P*P*P*P*P*/R*DBQK*BDR*:0:1:w]`);
    if (!walls.success) throw new Error(walls.message);
    const wallBoard = walls.game.get_current_boards()[0];
    if ('parsed' in wallBoard) throw new Error('Board display must be reconstructed from FEN');
    const cells = parse_FEN(wallBoard.fen, 8, 8);
    if (cells.length !== 8 || cells.some(row => row.length !== 8)
        || cells.flat().filter(cell => cell === '#').length !== 6
        || cells[2][7] !== '#' || cells[3][6] !== '#' || cells[4][6] !== '#'
        || cells[5][7] !== '#' || cells[2][5] !== 'D') {
      throw new Error('Wall display cells lost their positions');
    }
    const reloadedWalls = engine_wasm.from_pgn(walls.game.show_pgn());
    if (!reloadedWalls.success) throw new Error(reloadedWalls.message);
    if (reloadedWalls.game.get_current_boards()[0].fen !== wallBoard.fen) {
      throw new Error('Wall FEN failed to round trip through PGN');
    }
    reloadedWalls.game.delete();
    walls.game.delete();

    const rectangular = engine_wasm.from_pgn('[Board "Custom"] [Size "4x3"] [k3/4/3K:0:1:w]');
    if (!rectangular.success) throw new Error(rectangular.message);
    const rectangle = parse_FEN(rectangular.game.get_current_boards()[0].fen, 4, 3);
    if (rectangle.length !== 3 || rectangle.some(row => row.length !== 4)
        || rectangle.flat().includes('#') || rectangle[0][0] !== 'k' || rectangle[2][3] !== 'K') {
      throw new Error('Display cells include padding outside the declared board');
    }
    rectangular.game.delete();
    console.log('Wall and rectangular board display tests passed');

    const g0 = engine_wasm.from_pgn('[Board "Standard"]');
    if (!g0.success) {
      console.error('Failed to load game:', g0.message);
      return;
    }

    const g = g0.game;
    console.log('Initial game object:', g);

    // Metadata API test: get_metadata/set_metadata roundtrip
    const initialMetadata = g.get_metadata();
    console.log('Initial metadata:', initialMetadata);

    const nextMetadata = {
      Event: 'WASM Binding Test',
      Site: 'Local',
      Date: '2026.03.18',
      Round: '1',
      White: 'TesterA',
      Black: 'TesterB',
      Result: '*',
      Variant: '5D Chess'
    };
    g.set_metadata(nextMetadata);

    const roundtripMetadata = g.get_metadata();
    console.log('Roundtrip metadata:', roundtripMetadata);

    for (const [k, v] of Object.entries(nextMetadata)) {
      if (roundtripMetadata[k] !== v) {
        throw new Error(`Metadata mismatch for key ${k}: expected ${v}, got ${roundtripMetadata[k]}`);
      }
    }
    console.log('Metadata roundtrip test passed');

    const duplicate = engine_wasm.from_pgn(`[Variant "Standard - Turn Zero"]
(1. Nf3 {old} / Nf6 2. Nc3 {discarded})
1. Ng1f3 {replacement}`);
    if (!duplicate.success) throw new Error(duplicate.message);
    const loaded = duplicate.game;
    if (loaded.get_comments()[0] !== 'replacement' || loaded.get_historical_actions().length !== 1
        || loaded.get_child_actions().length !== 0) throw new Error('Duplicate replacement endpoint is incorrect');
    loaded.visit_parent();
    const surviving = loaded.get_child_actions();
    if (surviving.length !== 1 || !loaded.has_child(surviving[0].action)
        || loaded.add_child(surviving[0].action)) throw new Error('Duplicate membership is incorrect');
    loaded.delete();
    console.log('Duplicate PGN replacement test passed');

    const mvs = g.gen_move_if_playable({ l: 0, t: 1, x: 1, y: 0, c: true });
    console.log(
      `moves: ${mvs.length}, first move: l=${mvs[0].l}, t=${mvs[0].t}, x=${mvs[0].x}, y=${mvs[0].y}`
    );
    console.log(`mvs: ${JSON.stringify(mvs)}`);

    for (let i = 0; i < 3; i++) {
        const action = g.suggest_action();
        if (action !== null) {
            if (g.has_child(action)) throw new Error('Suggestion already exists');
            if (g.visit_child(action)) throw new Error('Navigation created a missing child');
            if (!g.add_child(action)) throw new Error('Insertion failed');
            if (!g.has_child(action)) throw new Error('Inserted child is missing');
            if (g.add_child(action)) throw new Error('Duplicate insertion succeeded');
        }
    }

    const suggestions = g.get_child_actions();
    console.log('Suggested actions:', suggestions);
    console.log(JSON.stringify(suggestions[0].action));

    // Visit the first suggested move
    if (!g.visit_child(suggestions[0].action)) throw new Error('Existing navigation failed');
    g.visit_parent();
    if (!g.delete_child(suggestions[0].action)) throw new Error('Deletion failed');
    if (g.has_child(suggestions[0].action) || g.visit_child(suggestions[0].action)) {
        throw new Error('Deleted continuation reappeared');
    }

    // Show PGN and boards
    console.log('PGN after suggestion:', g.show_pgn(engine_wasm.SHOW_NOTHING, true, false));
    console.log('Current boards:', g.get_current_boards());
  } catch (err) {
    console.error('Error loading module:', err);
    process.exitCode = 1;
  }
}

// Run the test
runTest();
