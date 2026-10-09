// UI gesture regressions. Run with: node --test scripts/test/test_ui_canvas.mjs
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

// The browser sources are ES modules; load without changing ui/'s local Node configuration.
const source = await readFile(new URL('../../ui/scripts/canvas.js', import.meta.url), 'utf8');
const { InfiniteScrollableCanvas } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

function fixture() {
    const clicks = [];
    const canvas = Object.create(InfiniteScrollableCanvas.prototype);
    Object.assign(canvas, {
        config: { dragThreshold: 5, dragSpeed: 1, minZoom: -5, maxZoom: 7 },
        cameraCurrent: { scale: 0, getZoomLevel: () => 1 },
        cameraTarget: { x: 0, y: 0, scale: 0 },
        dragStart: {}, touchStartPos: {},
        startAnimation() {},
        _getTouchWorldPosition: touch => ({ x: touch.clientX, y: touch.clientY }),
        onClick: (x, y) => clicks.push([x, y])
    });
    return { canvas, clicks };
}
const point = (x, y = 20) => ({ clientX: x, clientY: y });
const event = (touches, changedTouches = []) => ({ touches, changedTouches, preventDefault() {} });

test('a stationary tap selects exactly once', () => {
    const { canvas, clicks } = fixture();
    canvas._handleTouchStart(event([point(20)]));
    canvas._handleTouchEnd(event([], [point(20)]));
    assert.deepEqual(clicks, [[20, 20]]);
});

test('a fast swipe cannot become a tap even without an intermediate move event', () => {
    const { canvas, clicks } = fixture();
    canvas._handleTouchStart(event([point(20)]));
    canvas._handleTouchEnd(event([], [point(40)]));
    assert.deepEqual(clicks, []);
});

test('dragging out and back to the starting square does not select it', () => {
    const { canvas, clicks } = fixture();
    canvas._handleTouchStart(event([point(20)]));
    canvas._handleTouchMove(event([point(40)]));
    canvas._handleTouchMove(event([point(20)]));
    canvas._handleTouchEnd(event([], [point(20)]));
    assert.deepEqual(clicks, []);
});

test('lifting fingers after a pinch does not select a piece; the next tap still works', () => {
    const { canvas, clicks } = fixture();
    canvas._handleTouchStart(event([point(20)]));
    canvas._handleTouchStart(event([point(20), point(60)]));
    canvas._handleTouchMove(event([point(20), point(80)]));
    canvas._handleTouchEnd(event([point(20)], [point(80)]));
    canvas._handleTouchEnd(event([], [point(20)]));
    assert.deepEqual(clicks, []);
    canvas._handleTouchStart(event([point(30)]));
    canvas._handleTouchEnd(event([], [point(30)]));
    assert.deepEqual(clicks, [[30, 20]]);
});

test('resizing uses the reserved canvas area and preserves relative mobile zoom', () => {
    const { canvas } = fixture();
    const previousDocument = globalThis.document;
    globalThis.document = { body: { classList: { contains: () => true } } };
    try {
        let rect = { width: 390, height: 600 };
        canvas.canvas = { getBoundingClientRect: () => rect };
        canvas._render = () => {};
        canvas.resize();
        assert.equal(canvas.canvas.width, 390);
        assert.equal(canvas.canvas.height, 600);
        rect = { width: 390, height: 195 };
        canvas.resize();
        assert.equal(canvas.canvas.height, 195);
        assert.equal(canvas.cameraTarget.scale, -1);
        assert.equal(canvas.cameraCurrent.scale, -1);
        rect = { width: 390, height: 600 };
        canvas.resize();
        assert.equal(canvas.cameraTarget.scale, 0);
    } finally {
        if (previousDocument === undefined) delete globalThis.document;
        else globalThis.document = previousDocument;
    }
});

test('display density changes preserve camera bounds and mouse/touch positions', () => {
    const previous = Object.fromEntries(['document', 'window', 'ResizeObserver', 'devicePixelRatio'].map(key => [key, globalThis[key]]));
    const calls = [];
    const ctx = Object.fromEntries(['save', 'restore', 'setTransform', 'clearRect', 'scale', 'translate'].map(name => [name, (...args) => calls.push([name, ...args])]));
    const element = {
        getContext: () => ctx,
        getBoundingClientRect: () => ({ left: 10, top: 20, right: 400, bottom: 620, width: 390, height: 600 }),
        addEventListener() {}
    };
    let densityChanged;
    globalThis.document = { getElementById: () => element, body: { classList: { contains: () => true } } };
    globalThis.window = {
        addEventListener() {},
        matchMedia: () => ({ addEventListener: (_, listener) => { densityChanged = listener; }, removeEventListener() {} })
    };
    globalThis.ResizeObserver = class { observe() {} };
    globalThis.devicePixelRatio = 1;
    try {
        const canvas = new InfiniteScrollableCanvas('canvas', { initialZoom: 1, initialX: 7, initialY: -3 });
        const bounds = canvas.getVisibleBounds();
        const pointer = { clientX: 245, clientY: 360 };
        const expected = canvas._getMouseWorldPosition(pointer);
        assert.deepEqual(expected, { x: 13, y: 23 });
        for (const ratio of [1.25, 1.5, 2, 3, 1]) {
            globalThis.devicePixelRatio = ratio;
            calls.length = 0;
            densityChanged();
            assert.equal(element.width, Math.round(390 * ratio));
            assert.equal(element.height, Math.round(600 * ratio));
            assert.deepEqual(canvas.getVisibleBounds(), bounds);
            assert.deepEqual(canvas._getMouseWorldPosition(pointer), expected);
            assert.deepEqual(canvas._getTouchWorldPosition(pointer), expected);
            assert.equal(canvas.cameraCurrent.scale, 1);
            assert.equal(canvas.cameraTarget.scale, 1);
            assert.deepEqual(calls.find(([name]) => name === 'clearRect'), ['clearRect', 0, 0, element.width, element.height]);
            assert.deepEqual(calls.find(([name]) => name === 'scale'), ['scale', element.width / 390, element.height / 600]);
        }
    } finally {
        for (const [key, value] of Object.entries(previous)) {
            if (value === undefined) delete globalThis[key];
            else globalThis[key] = value;
        }
    }
});
