import test from 'node:test';
import assert from 'node:assert/strict';
import {handleRequest,DemoBudget} from './index.mjs';
import {RULES} from '../../ui/scripts/ai-rules.mjs';

const origin='https://fl1034.github.io';
function fixture() {
    let stored;
    const storage={
        transaction:async callback=>callback(storage),
        get:async()=>structuredClone(stored),
        put:async(key,value)=>{stored=structuredClone(value);},
    };
    const env={ALLOWED_ORIGIN:origin,OPENAI_API_KEY:'owner-test-secret',
        DAILY_REQUESTS:'3',DAILY_REQUESTS_PER_VISITOR:'2',REQUESTS_PER_MINUTE:'2'};
    const budget=new DemoBudget({storage},env);
    env.BUDGET={idFromName:name=>name,get:()=>({
        fetch:(url,options)=>budget.fetch(new Request(url,options)),
    })};
    const body={model:'gpt-6-luna',messages:[
        {role:'system',content:'untrusted client system prompt'},
        {role:'user',content:JSON.stringify({state:{side:'White',boards:[{}],timelines:{mandatory_timelines:[0]}}})},
    ],max_completion_tokens:999999,store:true};
    const request=(options={})=>new Request('https://demo.example/v1/chat/completions',{
        method:'POST',headers:{Origin:origin,'Content-Type':'application/json','CF-Connecting-IP':'192.0.2.1',...options.headers},
        body:JSON.stringify({...body,...options.body}),
    });
    return {env,body,request};
}

test('owner key stays server-side and clients cannot override rules, schema or limits',async()=>{
    const f=fixture();let calls=0;
    const result=await handleRequest(f.request(),f.env,async(url,options)=>{
        calls++;
        assert.equal(url,'https://api.openai.com/v1/chat/completions');
        assert.equal(options.headers.Authorization,'Bearer owner-test-secret');
        const sent=JSON.parse(options.body);
        assert(sent.messages[0].content.startsWith(RULES));
        assert(!sent.messages[0].content.includes('untrusted client'));
        assert.equal(sent.max_completion_tokens,2400);
        assert.equal(sent.store,false);
        assert.equal(sent.response_format.json_schema.strict,true);
        return Response.json({choices:[{message:{content:'test'}}],usage:{total_tokens:10}});
    });
    assert.equal(result.status,200);
    assert.equal(result.headers.get('Access-Control-Allow-Origin'),origin);
    assert(!(await result.text()).includes('owner-test-secret'));
    assert.equal(calls,1);
});

test('origin, model, malformed state, body size and missing secret are checked before spending',async()=>{
    const f=fixture();const upstream=()=>{throw Error('Must not call upstream');};
    assert.equal((await handleRequest(f.request({headers:{Origin:'https://other.example'}}),f.env,upstream)).status,403);
    assert.equal((await handleRequest(f.request({body:{model:'gpt-6-astra'}}),f.env,upstream)).status,400);
    assert.equal((await handleRequest(f.request({body:{messages:[]}}),f.env,upstream)).status,400);
    assert.equal((await handleRequest(f.request({body:{extra:'x'.repeat(40000)}}),f.env,upstream)).status,400);
    assert.equal((await handleRequest(f.request(),{...f.env,OPENAI_API_KEY:''},upstream)).status,503);
});

test('persistent per-visitor and global quotas reject calls before OpenAI',async()=>{
    const f=fixture();let calls=0;
    const upstream=async()=>{calls++;return Response.json({choices:[],usage:{}});};
    assert.equal((await handleRequest(f.request(),f.env,upstream)).status,200);
    assert.equal((await handleRequest(f.request(),f.env,upstream)).status,200);
    const limited=await handleRequest(f.request(),f.env,upstream);
    assert.equal(limited.status,429);
    assert.equal((await limited.json()).error.code,'demo_limit_reached');
    assert.equal((await handleRequest(f.request({headers:{'CF-Connecting-IP':'192.0.2.2'}}),f.env,upstream)).status,200);
    assert.equal((await handleRequest(f.request({headers:{'CF-Connecting-IP':'192.0.2.3'}}),f.env,upstream)).status,429);
    assert.equal(calls,3);
});

test('upstream failures never echo credential-bearing error bodies',async()=>{
    const f=fixture();
    const result=await handleRequest(f.request(),f.env,async()=>Response.json({
        error:{message:'do not return owner-test-secret'}},{status:401}));
    assert.equal(result.status,503);
    assert(!(await result.text()).includes('owner-test-secret'));
});
