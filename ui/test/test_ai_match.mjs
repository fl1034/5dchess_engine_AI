import test from 'node:test';
import assert from 'node:assert/strict';
import { MatchController } from '../scripts/ai-match.mjs';
import { OpenAIPlayer, retryDelay } from '../scripts/ai-player.mjs';
import { checkedMoves } from '../scripts/ai-engine.mjs';
import { RULES } from '../scripts/ai-rules.mjs';
import { usageCost } from '../scripts/ai-models.mjs';

const models = {whiteModel:'gpt-6-luna',blackModel:'gpt-6.1-terra'};

const move = {from:{x:4,y:1,t:1,l:0},to:{x:4,y:3,t:1,l:0},promote_to:0};
const summary = state => ({side:state.side,boardCount:state.boards.length,
    mandatoryTimelines:state.timelines.mandatory_timelines});
const reply = (state, extra = {}) => ({kind:'propose',moves:[move],source:null,
    stateSummary:summary(state),explanation:'Advance a central pawn.',...extra});

function fixture(factory) {
    const operations = [], requests = [];
    let revision = 0, epoch = 0, commits = 0, side = 'White', proposal = null, paused = true;
    const snapshot = () => ({matchId:'match',revision,epoch,commits,side,proposal,paused,
        status:side + "'s Move",boards:[{},{}],timelines:{mandatory_timelines:[0]}});
    let rejectNext = false, loseCommit = false;
    const rpc = async message => {
        operations.push(structuredClone(message));
        if (message.op === 'start') { epoch++; revision++; paused = false; }
        if (message.op === 'propose') {
            if (rejectNext) { rejectNext = false; return {ok:false,error:'Invalid second move; position restored.'}; }
            proposal = {id:'proposal',moves:message.moves}; revision++;
        }
        if (message.op === 'commit') {
            commits++; proposal = null; revision++; side = side === 'White' ? 'Black' : 'White';
            if (loseCommit) { loseCommit = false; throw Error('Lost acknowledgement'); }
        }
        if (['pause','resume','stop'].includes(message.op)) {
            epoch++; revision++; proposal = null; paused = message.op !== 'resume';
        }
        return {ok:true,state:snapshot(),position:snapshot(),destinations:[move.to],complete:false};
    };
    const controller = new MatchController({rpc, playerFactory:config => ({
        ask:async (state, feedback, signal) => {
            requests.push({side:config.side,model:config.model,demoUrl:config.demoUrl,state:structuredClone(state),feedback});
            config.reserve(); config.usage({prompt_tokens:100,completion_tokens:20});
            return factory ? factory(state,feedback,signal,requests.length) : reply(state);
        },forget() {},
    })});
    return {controller,operations,requests,
        reject:() => {rejectNext = true;},loseCommit:() => {loseCommit = true;}};
}

test('valid complete turns wait for OK, then only the opposite context starts', async () => {
    const {controller:c,requests,operations} = fixture();
    await c.start({...models,key:'test',limit:10});
    assert.equal(c.phase,'proposal');
    assert.deepEqual(requests.map(r=>r.side),['White']);
    assert.equal(operations.filter(o=>o.op==='commit').length,0);
    await Promise.all([c.ok(),c.ok()]);
    assert.deepEqual(requests.map(r=>r.side),['White','Black']);
    assert.equal(operations.filter(o=>o.op==='commit').length,1);
    assert.equal(c.state.commits,1);
    assert.equal(c.phase,'proposal');
    assert.equal(c.usage.requests,2);
    assert.deepEqual(requests.map(r=>r.model),['gpt-6-luna','gpt-6.1-terra']);
    assert(Math.abs(c.usage.cost - 0.00022) < 1e-12);
});

test('both explicit model selections are required before any game reset or API call', async () => {
    const f = fixture();
    for (const choices of [{},{whiteModel:models.whiteModel},{blackModel:models.blackModel},
        {...models,whiteModel:''},{...models,blackModel:'unsupported-model'}]) {
        await assert.rejects(f.controller.start({key:'test',...choices}),/both White and Black/);
        assert.equal(f.controller.phase,'idle');
        assert.equal(f.operations.length,0);
        assert.equal(f.requests.length,0);
    }
    await f.controller.start({key:'test',whiteModel:'gpt-6.1-sol',blackModel:'gpt-6.1-sol'});
    await f.controller.ok();
    assert.deepEqual(f.requests.map(r=>r.model),['gpt-6.1-sol','gpt-6.1-sol']);
});

test('usage estimates use the selected model and its cached-input price', () => {
    const u = {prompt_tokens:1000,completion_tokens:200,prompt_tokens_details:{cached_tokens:500}};
    assert.equal(usageCost('gpt-6-luna',u),0.000155);
    assert.equal(usageCost('gpt-6.1-terra',u),0.001525);
    assert.equal(usageCost('gpt-6.1-sol',u),0.00305);
    assert.equal(usageCost('gpt-5-mini',u),0.0005375);
});

