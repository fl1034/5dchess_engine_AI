// main.js

// index.html has already started the worker before these imports load piece assets.
import { worker, connectWorker, reportLoaded } from './startup.js';
import { UI } from 'ui';
import { actionKey } from './branch-deletion.js';
import ChessBoardCanvasBase from 'draw';
import { compactViewport, initMobileLayout } from './mobile.js';
import { createSelection } from './selection.js';
import { mountSpectator } from './ai-ui.mjs';

initMobileLayout();


// State management for move selection
let presentC;
let nextOptions = [];
let positionId = 0;
let showPhantom = true;
let focusOnNextData = true;
let committedData = null;
let spectator;

class ChessBoardCanvas extends ChessBoardCanvasBase {
    onClickSquare(l, t, c, x, y) {
        if (spectator?.active) return;
        selection.click({ l, t, c, x, y });
    }
    
    onPreviewSquare(pos) { if (!spectator?.active) return selection.preview(pos); }
    onCommitSquare(token) { selection.commit(token); }
    onCancelSquare(token) { selection.cancel(token); }

    onRightClickSquare() {
        deselect();
    }
}

window.chessBoardCanvas = new ChessBoardCanvas('canvas');
const renderBoard = window.chessBoardCanvas.infCanvas.onRender;
window.chessBoardCanvas.infCanvas.onRender = (ctx, bounds) => {
    renderBoard(ctx, bounds);
    if (committedData) {
        window.chessBoardCanvas.infCanvas.onRender = renderBoard;
        reportLoaded('position');
        UI.showInfoPage();
    }
};
compactViewport.addEventListener('change', () => {
    if (compactViewport.matches) {
        requestAnimationFrame(() => {
            window.chessBoardCanvas.infCanvas.resize();
            window.chessBoardCanvas.goToNextFocus();
        });
    }
});

const selection = createSelection({
    send: message => worker.postMessage(message),
    presentColor: () => presentC,
    onDiscardPreview: () => { if (committedData) showPosition(committedData); },
    onChange: ({ selected, moves }) => {
        const feedback = selected ? [selected] : [];
        window.chessBoardCanvas.setSelectionHighlights([
            { color: '--highlight-movable-piece', coordinates: feedback },
            { color: '--highlight-generated-move', coordinates: moves.map(pos => ({ ...pos, c: selected.c })) }
        ]);
    }
});

function deselect() {
    window.chessBoardCanvas.infCanvas._cancelPendingTap();
    selection.reset();
}

function showPosition(raw, preview = false) {
    // Keep the real position intact for immediate cancellation of a preview.
    const data = structuredClone(raw);
    if (spectator?.active && !data.focus?.length) {
        // A completed draft has no mandatory boards left. Focus its resulting
        // timeline tips so the spectator can actually review the proposed turn.
        const tips = new Map();
        for (const board of data.boards) {
            const previous = tips.get(board.l);
            if (!previous || 2 * board.t + Number(board.c) > 2 * previous.t + Number(previous.c)) tips.set(board.l,board);
        }
        data.focus = [...tips.values()].map(({l,t,c}) => ({l,t,c}));
    }
    window.chessBoardCanvas.gestureData = preview && committedData ? {
        boards: [...(committedData.boards || []), ...(showPhantom ? committedData.phantom || [] : [])]
    } : undefined;
    if(data.phantom && data.phantom.length > 0) {
        UI.setHudLight(true);
        if (showPhantom) {
            addHighlight(data, '--highlight-phantom-board', 'boards', data.phantom.map((b) => ({l: b.l, t: b.t, c: !data.present.c})));
            data.boards = data.boards.concat(data.phantom.map((board) => ({
                ...board,
                isPhantom: true
            })));
            addHighlight(data, '--highlight-check', 'arrows', data.phantomChecks.map((c) => ({
                from: {...c.from, c: !data.present.c},
                to: {...c.to, c: !data.present.c}
            })));
        }
    } else {
        UI.setHudLight(false);
    }
    window.chessBoardCanvas.setData(data);
}

function disablePhantom() {
    showPhantom = false;
    UI.setHudLight(false);
}

