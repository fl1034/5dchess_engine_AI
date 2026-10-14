import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
const { PendingBranches, actionKey } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/branch-deletion.js', import.meta.url), 'utf8')));
const move = x => [{ from: { l: 0, t: 1, x, y: 1 }, to: { l: 0, t: 1, x, y: 3 }, promote: 0 }];
const a = move(4), b = move(3);

test('pending branches distinguish promotions, deduplicate and preflight all targets', () => {
    const pending = new PendingBranches();
    const options = [{ action: a, pgn: 'e4' }, { action: b, pgn: 'd4' }];
    assert.notEqual(actionKey(a), actionKey([{ ...a[0], promote: 5 }]));
    assert(pending.toggle(a, options));
    assert(pending.has(structuredClone(a)));
    assert.equal(pending.plan(options).length, 1);
    assert.equal(pending.plan(options, a).length, 0);
    assert.throws(() => pending.plan(options.slice(1)));
    assert(pending.toggle(a, options));
    assert.equal(pending.plan(options).length, 0);
    assert(!pending.toggle(move(1), options));
});

async function workerFixture() {
    const messages = [], removed = [], navigated = [];
    let options = [{ action: a, pgn: 'e4' }, { action: b, pgn: 'd4' }];
    const game = {
        get_child_actions: () => options,
        has_child: action => options.some(child => actionKey(child.action) === actionKey(action)),
        delete_child(action) {
            removed.push(actionKey(action));
            options = options.filter(child => actionKey(child.action) !== actionKey(action));
            return true;
        },
        get_submit_action: () => a,
        submit: () => { navigated.push('submit'); return true; },
        visit_parent: () => navigated.push('prev'),
        visit_child: () => { navigated.push('next'); return true; },
        suggest_action: () => a, get_current_present: () => ({ c: false, t: 1 }),
        get_historical_actions: () => [], get_comments: () => [], get_metadata: () => ({}),
        get_match_status: () => '', can_undo: () => true, can_redo: () => true,
        has_parent: () => true, can_submit: () => true, currently_check: () => false,
        discard_preview() {}, undo() {}, delete() {},
        get_board_size: () => ({ x: 8, y: 8 }), get_current_boards: () => [],
        get_current_timeline_status: () => ({ mandatory_timelines: [] }),
        get_cached_moves: () => [], get_phantom_boards_and_checks: () => ({ boards: [], checks: [] }),
        show_pgn: () => options.map(child => child.pgn).join(' '),
    };
    const self = { postMessage: message => messages.push(structuredClone(message)) };
    const engine = { get_version: () => 'test',
        from_pgn: pgn => pgn === 'invalid' ? { success: false, message: 'syntax error' } : { success: true, game } };
    const source = (await readFile(new URL('../scripts/worker.js', import.meta.url), 'utf8')).replace(/^import .*;\n/gm, '');
    vm.runInNewContext(source, { self, PendingBranches, actionKey, hashAction: actionKey,
        hashActionList: JSON.stringify, createModule: () => Promise.resolve(engine) });
    await Promise.resolve();
    self.game = game;
    const send = (type, data = {}) => self.onmessage({ data: { type, positionId: 0, ...data } });
    const last = type => messages.filter(message => message.type === type).at(-1);
    return { send, last, messages, removed, navigated, game };
}

test('cancel, restore-all and stale confirmations leave the tree intact', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('prev');
    assert.equal(f.last('confirm_deletions').actions[0], 'e4');
    f.send('cancel_deletions', { id: f.last('confirm_deletions').id });
    assert.equal(f.last('pending_deletions').count, 1);
    assert.deepEqual(f.removed, []);
    assert.deepEqual(f.navigated, []);
    f.send('prev');
    const id = f.last('confirm_deletions').id;
    f.send('restore_all');
    f.send('accept_deletions', { id });
    assert.equal(f.last('pending_deletions').count, 0);
    assert.deepEqual(f.removed, []);
    assert.deepEqual(f.navigated, []);
    f.send('toggle_deletion', { action: a, positionId: 99 });
    assert.equal(f.last('pending_deletions').count, 0);
});

test('next rejects pending/missing children and confirmation deletes before navigation', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('next', { action: a });
    f.send('next', { action: move(0) });
    assert.equal(f.last('confirm_deletions'), undefined);
    f.send('next', { action: b });
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert.deepEqual(f.removed, [actionKey(a)]);
    assert.deepEqual(f.navigated, ['next']);
});

test('submit preserves the matching original branch and confirms only other deletions', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('toggle_deletion', { action: b });
    f.send('submit');
    assert.deepEqual(f.last('confirm_deletions').actions, ['d4']);
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert.deepEqual(f.removed, [actionKey(b)]);
    assert.deepEqual(f.navigated, ['submit']);
    const only = await workerFixture();
    only.send('toggle_deletion', { action: a });
    only.send('submit');
    assert.equal(only.last('confirm_deletions'), undefined);
    assert.deepEqual(only.removed, []);
    assert.deepEqual(only.navigated, ['submit']);
});

test('export opening waits for confirmation; direct confirmation needs no popup', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('open_export');
    assert.equal(f.last('open_export'), undefined);
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert(f.last('open_export'));
    f.send('export', { requestId: 12 });
    assert.equal(f.last('update_pgn').pgn, 'd4');
    const direct = await workerFixture();
    direct.send('toggle_deletion', { action: a });
    direct.send('commit_deletions');
    assert.equal(direct.last('confirm_deletions'), undefined);
    assert.deepEqual(direct.removed, [actionKey(a)]);
});

test('failed load preserves pending changes; successful load clears them without deletion', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('load', { pgn: 'invalid' });
    assert.equal(f.last('pending_deletions').count, 1);
    f.send('open_export');
    assert.equal(f.last('confirm_deletions').actions.length, 1);
    f.send('load', { pgn: 'valid' });
    assert.equal(f.last('pending_deletions').count, 0);
    assert.deepEqual(f.removed, []);
});
