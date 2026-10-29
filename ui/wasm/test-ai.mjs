// Run after a JSTEST WebAssembly build: node build-wasm/ui/wasm/test-ai.mjs
import assert from 'node:assert/strict';
import createModule from './engine.js';
import { EngineMatch } from '../scripts/ai-engine.mjs';
const engine = await createModule();
const move = (x,y,t,l,xx,yy,tt=t,ll=l,promote_to=0) =>
    ({from:{x,y,t,l},to:{x:xx,y:yy,t:tt,l:ll},promote_to});
let sequence = 0;
function fixture(pgn = '[Board "Standard - Turn Zero"]') {
    let game;
    const adapter = new EngineMatch({...engine,from_pgn:()=>engine.from_pgn(pgn,false)},
        ()=>game,next=>{game?.delete();game=next;},()=>{});
    const send = (op,extra={}) => adapter.handle({...adapter.identity(),requestId:String(++sequence),op,...extra});
    const initial = send('start',{matchId:'fixture-'+sequence});
    assert(initial.ok, initial.error);
    return {adapter,send,get game(){return game;},close:()=>game.delete()};
}

{
    const f = fixture(), before = f.send('state').state;
    const e4 = move(4,1,1,0,4,3);
    const invalid = f.send('propose',{moves:[e4,move(4,1,1,0,4,7)]});
    assert.equal(invalid.ok,false);
    assert.deepEqual(f.game.get_current_boards(),before.boards);
    assert.equal(f.game.can_undo(),false);
    assert.equal(f.game.get_historical_actions().length,0);
    assert.equal(f.send('state').state.pgn,before.pgn);
    const inspected = f.send('inspect',{moves:[],source:e4.from});
    assert(inspected.destinations.some(p=>p.x===4&&p.y===3));
    assert.deepEqual(f.game.get_current_boards(),before.boards);
    const staged = f.send('propose',{moves:[e4]});
    assert(staged.ok,staged.error);
    assert.equal(f.game.get_historical_actions().length,0);
    assert.equal(staged.state.side,'White');
    assert.equal(staged.state.pgn,before.pgn);
    assert(staged.state.canSubmit);
    const stale = f.adapter.handle({...before,requestId:'stale',op:'commit',proposalId:staged.state.proposal.id});
    assert.equal(stale.ok,false);
    const commit = {...f.adapter.identity(),op:'commit',requestId:'one-commit',proposalId:staged.state.proposal.id};
    const accepted = f.adapter.handle(commit);
    assert(accepted.ok,accepted.error);
    assert.equal(accepted.state.side,'Black');
    assert.equal(accepted.state.commits,1);
    assert.deepEqual(f.adapter.handle(commit),accepted);
    assert.equal(f.game.get_historical_actions().length,1);
    assert.equal(f.send('commit',{proposalId:commit.proposalId}).ok,false);
    const loaded = engine.from_pgn(accepted.state.pgn,false);
    assert(loaded.success,loaded.message);
    assert.deepEqual(loaded.game.get_current_boards(),f.game.get_current_boards());
    loaded.game.delete();
    const black = f.send('propose',{moves:[move(4,6,1,0,4,4)]});
    assert(black.ok,black.error);
    const pause = f.send('pause');
    assert.equal(pause.state.side,'Black'); assert.equal(pause.state.proposal,null);
    assert.equal(f.game.get_historical_actions().length,1);
    assert.equal(f.adapter.handle({...black.state,requestId:'late',op:'commit',proposalId:black.state.proposal.id}).ok,false);
    f.close();
    console.log('PASS real WASM: rollback, inspection, preview, strict OK, stale/duplicate protection, PGN reload, pause.');
}
{
    const f = fixture('[Board "Custom"] [Size "4x4"] [3k/4/P3/K3:0:1:w] [3k/4/P3/K3:1:1:w]');
    assert.equal(f.game.get_current_timeline_status().mandatory_timelines.length,2);
    const first = move(0,1,1,0,0,2);
    const partial = f.send('inspect',{moves:[first],source:null});
    assert(partial.ok,partial.error); assert.equal(partial.complete,false);
    assert.equal(f.send('propose',{moves:[first]}).ok,false);
    const full = f.send('propose',{moves:[first,move(0,1,1,1,0,2)]});
    assert(full.ok,full.error); assert.equal(f.game.get_historical_actions().length,0);
    assert(f.send('commit',{proposalId:full.state.proposal.id}).ok);
    assert.equal(f.game.get_historical_actions().length,1);
    assert.equal(f.game.get_historical_actions()[0].action.length,2);
    f.close();
    console.log('PASS real WASM: two required boards, incomplete prefix rejection, complete two-move turn.');
}
{
    const f = fixture('[Timeline "odd"] [Size "8x8"] [7k/P7/8/8/8/8/8/K7:0:1:w]');
    const staged = f.send('propose',{moves:[move(0,6,1,0,0,7,1,0,81)]});
    assert(staged.ok,staged.error);
    assert(f.send('commit',{proposalId:staged.state.proposal.id}).ok);
    assert(f.game.get_current_boards().some(b=>b.c===true&&b.fen.startsWith('Q6k')));
    f.close();
    console.log('PASS real WASM: explicit promotion.');
}
{
    const f = fixture('[Variant "Standard - Turn Zero"] 1. Nf3 / Nf6 2. Nc3 / Nc6');
    const initial = f.game.get_current_boards();
    const travel = move(5,2,3,0,5,4,2,0);
    const peek = f.send('inspect',{moves:[travel],source:null});
    assert(peek.ok,peek.error);
    assert(peek.position.boards.length>initial.length);
    assert.deepEqual(f.game.get_current_boards(),initial);
    const staged = f.send('propose',{moves:[travel]});
    assert(staged.ok,staged.error);
    assert(f.send('commit',{proposalId:staged.state.proposal.id}).ok);
    assert(f.game.get_current_boards().some(b=>b.l!==0));
    f.close();
    console.log('PASS real WASM: branching into history and restoring an inspected branch.');
}
{
    const f = fixture('[Board "Standard - Turn Zero"] 1. f3 / e5 2. g4 / Qh4');
    assert.equal(f.send('state').state.status,'Black Wins');
    assert.equal(f.send('propose',{moves:[move(0,1,3,0,0,2)]}).ok,false);
    f.close();
    console.log('PASS real WASM: terminal result prevents further proposals.');
}
