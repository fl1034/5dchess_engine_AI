import { RULES, REPLY_SCHEMA } from './ai-rules.mjs';

export class PlayerError extends Error {
    constructor(message, recoverable = false) { super(message); this.recoverable = recoverable; }
}

export function normalizeApiKey(value) {
    if (typeof value !== 'string' || !value.trim()) throw Error('Supply a nonempty API key.');
    const key = value.trim();
    if (/[^\x21-\x7e]/.test(key)) throw Error('The API key contains whitespace or unsupported characters. Paste the key on one line without surrounding quotes.');
    return key;
}

export function retryDelay(value, attempt, now = Date.now(), random = Math.random) {
    if (value) {
        const seconds = Number(value);
        const ms = Number.isFinite(seconds) ? seconds * 1000 : Date.parse(value) - now;
        if (Number.isFinite(ms) && ms >= 0) return ms;
    }
    return Math.min(30000, 1000 * 2 ** attempt) * (0.75 + random() * 0.5);
}

export function abortableWait(ms, signal) {
    return new Promise((resolve, reject) => {
        const cancel = () => { clearTimeout(timer); reject(new DOMException('Stopped', 'AbortError')); };
        const timer = setTimeout(() => { signal?.removeEventListener('abort', cancel); resolve(); }, ms);
        if (signal?.aborted) cancel();
        else signal?.addEventListener('abort', cancel, {once:true});
    });
}

export class OpenAIPlayer {
    constructor({ key, side, model = 'gpt-5-mini', demoUrl = null, fetcher = globalThis.fetch.bind(globalThis),
        reserve = () => {}, usage = () => {}, waiting = () => {}, wait = abortableWait }) {
        Object.assign(this, {key, side, model, demoUrl, fetcher, reserve, usage, waiting, wait});
        this.history = [];
    }
    async ask(state, feedback, signal) {
        const user = JSON.stringify({state, feedback: feedback ?? null});
        if (user.length > 64000) throw new PlayerError('Position is too large for the configured context safeguard. Match paused.');
        const messages = [
            {role:'system',content:RULES + '\nYou play ' + this.side + '. Your opponent has an independent conversation.'},
            ...this.history.slice(-4),
            {role:'user',content:user},
        ];
        const body = JSON.stringify({
            model:this.model, messages, reasoning_effort:'low', max_completion_tokens:2400,
            store:false,
            response_format:{type:'json_schema',json_schema:{name:'chess_turn',strict:true,schema:REPLY_SCHEMA}},
        });
        for (let attempt = 0; attempt < 4; attempt++) {
            if (signal.aborted) throw new DOMException('Stopped', 'AbortError');
            // Count every HTTP attempt, including errors and ambiguous transport failures.
            this.reserve();
            let response;
            try {
                response = await this.fetcher(this.demoUrl || 'https://api.openai.com/v1/chat/completions', {
                    method:'POST', headers:this.demoUrl ? {'Content-Type':'application/json'}
                        : {'Content-Type':'application/json',Authorization:'Bearer ' + this.key},
                    body, signal:AbortSignal.any([signal, AbortSignal.timeout(90000)]),
                });
            } catch (error) {
                if (signal.aborted) throw new DOMException('Stopped', 'AbortError');
                if (attempt === 3) {
                    const timedOut = error.name === 'TimeoutError' || error.name === 'AbortError';
                    throw new PlayerError(timedOut
                        ? 'OpenAI did not respond within 90 seconds. The match is paused on the same turn; resume to retry. The earlier request may still complete.'
                        : 'This browser could not connect to ' + (this.demoUrl ? 'the public demo service' : 'api.openai.com')
                            + '. Check this workstation’s network, proxy, firewall, or browser extensions, then resume. This connection failure does not establish whether the key is valid.');
                }
                this.waiting('Connection interrupted; retrying this same turn.');
                await this.wait(retryDelay(null, attempt), signal);
                continue;
            }
            if (response.ok) {
                let data;
                try { data = await response.json(); }
                catch { throw new PlayerError('Malformed API response. Return the required JSON object.', true); }
                this.usage(data.usage ?? {});
                const choice = data.choices?.[0];
                if (choice?.message?.refusal) throw new PlayerError('The model declined this request. Match paused.');
                if (choice?.finish_reason !== 'stop') throw new PlayerError('Response did not finish within the output limit. Give a shorter legal proposal or inspection.', true);
                let reply;
                try { reply = JSON.parse(choice.message.content); }
                catch { throw new PlayerError('Response was not valid JSON. Return the required JSON object.', true); }
                // Private messages remain in this player's object; keys are never messages.
                this.history.push({role:'user',content:JSON.stringify({side:state.side,revision:state.revision,feedback})},
                    {role:'assistant',content:JSON.stringify(reply)});
                this.history = this.history.slice(-4);
                return reply;
            }
            // Never expose raw service bodies (or request headers) in logs/UI.
            let code;
            try { code = (await response.json()).error?.code; } catch {}
            if (code === 'demo_limit_reached') throw new PlayerError('The shared demo request limit was reached. Try later, or enter Analysis mode and start a new match with your own key.');
            if (['demo_not_configured','demo_budget_unavailable','demo_upstream_unavailable'].includes(code)) {
                throw new PlayerError('The public demo service is unavailable or needs its owner to finish API configuration. Your turn is paused.');
            }
            if (code === 'invalid_demo_request') throw new PlayerError('This position or model exceeds the public demo limits. You can use your own key for a new match.');
            if (this.demoUrl && [401,403].includes(response.status)) throw new PlayerError('Public demo access was denied. Its owner may need to update the allowed website configuration.');
            if ([401,403].includes(response.status)) throw new PlayerError('API authentication or access denied (HTTP ' + response.status + '). Check the key and model access, then resume.');
            if (['insufficient_quota','billing_hard_limit_reached','billing_not_active'].includes(code)) {
                throw new PlayerError('OpenAI billing or quota is unavailable. Check API billing, then resume.');
            }
            if (response.status === 429 || [408,409,500,502,503,504].includes(response.status)) {
                if (attempt === 3) throw new PlayerError('Temporary API errors persisted (HTTP ' + response.status + '). Resume to retry the same turn.');
                const delay = retryDelay(response.headers.get('retry-after'), attempt);
                this.waiting('API busy (HTTP ' + response.status + '); retry in ' + Math.ceil(delay / 1000) + 's.');
                await this.wait(delay, signal);
                continue;
            }
            throw new PlayerError('API request failed (HTTP ' + response.status + '). Check model access and request configuration.');
        }
    }
    forget() { this.key = ''; this.history = []; }
}
