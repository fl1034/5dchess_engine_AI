// main.js

import { UI } from 'ui';
import ChessBoardCanvasBase from 'draw';
import { compactViewport, initMobileLayout } from './mobile.js';
import { createSelection } from './selection.js';

initMobileLayout();


let worker = new Worker(window.worker_path, { type: 'module' });

// State management for move selection
let presentC;
let nextOptions = [];
let showPhantom = true;
let focusOnNextData = true;
let committedData = null;

class ChessBoardCanvas extends ChessBoardCanvasBase {
    onClickSquare(l, t, c, x, y) {
        selection.click({ l, t, c, x, y });
    }
    
    onPreviewSquare(pos) { return selection.preview(pos); }
    onCommitSquare(token) { selection.commit(token); }
    onCancelSquare(token) { selection.cancel(token); }

    onRightClickSquare() {
        deselect();
    }
}

window.chessBoardCanvas = new ChessBoardCanvas('canvas');
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


worker.onmessage = (e) => {
    const msg = e.data;
    if (msg.type === 'ready') {
        // Send current export options to worker on startup
        worker.postMessage({
            type: 'update_settings', 
            settings: UI.getSettings()
        });
        worker.postMessage({type: 'load', pgn: '[Board "Standard - Turn Zero"]'});
    }
    else if (msg.type === 'engine_version') {
        // Update the version in the Information popup and welcome popup
        UI.setVersionNumber(msg.version);
        console.log('Version', msg.version);
        // Show info popup on startup if enabled
        UI.showInfoPage();
    }
    else if (msg.type === 'alert') {
        alert('[WORKER] ' + msg.message);
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
        if (msg.previewId == null && focusOnNextData && compactViewport.matches) window.chessBoardCanvas.goToNextFocus();
        if (msg.previewId == null) focusOnNextData = false;
    }
    else if (msg.type === 'moves') {
        selection.receiveMoves(msg);
    }
    else if (msg.type === 'update_buttons')
    {
        UI.setHintAvailable(msg.hint);
        for(let key of ['undo', 'redo', 'prev', 'next', 'submit'])
        {
            if(msg[key])
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
        let options = msg.options.map(obj => obj.pgn);
        nextOptions = msg.options.map(obj => obj.action);
        const selectedIndex = UI.select.getSelectedIndex();
        UI.select.setOptions(options);
        if (msg.preserveSelection) {
            UI.select.setSelectedIndex(selectedIndex);
        } else if (msg.selectedIndex !== undefined) {
            UI.select.setSelectedIndex(msg.selectedIndex);
        }
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
}

UI.buttons.setCallbacks({
    prev: () => {
        deselect();
        worker.postMessage({type: 'prev'});
        disablePhantom();
    },
    next: () => {
        deselect();
        let index = UI.select.getSelectedIndex();
        let action = nextOptions[index];
        worker.postMessage({type: 'next', action: action});
        disablePhantom();
    },
    undo: () => {
        deselect();
        worker.postMessage({type: 'undo'});
    },
    redo: () => {
        deselect();
        worker.postMessage({type: 'redo'});
    },
    submit: () => {
        deselect();
        worker.postMessage({type: 'submit'});
        disablePhantom();
    }
});

UI.setHintCallback(() => {
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

UI.setImportCallback((data) => {
    deselect();
    focusOnNextData = true;
    worker.postMessage({type: 'load', pgn: data});
});

UI.setExportCallback((requestId) => {
    worker.postMessage({type: 'export', requestId});
});

UI.setCommentsEditCallback((text) => {
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
