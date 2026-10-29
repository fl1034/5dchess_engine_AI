import { RULES, REPLY_SCHEMA } from '../../ui/scripts/ai-rules.mjs';
import { findModel } from '../../ui/scripts/ai-models.mjs';

const limit = (value, fallback) => /^\d+$/.test(String(value)) ? Math.max(1, Math.min(10000, Number(value))) : fallback;
const allowedModels = env => (env.ALLOWED_MODELS || 'gpt-6-luna').split(',').map(s => s.trim()).filter(findModel);
const json = (body, status = 200, origin) => Response.json(body, {status, headers:{
    'Access-Control-Allow-Origin':origin || 'null', 'Vary':'Origin',
    'Access-Control-Allow-Methods':'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers':'Content-Type', 'Cache-Control':'no-store',
}});

async function readBody(request) {
    const reader = request.body?.getReader();
    if (!reader) throw Error('Missing request body.');
    const chunks = []; let size = 0;
    try {
        while (true) {
            const {done,value} = await reader.read();
            if (done) break;
            size += value.byteLength;
            if (size > 32768) { await reader.cancel(); throw Error('Request too large.'); }
            chunks.push(value);
        }
    } finally { reader.releaseLock(); }
    const bytes = new Uint8Array(size); let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk,offset); offset += chunk.length; }
    return JSON.parse(new TextDecoder().decode(bytes));
}

export async function handleRequest(request, env, upstream = fetch) {
    const origin = request.headers.get('Origin');
    if (origin !== env.ALLOWED_ORIGIN) return json({error:{code:'origin_denied'}},403);
    const respond = (body,status) => json(body,status,origin);
    if (request.method === 'OPTIONS') return respond({ok:true},200);
    const path = new URL(request.url).pathname;
    if (request.method === 'GET' && path === '/models') {
        return respond({data:allowedModels(env).map(id=>({id})),
            configured:Boolean(env.OPENAI_API_KEY && env.BUDGET),
            configuration:{keyPresent:Boolean(env.OPENAI_API_KEY),quotaBindingPresent:Boolean(env.BUDGET)}},200);
    }
    if (request.method !== 'POST' || path !== '/v1/chat/completions') {
        return respond({error:{code:'not_found'}},404);
    }
    if (!env.OPENAI_API_KEY || !env.BUDGET) return respond({error:{code:'demo_not_configured'}},503);
    let body, messages, side;
    try {
        body = await readBody(request);
        if (!allowedModels(env).includes(body.model)) throw Error('Unsupported demo model.');
        if (!Array.isArray(body.messages) || body.messages.length > 6) throw Error('Invalid messages.');
        messages = body.messages.filter(m=>m.role!=='system').slice(-5);
        if (!messages.length || messages.at(-1).role !== 'user'
            || messages.some(m=>!['user','assistant'].includes(m.role)||typeof m.content!=='string'||m.content.length>20000)) {
            throw Error('Invalid message format.');
        }
        const state = JSON.parse(messages.at(-1).content).state;
        side = state.side;
        if (!['White','Black'].includes(side) || !Array.isArray(state.boards)
            || state.boards.length > 128 || !Array.isArray(state.timelines?.mandatory_timelines)) {
            throw Error('A structured chess state is required.');
        }
    } catch { return respond({error:{code:'invalid_demo_request'}},400); }

    try {
        const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
        const hash = await crypto.subtle.digest('SHA-256',new TextEncoder().encode(ip));
        const client = Array.from(new Uint8Array(hash),b=>b.toString(16).padStart(2,'0')).join('');
        const budget = env.BUDGET.get(env.BUDGET.idFromName('portfolio-global-budget'));
        const reservation = await budget.fetch('https://budget/reserve',{method:'POST',body:JSON.stringify({client})});
        if (!reservation.ok) return respond({error:{code:'demo_limit_reached'}},429);
    } catch { return respond({error:{code:'demo_budget_unavailable'}},503); }

    // Client system prompts, keys, limits, URLs and schemas never override these.
    const payload = {
        model:body.model, store:false, reasoning_effort:'low', max_completion_tokens:2400,
        messages:[{role:'system',content:RULES+'\nYou play '+side+'.'},...messages],
        response_format:{type:'json_schema',json_schema:{name:'chess_turn',strict:true,schema:REPLY_SCHEMA}},
    };
    try {
        const result = await upstream('https://api.openai.com/v1/chat/completions',{
            method:'POST',headers:{Authorization:'Bearer '+env.OPENAI_API_KEY,'Content-Type':'application/json'},
            body:JSON.stringify(payload),signal:AbortSignal.any([request.signal,AbortSignal.timeout(85000)]),
        });
        if (!result.ok) return respond({error:{code:'demo_upstream_unavailable'}},503);
        const data = await result.json();
        return respond({choices:data.choices,usage:data.usage},200);
    } catch { return respond({error:{code:'demo_upstream_unavailable'}},503); }
}

export default {fetch:(request,env)=>handleRequest(request,env)};

// One persistent object makes the quota global, including across cold starts.
// SQLite-backed Durable Objects are supported on Cloudflare's free plan.
export class DemoBudget {
    constructor(state, env) { this.storage = state.storage; this.env = env; }
    async fetch(request) {
        const {client} = await request.json();
        if (typeof client!=='string' || client.length>64) return Response.json({ok:false},{status:400});
        const now = Date.now(), day = new Date(now).toISOString().slice(0,10), minute = Math.floor(now/60000);
        const ok = await this.storage.transaction(async transaction=>{
            let record = await transaction.get('quota');
            if (!record || record.day!==day) record = {day,total:0,clients:{}};
            const previous = record.clients[client] || {total:0,minute,count:0};
            if (previous.minute!==minute) {previous.minute=minute;previous.count=0;}
            if (record.total>=limit(this.env.DAILY_REQUESTS,100)
                || previous.total>=limit(this.env.DAILY_REQUESTS_PER_VISITOR,20)
                || previous.count>=limit(this.env.REQUESTS_PER_MINUTE,6)) return false;
            previous.total++;previous.count++;record.total++;record.clients[client]=previous;
            await transaction.put('quota',record);
            return true;
        });
        return Response.json({ok},{status:ok?200:429});
    }
}
