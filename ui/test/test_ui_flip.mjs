import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

// Rendering dependencies are browser-only; exercise the actual board methods
// with a recording context and a single upright piece image.
const source = (await readFile(new URL('../scripts/draw.js', import.meta.url), 'utf8'))
    .replace(/^import .*;\n/gm, '');
const { default: ChessBoardCanvas } = await import(`data:text/javascript;base64,${Buffer.from(
    'const chooseLOD = () => ({ P: "piece" });\n' + source
).toString('base64')}`);

function board(width, height, flipped, preserveFiles) {
    return Object.assign(Object.create(ChessBoardCanvas.prototype), {
        boardLengthX: width, boardLengthY: height, squareSize: 10,
        boardSkipX: width * 10 + 20, boardSkipY: height * 10 + 20,
        backgroundShiftX: 10, backgroundShiftY: 10, fontSizeforLT: 16,
        flipped, doNotReverseFilesWhenFlipped: preserveFiles,
        infCanvas: {
            cameraCurrent: {
                getZoomLevel: () => 5, x: 0, y: 0,
                screenToWorld: () => ({ x: 0, y: 0 }),
            }, pixelRatio: 1,
            viewportSize: { width: 800, height: 600 },
        },
    });
}

test('square coordinates and hit detection agree in both flip modes and board sizes', () => {
    for (const [width, height] of [[8, 8], [7, 5], [1, 1]]) {
        for (const flipped of [false, true]) {
            for (const preserveFiles of [false, true]) {
                const canvas = board(width, height, flipped, preserveFiles);
                for (const l of [-2, 0, 3]) {
                    for (const t of [-1, 2]) {
                        for (const c of [false, true]) {
                            for (let x = 0; x < width; x++) {
                                for (let y = 0; y < height; y++) {
                                    const pos = { l, t, c, x, y };
                                    const [px, py] = canvas._getCoordinate(pos);
                                    assert.equal(px, (2 * t + Number(c)) * canvas.boardSkipX
                                        + (flipped && !preserveFiles ? width - 1 - x : x) * 10);
                                    const hit = canvas.worldToBoard(px + 5, py + 5);
                                    assert.deepEqual({ ...hit, l: hit.l || 0 }, pos);
                                }
                            }
                        }
                    }
                }
                // The horizontal gap must remain outside the board after reflection.
                const gap = canvas.worldToBoard(width * 10 + 5, 5);
                assert(gap.x < 0 || gap.x >= width);
            }
        }
    }
});

test('rendering keeps pieces, checkerboard, labels, highlights and arrows aligned', () => {
    for (const width of [7, 8]) {
        for (const flipped of [false, true]) {
            for (const preserveFiles of [false, true]) {
                const canvas = board(width, 8, flipped, preserveFiles);
                const parsed = Array.from({ length: 8 }, () => Array(width).fill(0));
                parsed[0][0] = 'P';
                const pos = { l: 2, t: 1, c: false, x: 0, y: 7 };
                canvas.data = {};
                canvas.colors = { squareWhite: 'white', squareBlack: 'black' };
                canvas.fontSizeforXY = 3;
                canvas._filterBoardData = () => {};
                canvas.filterCache = {
                    filteredBoards: [{ ...pos, parsed }],
                    filteredCoordinateHighlight: { highlight: [pos] },
                    filteredArrowHighlight: { arrow: [{ from: pos, to: { ...pos, x: width - 1 } }] },
                    filteredBoardHighlight: {}, filteredTimelineHighlight: {},
                };
                canvas.cameraElement = {};
                const rectangles = [], images = [], labels = [], arrows = [];
                canvas._drawArrow = (...args) => arrows.push(args.slice(1));
                const ctx = {
                    save() {}, restore() {}, fill() {},
                    fillRect(...args) { rectangles.push({ color: this.fillStyle, args }); },
                    drawImage(...args) { images.push(args); },
                    fillText(text, x, y) { labels.push({ text, x, y, color: this.fillStyle }); },
                    measureText() { return { actualBoundingBoxLeft: 0, actualBoundingBoxAscent: 2, actualBoundingBoxDescent: 0 }; },
                    createLinearGradient() { return { addColorStop() {} }; },
                };
                canvas.drawBoards(ctx, -3, 3, 0, 4);
                const [px, py] = canvas._getCoordinate(pos);
                assert.deepEqual(images, [['piece', px, py, 10, 10]]);
                assert(rectangles.some(rect => rect.color === 'white' && rect.args[0] === px && rect.args[1] === py));
                assert(rectangles.some(rect => rect.color === 'highlight' && rect.args[0] === px && rect.args[1] === py));
                const [tx, ty] = canvas._getCoordinate({ ...pos, x: width - 1 });
                assert.deepEqual(arrows, [[px + 5, py + 5, tx + 5, ty + 5]]);
                const files = labels.filter(label => /^[a-h]$/.test(label.text)).sort((a, b) => a.x - b.x);
                const expected = Array.from({ length: width }, (_, i) => String.fromCharCode(97 + i));
                assert.deepEqual(files.map(label => label.text), flipped && !preserveFiles ? expected.reverse() : expected);
                const rank8 = labels.find(label => label.text === '8');
                assert.equal(rank8.y, py + 2.3);
                assert.equal(rank8.color, canvas._getLabelTextColor(0, canvas._displayBoardCol(0)));
            }
        }
    }
});

test('changing the preference redraws without moving boards or the camera', () => {
    const canvas = board(8, 8, true, false);
    let redraws = 0;
    canvas.infCanvas.startAnimation = () => redraws++;
    canvas.infCanvas.cameraTarget = { x: -240, y: 160 };
    const camera = { ...canvas.infCanvas.cameraTarget };
    const before = canvas._getCoordinate({ l: 2, t: 1, c: false, x: 0, y: 0 });
    canvas.setDoNotReverseFilesWhenFlipped(true);
    assert.equal(redraws, 1);
    assert.deepEqual(canvas.infCanvas.cameraTarget, camera);
    assert.equal(canvas._displayL(2), -2);
    const after = canvas._getCoordinate({ l: 2, t: 1, c: false, x: 0, y: 0 });
    assert.equal(before[0] - after[0], 70);
    assert.equal(before[1], after[1]);
});