function addHighlight(data, color, field, values) {
  if (!Array.isArray(data.highlights)) {
    data.highlights = [];
  }
  let colorBlock = data.highlights.find(h => h.color === color);
  if (!colorBlock) {
    data.highlights.push({
      color,
      [field]: [...values],
    });
  } else {
    if (!Array.isArray(colorBlock[field])) {
      colorBlock[field] = [];
    }
    colorBlock[field].push(...values);
  }
}


function onWorkerMessage(e) {
    const msg = e.data;
    if (msg.type === 'ready') {
        // Send current export options to worker on startup
        worker.postMessage({
            type: 'update_settings', 
            settings: UI.getSettings()
        });
        worker.postMessage({type: 'load', pgn: '[Board "Standard - Turn Zero"]'});
        spectator.boot();
    }
    else if (msg.type === 'engine_version') {
        // Update the version in the Information popup and welcome popup
        UI.setVersionNumber(msg.version);
        console.log('Version', msg.version);
    }
    else if (msg.type === 'alert') {
        alert(msg.message);
    }
    else if (msg.type === 'load_result') {
        if (UI.receiveImportResult(msg)) worker.postMessage({ type: 'commit_load', requestId: msg.requestId });
    }
    else if (msg.type === 'load_committed') {
        UI.completeImport(msg.requestId);
    }
    else if (msg.type === 'data') {
        let data = msg.data;
        if (msg.previewId == null) presentC = data.present.c;
        if (msg.positionChanged) deselect();
        if (msg.previewId != null) {
            if (!selection.acceptsPreview(msg.previewId)) return;
        } else {
            committedData = data;
        }
        showPosition(data, msg.previewId != null);
        if (msg.previewId == null && focusOnNextData && (compactViewport.matches || spectator?.active)) window.chessBoardCanvas.goToNextFocus();
        if (msg.previewId == null) focusOnNextData = false;
    }
    else if (msg.type === 'moves') {
        selection.receiveMoves(msg);
    }
    else if (msg.type === 'update_buttons')
    {
        UI.setHintAvailable(msg.hint);
        UI.setAllowSubmitWithChecks(msg.allowSubmitWithChecks, msg.allowSubmitWithChecksLocked);
        for(let key of ['undo', 'redo', 'prev', 'next', 'submit'])
        {
            if(msg[key] && (key !== 'next' || UI.select.getSelectedIndex() >= 0))
            {
                UI.buttons.enable(key);
            }
            else
            {
                UI.buttons.disable(key);
            }
        }
    }
    else if (msg.type === 'update_select')
    {
        positionId = msg.positionId;
        let options = msg.options;
        const wasEmpty = nextOptions.length === 0;
        const previous = nextOptions[UI.select.getSelectedIndex()];
        nextOptions = msg.options.map(obj => obj.action);
        let selection;
        if (msg.preserveSelection && !(msg.hintAdded && wasEmpty)) {
            selection = previous ? nextOptions.findIndex(action => actionKey(action) === actionKey(previous)) : -1;
        } else if (!msg.preserveSelection && msg.selectedIndex !== undefined) {
            selection = msg.selectedIndex;
        }
        UI.select.setOptions(options, {
            selection,
            preserveFocus: Boolean(msg.preserveSelection),
            scrollToBottom: Boolean(msg.hintAdded),
        });
    }
    else if (msg.type === 'navigated') {
        disablePhantom();
    }
    else if (msg.type === 'pending_deletions') {
        UI.setPendingDeletions(msg.count);
    }
    else if (msg.type === 'confirm_deletions') {
        UI.confirmDeletions(msg, accepted => worker.postMessage({
            type: accepted ? 'accept_deletions' : 'cancel_deletions', id: msg.id
        }));
    }
    else if (msg.type === 'open_export') {
        UI.openExport();
    }
    else if (msg.type === 'update_pgn')
    {
        UI.setExportData(msg.pgn, msg.requestId);
    }
    else if (msg.type === 'update_hud_status')
    {
        UI.setHudTitle(msg.hudTitle);
        UI.setGameMetadata(msg.metadata);
        if (msg.hudText) {
            UI.setHudText(msg.hudText);
        }
        else {
            UI.setHudText('');
        }
    }
    spectator?.gate();
}

