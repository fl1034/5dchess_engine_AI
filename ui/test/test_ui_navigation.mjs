import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const load = async name => import(`data:text/javascript;base64,${Buffer.from(await readFile(new URL(`../scripts/${name}.js`, import.meta.url), 'utf8')).toString('base64')}`);
const { nearestBoard, directionalTarget } = await load('navigation');
const { createKeyboardShortcuts } = await load('keyboard');
const directions = { up: { x: 0, y: -1 }, left: { x: -1, y: 0 }, down: { x: 0, y: 1 }, right: { x: 1, y: 0 } };

test('nearest board uses visual distance and handles empty data and ties deterministically', () => {
    const boards = [{ x: 0, y: 2 }, { x: 3, y: 0 }];
    assert.equal(nearestBoard(boards, { x: 0, y: 0 }, { x: 100, y: 200 }), boards[1]);
    assert.equal(nearestBoard(boards, { x: 0, y: 0 }, { x: 100, y: 100 }), boards[0]);
    assert.equal(nearestBoard([], { x: 0, y: 0 }, { x: 1, y: 1 }), null);
    assert.deepEqual(nearestBoard([{ x: -1, y: 0 }, { x: 1, y: 0 }], { x: 0, y: 0 }, { x: 1, y: 1 }), { x: -1, y: 0 });
});

test('directional navigation skips gaps only in the same row or column', () => {
    const boards = [{ x: 0, y: 0 }, { x: 2, y: 0 }, { x: 7, y: 0 }, { x: 0, y: -3 }, { x: 1, y: -1 }];
    const origin = { x: 0, y: 0 };
    assert.deepEqual(directionalTarget(boards, origin, directions.right), { x: 2, y: 0 });
    assert.deepEqual(directionalTarget(boards, origin, directions.up), { x: 0, y: -3 });
    assert.deepEqual(directionalTarget(boards, origin, directions.left), { x: -1, y: 0 });
    assert.deepEqual(directionalTarget(boards, origin, directions.down), { x: 0, y: 1 });
    assert.deepEqual(directionalTarget(boards, { x: 7, y: 0 }, directions.left), { x: 2, y: 0 });
});

test('empty space supports continued steps and returning to a board', () => {
    const boards = [{ x: -2, y: -1 }];
    let target = { x: -2.1, y: -0.9 };
    target = directionalTarget(boards, target, directions.left);
    assert.deepEqual(target, { x: -3, y: -1 });
    target = directionalTarget(boards, target, directions.left);
    assert.deepEqual(target, { x: -4, y: -1 });
    assert.deepEqual(directionalTarget(boards, target, directions.right), boards[0]);
});

function keyboard() {
    const calls = [];
    let time = 0;
    let blocked = false;
    const handler = createKeyboardShortcuts({ onAction: (...args) => calls.push(args), isBlocked: () => blocked, now: () => time });
    const press = (key, overrides = {}) => {
        const event = { key, target: { closest: () => null }, preventDefault() { this.defaultPrevented = true; }, ...overrides };
        handler.handleKeyDown(event);
        return event;
    };
    return { calls, press, reset: handler.reset, tick: ms => { time += ms; }, block: value => { blocked = value; } };
}

test('Space responds immediately, double Space widens the same board, and Shift+Space is direct', () => {
    const k = keyboard();
    assert(k.press(' ').defaultPrevented);
    assert.deepEqual(k.calls.pop(), ['nearby', { wide: false, reuse: false }]);
    k.tick(150);
    k.press(' ', { repeat: true });
    assert.equal(k.calls.length, 0);
    k.press(' ');
    assert.deepEqual(k.calls.pop(), ['nearby', { wide: true, reuse: true }]);
    k.tick(400); k.press(' ');
    assert.deepEqual(k.calls.pop(), ['nearby', { wide: false, reuse: false }]);
    k.press(' ', { shiftKey: true });
    assert.deepEqual(k.calls.pop(), ['nearby', { wide: true, reuse: false }]);
    k.reset(); k.press(' ');
    assert.deepEqual(k.calls.pop(), ['nearby', { wide: false, reuse: false }]);
});

test('editing, composition, browser modifiers, dialogs and native activation are untouched', () => {
    const k = keyboard();
    for (const overrides of [
        { target: { isContentEditable: true } },
        { target: { closest: selector => selector.includes('input') ? {} : null } },
        { isComposing: true }, { keyCode: 229 }, { metaKey: true }, { ctrlKey: true }, { altKey: true }
    ]) {
        for (const key of ['f', 'w', ' ', 'Enter', 'ArrowLeft']) assert(!k.press(key, overrides).defaultPrevented);
    }
    const button = { closest: selector => selector.includes('button') ? {} : null };
    assert(!k.press(' ', { target: button }).defaultPrevented);
    assert(!k.press('Enter', { target: button }).defaultPrevented);
    k.block(true); assert(!k.press('w').defaultPrevented); k.block(false);
    assert.equal(k.calls.length, 0);
    k.press('w', { target: button });
    k.press('F', { target: button });
    assert.deepEqual(k.calls.map(([action]) => action), ['up', 'focus']);
});

test('distinct navigation presses chain immediately; held keys are bounded and focus does not repeat', () => {
    const k = keyboard();
    k.press('d'); k.press('d'); k.press('d', { repeat: true });
    assert.equal(k.calls.length, 2);
    k.tick(120); k.press('d', { repeat: true });
    k.press('f'); k.press('f', { repeat: true });
    assert.deepEqual(k.calls.map(([action]) => action), ['right', 'right', 'right', 'focus']);
});

test('existing history-arrow shortcuts retain key-repeat behavior', () => {
    const k = keyboard();
    k.press('ArrowUp'); k.press('ArrowUp', { repeat: true });
    assert.deepEqual(k.calls.map(([action]) => action), ['prev', 'prev']);
});
