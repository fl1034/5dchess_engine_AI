// The existing worker owns this adapter and the live WASM game. No network code.
const playing = status => status === "White's Move" || status === "Black's Move";
const coord = p => p && ['x', 'y', 't', 'l'].every(k => Number.isInteger(p[k]))
    && p.x >= 0 && p.x < 8 && p.y >= 0 && p.y < 8
    && p.t >= 0 && p.t <= 32767 && Math.abs(p.l) <= 32767;

export function checkedMoves(moves) {
    if (!Array.isArray(moves) || moves.length > 64) throw Error('Supply at most 64 ordered piece moves.');
    return moves.map(m => {
        if (!coord(m?.from) || !coord(m?.to) || !Number.isInteger(m.promote_to)
            || ![0, ...'QRBNUDSCqrbnudsc'.split('').map(c => c.charCodeAt(0))].includes(m.promote_to)) {
            throw Error('Invalid move coordinates or promotion code. Use {from:{x,y,t,l},to:{x,y,t,l},promote_to:0}.');
        }
        return structuredClone(m);
    });
}

export class EngineMatch {
    constructor(engine, getGame, replaceGame, refresh) {
        Object.assign(this, { engine, getGame, replaceGame, refresh });
        this.locked = true;
        this.epoch = 0;
        this.revision = 0;
        this.matchId = null;
        this.proposal = null;
        this.paused = true;
        this.commits = 0;
        this.receipts = new Map();
    }
    get game() { return this.getGame(); }
    pgn() { return this.game.show_pgn(this.engine.SHOW_CAPTURE | this.engine.SHOW_PROMOTION, false, true); }
    rollback() {
        this.game.discard_preview();
        while (this.game.can_undo()) {
            if (!this.game.undo()) throw Error('Could not restore committed position.');
        }
        this.proposal = null;
    }
    identity() {
        return { matchId: this.matchId, epoch: this.epoch, revision: this.revision };
    }
    snapshot() {
        return {
            ...this.identity(), commits: this.commits, paused: this.paused,
            status: this.game.get_match_status(), side: this.side,
            size: this.game.get_board_size(), present: this.game.get_current_present(),
            timelines: this.game.get_current_timeline_status(),
            boards: this.game.get_current_boards(), movablePieces: this.game.get_movable_pieces(),
            promotionMask: this.game.get_promotion_options(),
            checks: this.game.get_current_checks(), canSubmit: this.game.can_submit(false),
            pgn: this.committedPgn ?? this.pgn(),
            proposal: this.proposal ? structuredClone(this.proposal) : null,
        };
    }
    assertIdentity(message) {
        if (!this.matchId || ['matchId', 'epoch', 'revision'].some(k => message[k] !== this[k])) {
            throw Error('Stale match or position. Refresh state before continuing.');
        }
    }
    apply(moves) {
        for (let i = 0; i < moves.length; i++) {
            if (!this.game.apply_move(moves[i])) throw Error('Move ' + (i + 1) + ' is not playable: ' + JSON.stringify(moves[i]));
        }
    }
    handle(message) {
        const receiptKey = JSON.stringify([message.matchId, message.epoch, message.requestId]);
        if (this.receipts.has(receiptKey)) return structuredClone(this.receipts.get(receiptKey));
        let result;
        try {
            const op = message.op;
            if (op === 'boot') {
                this.locked = true;
                this.side = this.game.get_current_present().c ? 'Black' : 'White';
                this.committedPgn = this.pgn();
            } else if (op === 'start') {
                if (typeof message.matchId !== 'string' || !message.matchId) throw Error('Missing match identity.');
                const next = this.engine.from_pgn('[Board "Standard - Turn Zero"]', false);
                if (!next.success) throw Error('Could not initialize standard position.');
                this.replaceGame(next.game);
                this.locked = true;
                this.matchId = message.matchId;
                this.epoch++;
                this.revision++;
                this.commits = 0;
                this.proposal = null;
                this.paused = false;
                this.side = 'White';
                this.committedPgn = this.pgn();
                this.refresh();
            } else if (op === 'state') {
                // Read-only reconciliation also works after a lost mutation acknowledgement.
                if (message.matchId !== this.matchId) throw Error('Different match.');
            } else {
                this.assertIdentity(message);
                if (op === 'pause' || op === 'resume' || op === 'stop') {
                    this.rollback();
                    this.epoch++;
                    this.revision++;
                    this.paused = op !== 'resume';
                    if (op === 'stop') this.locked = false;
                    this.refresh();
                } else {
                    if (!this.locked || this.paused || !playing(this.game.get_match_status())) throw Error('Match is paused or finished.');
                    if (op === 'inspect') {
                        if (this.proposal) throw Error('A proposal already awaits OK.');
                        try {
                            this.apply(checkedMoves(message.moves));
                            if (message.source !== null && !coord(message.source)) throw Error('Invalid inspection source.');
                            result = {
                                position: this.snapshot(),
                                destinations: message.source ? this.game.gen_move_if_playable(message.source) : [],
                                complete: this.game.can_submit(false),
                            };
                        } finally { this.rollback(); }
                    } else if (op === 'propose') {
                        if (this.proposal) throw Error('A proposal already awaits OK.');
                        try {
                            const moves = checkedMoves(message.moves);
                            if (!moves.length) throw Error('A turn must contain piece moves.');
                            this.apply(moves);
                            if (!this.game.can_submit(false)) {
                                throw Error('Incomplete or unsafe turn: advance the present on all required timelines and leave no friendly royal exposed. Inspect this prefix for remaining playable pieces.');
                            }
                            this.revision++;
                            this.proposal = { id: message.requestId, moves };
                            this.refresh();
                        } catch (error) { this.rollback(); throw error; }
                    } else if (op === 'commit') {
                        if (!this.proposal || message.proposalId !== this.proposal.id) throw Error('No matching proposal awaits OK.');
                        if (!this.game.can_submit(false) || !this.game.submit(false)) throw Error('Strict submission failed.');
                        // Record the transition before queries/refresh: never replay after a later failure.
                        this.proposal = null;
                        this.commits++;
                        this.revision++;
                        this.side = this.game.get_current_present().c ? 'Black' : 'White';
                        this.committedPgn = this.pgn();
                        this.refresh();
                    } else throw Error('Unknown match operation.');
                }
            }
            result = { ok: true, ...result, state: this.snapshot() };
        } catch (error) {
            result = { ok: false, error: error.message, identity: this.identity() };
        }
        result.requestId = message.requestId;
        this.receipts.set(receiptKey, structuredClone(result));
        if (this.receipts.size > 128) this.receipts.delete(this.receipts.keys().next().value);
        return result;
    }
}