test('invalid moves and wrong state interpretation retry the same player', async () => {
    const f = fixture((state,feedback,signal,n) => reply(state,n === 1
        ? {stateSummary:{...summary(state),boardCount:999}} : {}));
    f.reject();
    await f.controller.start({...models,key:'test',limit:10});
    assert.equal(f.controller.phase,'proposal');
    assert.equal(f.requests.length,3);
    assert(f.requests.every(r=>r.side==='White'));
    assert.match(f.requests[1].feedback.message,/interpretation/);
    assert.match(f.requests[2].feedback.message,/second move/);
    assert.equal(f.controller.state.commits,0);
});

test('inspection uses a hypothetical prefix without committing or switching sides', async () => {
    const f = fixture((state,feedback,signal,n) => reply(state,n === 1
        ? {kind:'inspect',moves:[],source:move.from} : {}));
    await f.controller.start({...models,key:'test'});
    assert.equal(f.operations.filter(o=>o.op==='inspect').length,1);
    assert.equal(f.requests[1].feedback.complete,false);
    assert.equal(f.controller.state.commits,0);
    assert.equal(f.controller.phase,'proposal');
});

test('pause discards previews; late responses cannot stage moves; resume keeps the side', async () => {
    let complete;
    const f = fixture((state,feedback,signal,n) => n === 1
        ? new Promise(resolve => { complete = () => resolve(reply(state)); }) : reply(state));
    const starting = f.controller.start({...models,key:'test'});
    while (!complete) await new Promise(resolve=>setImmediate(resolve));
    await f.controller.pause();
    complete(); await starting;
    assert.equal(f.controller.phase,'paused');
    assert.equal(f.operations.filter(o=>o.op==='propose').length,0);
    await f.controller.resume();
    assert.equal(f.controller.phase,'proposal');
    assert(f.requests.every(r=>r.side==='White'));
});

test('ambiguous commit acknowledgement reconciles without replaying the accepted turn', async () => {
    const f = fixture(); await f.controller.start({...models,key:'test'});
    f.loseCommit(); await f.controller.ok();
    assert.equal(f.operations.filter(o=>o.op==='commit').length,1);
    assert.equal(f.controller.state.commits,1);
    assert.deepEqual(f.requests.map(r=>r.side),['White','Black']);
});

test('request safeguard pauses before spending, and resumes the same owner with a raised limit', async () => {
    const f = fixture();
    await f.controller.start({...models,key:'test',limit:1});
    await f.controller.ok();
    assert.equal(f.controller.phase,'paused');
    assert.equal(f.controller.state.side,'Black');
    assert.equal(f.controller.state.commits,1);
    assert.equal(f.controller.usage.requests,1);
    await f.controller.resume(2);
    assert.equal(f.controller.phase,'proposal');
    assert.equal(f.controller.state.side,'Black');
});

test('generation includes verified rules, structured public state and independent histories', async () => {
    const bodies = [];
    const fetcher = async (url, options) => {
        bodies.push(JSON.parse(options.body));
        return {ok:true,json:async()=>({usage:{prompt_tokens:10,completion_tokens:5},
            choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply({side:'White',boards:[],timelines:{mandatory_timelines:[]}}))}}]})};
    };
    const state = {side:'White',boards:[],timelines:{mandatory_timelines:[]},revision:1};
    const white = new OpenAIPlayer({key:'never-in-messages',side:'White',fetcher});
    const black = new OpenAIPlayer({key:'never-in-messages',side:'Black',fetcher});
    await white.ask(state,null,new AbortController().signal);
    await white.ask(state,{message:'private white feedback'},new AbortController().signal);
    await black.ask({...state,side:'Black'},null,new AbortController().signal);
    assert.equal(bodies[0].messages[0].content.startsWith(RULES),true);
    assert.equal(bodies[0].response_format.json_schema.strict,true);
    assert.equal(bodies[0].store,false);
    assert.equal(JSON.stringify(bodies).includes('never-in-messages'),false);
    assert.equal(JSON.stringify(bodies[2]).includes('private white feedback'),false);
    white.forget(); assert.equal(white.key,'');
});

test('transient errors respect Retry-After and quota/auth failures stop without loops', async () => {
    const state = {side:'White',boards:[],timelines:{mandatory_timelines:[]}};
    const waits = []; let calls = 0;
    const p = new OpenAIPlayer({key:'test',side:'White',wait:async ms=>waits.push(ms),
        fetcher:async()=>{
            calls++;
            if(calls===1) return {ok:false,status:429,headers:new Headers({'retry-after':'2'}),json:async()=>({error:{code:'rate_limit_exceeded'}})};
            return {ok:true,json:async()=>({choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply(state))}}]})};
        }});
    await p.ask(state,null,new AbortController().signal);
    assert.equal(calls,2); assert.deepEqual(waits,[2000]);
    assert.equal(retryDelay('Thu, 01 Jan 1970 00:00:05 GMT',0,1000),4000);
    for(const [status,code] of [[401,'invalid_api_key'],[429,'insufficient_quota']]) {
        calls = 0;
        p.fetcher = async()=>{calls++;return {ok:false,status,json:async()=>({error:{code}})};};
        await assert.rejects(p.ask(state,null,new AbortController().signal));
        assert.equal(calls,1);
    }
});

