import { OpenAIPlayer, normalizeApiKey } from './ai-player.mjs';
import { checkedMoves } from './ai-engine.mjs';
import { findModel, usageCost } from './ai-models.mjs';

const live = s => s.status === "White's Move" || s.status === "Black's Move";
export class MatchController {
    constructor({rpc, render = () => {}, playerFactory = config => new OpenAIPlayer(config)}) {
        Object.assign(this, {rpc, render, playerFactory});
        this.phase = 'idle';
        this.state = null;
        this.run = 0;
        this.players = {};
        this.usage = {requests:0,input:0,output:0,cached:0,cost:0};
        this.models = {};
        this.demoUrl = null;
        this.limit = 40;
        this.turnLimit = 8;
    }
    emit(message = this.message) {
        this.message = message;
        this.render({phase:this.phase,state:this.state,message,usage:{...this.usage},models:{...this.models},demoMode:Boolean(this.demoUrl),limit:this.limit,
            explanation:this.explanation ?? '', feedback:this.feedback ?? null});
    }
    async boot() {
        const result = await this.rpc({op:'boot'});
        if (!result.ok) throw Error(result.error);
        this.state = result.state;
        this.emit('Choose a model for White and Black, then load your key to start. Each complete turn waits for OK.');
    }
    envelope(op, extra = {}) {
        return {op, matchId:this.state?.matchId,epoch:this.state?.epoch,revision:this.state?.revision,...extra};
    }
    async command(op, extra = {}) {
        const result = await this.rpc(this.envelope(op, extra));
        if (!result.ok) throw Error(result.error);
        this.state = result.state;
        return result;
    }
    cancel() { this.run++; this.abort?.abort(); }
    async start({key, whiteModel, blackModel, backendUrl = null, limit = 40}) {
        if (!findModel(whiteModel) || !findModel(blackModel)) throw Error('Choose a supported model for both White and Black before starting.');
        let demoUrl = null;
        if (backendUrl) {
            const url = new URL(backendUrl);
            if (url.protocol !== 'https:') throw Error('The public demo backend must use HTTPS.');
            demoUrl = new URL('/v1/chat/completions',url).href;
            key = '';
        } else key = normalizeApiKey(key);
        if (!Number.isInteger(limit) || limit < 1 || limit > 200) throw Error('Supply a request limit from 1 to 200.');
        this.cancel();
        const run = this.run;
        for (const player of Object.values(this.players)) player.forget?.();
        this.players = {};
        this.usage = {requests:0,input:0,output:0,cached:0,cost:0};
        this.models = {White:whiteModel,Black:blackModel};
        this.demoUrl = demoUrl;
        this.limit = limit;
        this.feedback = null;
        this.explanation = '';
        this.phase = 'starting';
        this.emit('Starting a standard turn-zero match.');
        const result = await this.rpc({op:'start',matchId:crypto.randomUUID()});
        if (run !== this.run) return;
        if (!result.ok) throw Error(result.error);
        this.state = result.state;
        for (const side of ['White','Black']) {
            this.players[side] = this.playerFactory({
                key,side,model:this.models[side],demoUrl:this.demoUrl,
                reserve:() => {
                    if (this.usage.requests >= this.limit) throw Error('Request safeguard reached. Increase the limit and resume; the same player keeps the turn.');
                    this.usage.requests++; this.emit();
                },
                usage:u => {
                    this.usage.input += u.prompt_tokens ?? 0;
                    this.usage.output += u.completion_tokens ?? 0;
                    this.usage.cached += u.prompt_tokens_details?.cached_tokens ?? 0;
                    this.usage.cost += usageCost(this.models[side],u);
                    this.emit();
                },
                waiting:message => this.emit(message),
            });
        }
        await this.think(run);
    }
    async think(run = this.run) {
        this.phase = 'thinking';
        this.abort = new AbortController();
        this.explanation = '';
        try {
            // Always reconcile committed state before spending or choosing a player.
            await this.command('state');
            if (run !== this.run) return;
            if (!live(this.state)) { this.phase = 'finished'; this.emit(this.state.status); return; }
            const side = this.state.side;
            for (let attempt = 0; attempt < this.turnLimit; attempt++) {
                if (run !== this.run) return;
                this.emit(side + ' is planning a complete turn' + (attempt ? ' (step ' + (attempt + 1) + ')' : '') + '.');
                const publicState = structuredClone(this.state);
                let reply;
                try {
                    reply = await this.players[side].ask(publicState, this.feedback, this.abort.signal);
                    if (run !== this.run) return;
                    if (!reply || !['inspect','propose'].includes(reply.kind)) throw Error('Use kind inspect or propose.');
                    const summary = reply.stateSummary;
                    if (summary?.side !== publicState.side || summary.boardCount !== publicState.boards.length
                        || JSON.stringify(summary.mandatoryTimelines) !== JSON.stringify(publicState.timelines.mandatory_timelines)) {
                        throw Error('State interpretation is incorrect. Re-read side, boards.length and mandatory_timelines in the supplied public state.');
                    }
                    checkedMoves(reply.moves);
                    if (reply.kind === 'inspect') {
                        const result = await this.rpc(this.envelope('inspect', {moves:reply.moves,source:reply.source}));
                        if (run !== this.run) return;
                        if (!result.ok) throw Error(result.error);
                        this.state = result.state;
                        this.feedback = {inspection:result.position,destinations:result.destinations,complete:result.complete};
                    } else {
                        if (reply.source !== null) throw Error('A proposal requires source=null.');
                        const result = await this.rpc(this.envelope('propose', {moves:reply.moves}));
                        if (run !== this.run) return;
                        if (!result.ok) throw Error(result.error);
                        this.state = result.state;
                        this.phase = 'proposal';
                        this.explanation = typeof reply.explanation === 'string' ? reply.explanation.slice(0,600) : '';
                        this.feedback = null;
                        this.emit(side + ' proposed a legal complete turn. Review the board, then click OK.');
                        return;
                    }
                } catch (error) {
                    if (run !== this.run || error.name === 'AbortError') return;
                    // Transport/auth/budget errors pause; schema/legality errors inform the same player.
                    if (reply === undefined && !error.recoverable) throw error;
                    this.feedback = {rejected:true,message:error.message,samePlayer:side,positionRestored:true};
                    this.emit(side + ' is correcting its proposal: ' + error.message);
                }
            }
            throw Error('Turn planning safeguard reached. Resume to give this same player more attempts.');
        } catch (error) {
            if (run === this.run && error.name !== 'AbortError') await this.pause(error.message);
        }
    }
    async ok() {
        if (this.phase !== 'proposal') return;
        const run = this.run, before = this.state.commits, proposalId = this.state.proposal.id;
        this.phase = 'committing'; this.emit('Committing the approved turn.');
        try {
            await this.command('commit', {proposalId});
        } catch (error) {
            // A post-commit failure must never replay the accepted action.
            try {
                await this.command('state');
                if (this.state.commits !== before + 1) throw error;
            } catch { await this.pause('Could not confirm OK. Reconcile the current position by resuming; the turn will not be replayed.'); return; }
        }
        if (run !== this.run) return;
        this.feedback = null;
        await this.think(run);
    }
    async pause(message = 'Paused. The uncommitted proposal was discarded; the same player keeps the turn.') {
        this.cancel();
        this.phase = 'pausing'; this.emit(message);
        try { await this.command('state'); await this.command('pause'); }
        catch { message += ' Worker acknowledgement unavailable; resume will reconcile before continuing.'; }
        this.phase = 'paused'; this.emit(message);
    }
    async resume(limit = this.limit) {
        if (this.phase !== 'paused') return;
        if (!this.players[this.state?.side]) throw Error('Load your key and start a new match; player credentials were cleared.');
        if (!Number.isInteger(limit) || limit < 1 || limit > 200) throw Error('Request limit must be from 1 to 200.');
        this.cancel();
        const run = this.run;
        this.limit = limit;
        try { await this.command('state'); await this.command('resume'); }
        catch (error) { this.emit(error.message); return; }
        await this.think(run);
    }
    replaceKey(key) {
        if (this.demoUrl) throw Error('The public demo uses a server-side key. Enter Analysis mode to start a match with your own key.');
        if (this.phase !== 'paused') throw Error('Pause the match before replacing the API key.');
        key = normalizeApiKey(key);
        if (!this.players.White || !this.players.Black) throw Error('Start a new match to load player credentials.');
        for (const player of Object.values(this.players)) player.key = key.trim();
        this.emit('Replacement key loaded for both players. Resume to retry the current turn.');
    }
    async stop() {
        this.cancel();
        await this.command('state'); await this.command('stop');
        for (const player of Object.values(this.players)) player.forget?.();
        this.players = {};
        this.phase = 'stopped'; this.emit('Analysis mode. Credentials and player conversations cleared.');
    }
    dispose() {
        this.cancel();
        for (const player of Object.values(this.players)) player.forget?.();
        this.players = {};
    }
}
