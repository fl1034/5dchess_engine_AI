# AI spectator matches

The web interface can run two OpenAI players while a spectator approves each
complete turn. Choose White and Black independently from GPT-6 Luna, GPT-6.1
Terra, GPT-6.1 Sol, or GPT-5 mini. Both players use low reasoning effort and
independent conversation histories. No model is preselected. The existing
WebAssembly engine owns the game and decides legality and results.

## Run locally

Build the web interface as described in [Building](building.md#webassembly):

```sh
emcmake cmake -S . -B build-wasm -DEMMODULE=ON -DJSTEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel 4
python3 -m http.server 8001 --bind 127.0.0.1 --directory build-wasm/ui
```

Use a toolchain with the required C++23 standard-library features. Debian
Emscripten 3.1.69 / LLVM 19 needs `-DCMAKE_CXX_FLAGS=-fexperimental-library` on
the configure command. No Python application server, npm dependencies, database,
or AI proxy server is needed.

Open the locally served interface. In **Players & API access**, select both
**White model** and **Black model**. They may use the same model or different
models. Paste your API
key or use the file picker for a local JSON file with a top-level `apiKey` string.
Keep that file outside the repository and served directory. The file picker
reads it locally; the static server does not read or serve it.

**Start new match** remains disabled until both model selections, a key, and the
rules engine are ready. Selecting models never starts paid requests by itself.
The controller also rejects missing or unsupported models before resetting the
game or calling the API. Model choices stay fixed during play and pauses;
enter **Analysis mode** to change them for a new match.

Starting resets to Standard turn-zero. White plans first. The player
can inspect hypothetical prefixes and piece destinations through the engine.
When its full turn passes strict validation, the board displays the draft, the
panel lists all proposed piece moves, and **OK** becomes available.

Click **OK** to commit the whole turn and start the opponent. There is no
automatic approval. A turn may contain several ordered piece moves across
several boards. Invalid moves or incomplete/unsafe turns are rolled back and
returned to the same player for correction.

**Pause** aborts pending browser requests and discards any uncommitted proposal.
**Resume** retries for the current turn owner. If the request safeguard was
reached, increase the request limit in the settings before resuming. A planning
limit pauses after eight model responses in one attempt; resuming grants another
attempt without changing sides. **Analysis mode** clears player credentials and
conversations, unlocks the regular analyzer, and preserves committed play.
Starting a new match requires a key again.

During AI control, manual moves, imports, history navigation, branch editing,
comments, hints, and permissive royal-exposure submission are locked. Camera
controls, orientation, screenshots, and committed-game export remain available.
Unapproved drafts are not included in exports. Wins and stalemate come directly
from the engine. A paused/error/limit stop is not a chess result.

## Credentials, usage, and recovery

### GitHub Project Pages

The public site is deployed separately from a personal GitHub Pages homepage:
`https://fl1034.github.io/5dchess_engine_AI/`.
The `Deploy AI spectator to GitHub Pages` workflow builds and tests a clean
WebAssembly site on pushes to `main`, then publishes only its static runtime.
Repository Settings → Pages must use **GitHub Actions** as its build source.
The release workflow continues to produce release artifacts independently.

The public site uses runtime key entry or the local file picker. The private
ready-to-run ZIP is a separate local-only package and must never be uploaded
to this repository or Pages. Deployment rejects key files, embedded API-key
patterns, ZIP archives, and the private build's automatic key loader.

### API access

Requests go directly from the browser to `https://api.openai.com/v1/chat/completions`.
The key is held in memory, never included in model messages, browser storage,
game exports, or static bundles. Use a trusted local/private copy of the app;
the scripts on a site necessarily have access to a key entered into that site.
The interface requests `store:false`; OpenAI's API data policies still apply.
Closing/reloading the tab requires entering the key again.

Pause and tab-close abort pending browser requests and prevent further
scheduling. Requests already accepted by OpenAI may still finish and incur
charges. The interface shows reported input/output tokens and an approximate
cost; failed or interrupted requests may have usage that was not reported.
The request limit counts all HTTP attempts, including retries. It is not a
provider billing cap. Each generation has a 2,400-token output limit and a
context-size safeguard.

Transient transport errors, HTTP 429, and temporary service errors retry up to
four HTTP attempts, honoring Retry-After or using exponential backoff with
jitter. Authentication, access, billing/quota, persistent failures, and safeguards
pause visibly. Malformed/truncated output is corrected without changing the
turn. Epochs, revisions, proposal identities, and acknowledgements reject stale
work. An ambiguous OK response is reconciled against the committed turn count;
the controller never blindly replays an accepted turn.

The cost display accounts for each player's chosen model separately, using
these standard prices per million tokens:

| Model | Input | Cached input | Output |
| --- | ---: | ---: | ---: |
| GPT-6 Luna | $0.10 | $0.01 | $0.50 |
| GPT-6.1 Terra | $1 | $0.05 | $5 |
| GPT-6.1 Sol | $2 | $0.10 | $10 |
| GPT-5 mini | $0.25 | $0.025 | $2 |

Verify current [model availability and pricing](https://developers.openai.com/api/docs/models)
for your account; the app does not promise playing strength or a fixed cost.

## Rules and state

[`ai-rules.mjs`](../../ui/scripts/ai-rules.mjs) contains the actual rules prompt
and strict response schema. It explains board colors, 0-based coordinates,
historical boards, FEN orientation and limitations, temporal movement,
branching, mandatory/optional timelines, multi-move completion, royal safety,
promotion codes, and terminal results. Its source references are recorded in
the module. Display FEN is never used to reconstruct game state.

Each model response must also identify the supplied state's side, board count,
and mandatory timelines. Incorrect interpretation is rejected before applying
moves. Inspection is read-only: apply a hypothetical prefix, return engine
state/destinations, then undo to the committed boundary. The live game's
castling, pawn, and other history-dependent rights remain authoritative.

## Validation

Offline regression tests:

```sh
node --test ui/test/*.mjs
node build-wasm/ui/wasm/test.js
node build-wasm/ui/wasm/test-ai.mjs
```

The controller tests cover the OK gate, private histories, invalid-proposal and
state-interpretation correction, inspection, cancellation, lost acknowledgements,
request limits, API retries, malformed output, and terminal scheduling.
The real WASM checks cover rollback after an invalid second move, stale/duplicate
commit protection, PGN reload, two required boards in one turn, promotion,
historical branching, and terminal results.

The initial live browser validation used the rules prompt and real API calls:
White proposed e4, Black e5, White Nf3, and Black Nf6. Three actual OK clicks
committed the first three turns; the fourth waited for approval. Both players
correctly interpreted the engine's state summary. A separate live 4×4 fixture
with two mandatory timelines produced a legal two-move turn (a2-a3 on both),
which the engine strictly committed. Eight generation requests in total reported
19,593 input and 3,034 output tokens, approximately $0.011 at the rates above.
This verifies the interaction loop and representative notation understanding,
not exhaustive strategy, long-game reliability, or every time-travel situation.