test('malformed/truncated/refused outputs and coordinate overflows cannot become moves', async () => {
    const p = new OpenAIPlayer({key:'test',side:'White',fetcher:async()=>({ok:true,json:async()=>({
        choices:[{finish_reason:'stop',message:{content:'not JSON'}}]})})});
    await assert.rejects(p.ask({},null,new AbortController().signal),error=>error.recoverable);
    p.fetcher = async()=>({ok:true,json:async()=>({choices:[{finish_reason:'length',message:{content:'{}'}}]})});
    await assert.rejects(p.ask({},null,new AbortController().signal),error=>error.recoverable);
    p.fetcher = async()=>({ok:true,json:async()=>({choices:[{finish_reason:'stop',message:{refusal:'declined'}}]})});
    await assert.rejects(p.ask({},null,new AbortController().signal),error=>!error.recoverable);
    assert.throws(()=>checkedMoves([{...move,to:{...move.to,x:8}}]));
    assert.throws(()=>checkedMoves([{...move,to:{...move.to,t:-1}}]));
    assert.throws(()=>checkedMoves([{...move,promote_to:999}]));
});

test('engine terminal status ends scheduling without any model request', async () => {
    let calls = 0;
    const c = new MatchController({rpc:async()=>({ok:true,state:{matchId:'terminal',
        epoch:1,revision:1,side:'White',status:'Black Wins',commits:0}}),
        playerFactory:()=>({ask:async()=>{calls++;},forget(){}})});
    await c.start({...models,key:'test'});
    assert.equal(c.phase,'finished');
    assert.equal(calls,0);
});

test('replacement credentials apply to both players without changing the paused position', async () => {
    const f = fixture();
    await f.controller.start({...models,key:'old-test-key'});
    assert.throws(()=>f.controller.replaceKey('new-test-key'),/Pause/);
    await f.controller.pause();
    const position = structuredClone(f.controller.state);
    f.controller.replaceKey('new-test-key');
    assert.equal(f.controller.players.White.key,'new-test-key');
    assert.equal(f.controller.players.Black.key,'new-test-key');
    assert.deepEqual(f.controller.state,position);
    assert.throws(()=>f.controller.replaceKey('bad key'),/whitespace/);
    await f.controller.resume();
    assert.equal(f.controller.state.side,'White');
    assert.equal(f.controller.state.commits,0);
});

test('browser connection failures and response timeouts have distinct diagnoses', async () => {
    for (const [name,pattern] of [['TimeoutError',/within 90 seconds/],['TypeError',/could not connect to api.openai.com/]]) {
        let calls = 0;
        const p = new OpenAIPlayer({key:'test',side:'White',wait:async()=>{},
            fetcher:async()=>{calls++;throw new DOMException('test transport failure',name);}});
        await assert.rejects(p.ask({},null,new AbortController().signal),error=>pattern.test(error.message));
        assert.equal(calls,4);
    }
});

test('malformed key input is rejected before requests instead of appearing as a network failure', async () => {
    const f = fixture();
    for (const key of ['bad key','bad\nkey','bad“key','']) {
        await assert.rejects(f.controller.start({...models,key}),/API key/);
        assert.equal(f.requests.length,0);
        assert.equal(f.operations.length,0);
    }
});

test('public matches need both models but no visitor credential', async () => {
    const f = fixture();
    await f.controller.start({...models,backendUrl:'https://demo.example'});
    assert.equal(f.controller.phase,'proposal');
    assert.equal(f.requests[0].demoUrl,'https://demo.example/v1/chat/completions');
    await f.controller.pause();
    assert.throws(()=>f.controller.replaceKey('visitor-key'),/server-side key/);
    await f.controller.resume();
    assert.equal(f.controller.state.side,'White');
});

test('public transport never sends a visitor Authorization header', async () => {
    const state={side:'White',boards:[],timelines:{mandatory_timelines:[]}};
    const p=new OpenAIPlayer({key:'must-never-be-forwarded',side:'White',demoUrl:'https://demo.example/v1/chat/completions',
        fetcher:async(url,options)=>{
            assert.equal(url,'https://demo.example/v1/chat/completions');
            assert.equal(new Headers(options.headers).get('Authorization'),null);
            assert(!options.body.includes('must-never-be-forwarded'));
            return Response.json({choices:[{finish_reason:'stop',message:{content:JSON.stringify(reply(state))}}]});
        }});
    await p.ask(state,null,new AbortController().signal);
});

test('public budget and owner-configuration failures pause without retry loops', async () => {
    for(const [status,code,pattern] of [[429,'demo_limit_reached',/shared demo/],[503,'demo_not_configured',/owner/]]){
        let calls=0;
        const p=new OpenAIPlayer({side:'White',demoUrl:'https://demo.example/v1/chat/completions',
            fetcher:async()=>{calls++;return Response.json({error:{code}},{status});}});
        await assert.rejects(p.ask({},null,new AbortController().signal),pattern);
        assert.equal(calls,1);
    }
});