UI.select.setChangeCallback(index => index < 0 ? UI.buttons.disable('next') : UI.buttons.enable('next'));
UI.setDeletionCallback((type, index) => worker.postMessage({ type, positionId, action: nextOptions[index] }));
UI.setExportOpenCallback(() => worker.postMessage({ type: 'open_export', positionId }));

UI.buttons.setCallbacks({
    prev: () => {
        if (spectator?.active) return;
        deselect();
        worker.postMessage({type: 'prev', positionId});
    },
    next: () => {
        if (spectator?.active) return;
        deselect();
        let index = UI.select.getSelectedIndex();
        let action = nextOptions[index];
        worker.postMessage({type: 'next', action: action, positionId});
    },
    undo: () => {
        if (spectator?.active) return;
        deselect();
        worker.postMessage({type: 'undo'});
    },
    redo: () => {
        if (spectator?.active) return;
        deselect();
        worker.postMessage({type: 'redo'});
    },
    submit: () => {
        if (spectator?.active) { spectator.ok(); return; }
        deselect();
        worker.postMessage({type: 'submit', positionId});
    }
});

UI.setHintCallback(() => {
    if (spectator?.active) return;
    deselect();
    worker.postMessage({type: 'hint'});
});

UI.setFocusCallback(() => {
    window.chessBoardCanvas.goToNextFocus();
});

UI.setCanvasNavigationCallback((action, options) => {
    if (action === 'nearby') window.chessBoardCanvas.zoomNearestBoard(options);
    else window.chessBoardCanvas.navigateBoard(action, options);
});

UI.setImportCallback((data, requestId) => {
    if (spectator?.active) return;
    deselect();
    focusOnNextData = true;
    worker.postMessage({type: 'load', pgn: data, requestId});
});

UI.setImportCancelCallback(requestId => worker.postMessage({ type: 'cancel_load', requestId }));

UI.setExportCallback((requestId) => {
    worker.postMessage({type: 'export', requestId});
});

UI.setCommentsEditCallback((text) => {
    if (spectator?.active) return;
    worker.postMessage({ type: 'update_comment', comment: text });
});

// Unified settings handler: forward to worker and handle local UI changes
UI.setSettingsChangeCallback((settings) => {
    // Forward all settings updates to the worker
    worker.postMessage({ type: 'update_settings', settings: settings });

    // Handle local UI changes based on settings keys
    if (settings.theme !== undefined) {
        // Theme changed: reload canvas colors
        if (window.chessBoardCanvas && window.chessBoardCanvas.reloadColors) {
            window.chessBoardCanvas.reloadColors();
        }
    } else if (settings.debugWindow !== undefined) {
        // Toggle debug window visibility
        const dbg = document.querySelector('.debug-window');
        if (dbg) dbg.style.display = settings.debugWindow ? '' : 'none';
    } else if (settings.allowSubmitWithChecks !== undefined) {
        // No local action needed here; worker will use this
    } else if (settings.showMovablePieces !== undefined) {
        worker.postMessage({ type: 'view' });
    } else if (settings.autoToggleComments !== undefined) {
        // Auto toggle of the HUD comments area is handled entirely within UI.js
    } else if (settings.doNotReverseFilesWhenFlipped !== undefined) {
        if (window.chessBoardCanvas) {
            window.chessBoardCanvas.setDoNotReverseFilesWhenFlipped(settings.doNotReverseFilesWhenFlipped);
        }
    } else if (settings.flipped !== undefined) {
        if (window.chessBoardCanvas) {
            window.chessBoardCanvas.setFlipped(settings.flipped);
        }
    }
});

// Testing HUD light functions
UI.setHudLightCallback((isOn) => {
    showPhantom = isOn;
    worker.postMessage({ type: 'view' });
});

UI.setHudLight(false); // Initialize HUD light to off

// Replay early engine messages only after all UI callbacks are installed.
reportLoaded('interface');
spectator = mountSpectator(worker, {
    onMode: active => { deselect(); if (active) focusOnNextData = true; else {
        worker.postMessage({type:'view'});
        worker.postMessage({type:'update_settings',settings:UI.getSettings()});
    } },
    onRefresh: () => window.chessBoardCanvas.goToNextFocus(),
});
connectWorker(onWorkerMessage);
