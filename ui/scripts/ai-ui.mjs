import { MatchController } from './ai-match.mjs';
import { compactViewport } from './mobile.js';
import { MODELS, findModel } from './ai-models.mjs';

export function mountSpectator(worker, {onMode = () => {}, onRefresh = () => {}} = {}) {
    const panel = document.createElement('section');
    panel.id = 'aiPanel';
    panel.setAttribute('aria-label', 'AI spectator match');
    panel.innerHTML = `
        <h2>5D Chess · AI spectator</h2>
        <p id="aiPlayers"></p>
        <p id="aiStatus" role="status" aria-live="polite">Preparing the rules engine…</p>
        <p id="aiExplanation"></p>
        <pre id="aiMoves" aria-label="Proposed complete turn" hidden></pre>
        <p id="aiUsage"></p>
        <div class="ai-actions">
            <button id="aiPause" type="button" disabled>Pause</button>
            <button id="aiResume" type="button" hidden>Resume</button>
            <button id="aiStop" type="button" disabled>Analysis mode</button>
        </div>
        <details id="aiConfig" open>
            <summary>Players &amp; API access</summary>
            <p>Choose each player’s model. Both players have separate private conversations.</p>
            <label>White model <select id="aiWhiteModel" required><option value="">Choose White’s model…</option></select></label>
            <label>Black model <select id="aiBlackModel" required><option value="">Choose Black’s model…</option></select></label>
            <label>OpenAI API key <input id="aiKey" type="password" autocomplete="off" spellcheck="false" placeholder="Paste a key for this session"></label>
            <label>Or load local JSON (apiKey) <input id="aiKeyFile" type="file" accept=".json,application/json"></label>
            <label>Request limit per match <input id="aiLimit" type="number" min="1" max="200" value="40"></label>
            <button id="aiStart" type="button" disabled>Start new match</button>
            <p class="ai-note">Starting resets the board to Standard turn-zero. API usage is billed to your key. Keys stay in memory and are sent directly to OpenAI; they are never saved in game exports or browser storage. Use this only on a trusted local or private site.</p>
            <p class="ai-note">Pause or closing the tab stops scheduling and aborts pending browser requests. Work already accepted by OpenAI may still complete and be charged. Request limits are safeguards, not a billing cap.</p>
        </details>`;
    document.body.append(panel);
    const placePanel = () => {
        if (compactViewport.matches) {
            const shell = document.getElementById('mobileLayout');
            shell.insertBefore(panel,document.getElementById('mobileSheet'));
        } else document.body.append(panel);
    };
    placePanel();
    compactViewport.addEventListener('change',placePanel);
    const el = id => panel.querySelector('#' + id);
    for (const id of ['aiWhiteModel','aiBlackModel']) {
        for (const model of MODELS) el(id).add(new Option(model.label,model.id));
    }
    let active = true, ready = false, pendingKey = '', focusedProposal = null;
    const disabledBeforeMatch = new Map();
    const waiting = new Map();
    function updateControls() {
        const editable = ['idle','paused','finished','stopped'].includes(controller.phase);
        const chosen = findModel(el('aiWhiteModel').value) && findModel(el('aiBlackModel').value);
        const hasKey = Boolean(el('aiKey').value.trim() || pendingKey);
        el('aiStart').disabled = !ready || !editable || !chosen || !hasKey;
        el('aiKey').disabled = !editable;
        el('aiKeyFile').disabled = !editable;
        const modelsLocked = !['idle','finished','stopped'].includes(controller.phase);
        el('aiWhiteModel').disabled = modelsLocked;
        el('aiBlackModel').disabled = modelsLocked;
    }
    worker.addEventListener('message', event => {
        if (event.data.type !== 'ai_result') return;
        const pending = waiting.get(event.data.requestId);
        if (!pending) return;
        clearTimeout(pending.timer); waiting.delete(event.data.requestId);
        pending.resolve(event.data);
    });
    const rpc = message => new Promise((resolve, reject) => {
        const requestId = crypto.randomUUID();
        const timer = setTimeout(() => {
            waiting.delete(requestId);
            reject(Error('The rules engine did not acknowledge the request.'));
        }, 30000);
        waiting.set(requestId, {resolve,reject,timer});
        worker.postMessage({type:'ai',...message,requestId});
    });
    function gate() {
        document.body.classList.toggle('ai-controlled', active);
        for (const selector of ['#btnImport','#btnEdit','#hudText','.hud-text','#hudUpdateComment',
            '#hudDiscardChanges','#hintBtn','#restoreAllBranches','#confirmBranches','#allowSubmitWithChecks']) {
            for (const node of document.querySelectorAll(selector)) {
                if ('disabled' in node) {
                    if (active) {
                        if (!disabledBeforeMatch.has(node)) disabledBeforeMatch.set(node,node.disabled);
                        node.disabled = true;
                    } else if (disabledBeforeMatch.has(node)) {
                        node.disabled = disabledBeforeMatch.get(node);
                        disabledBeforeMatch.delete(node);
                    }
                }
                if (node.classList.contains('hud-text')) node.contentEditable = String(!active);
            }
        }
        const continuations = document.getElementById('selectContainer');
        if (continuations) continuations.inert = active;
        for (const name of ['undo','redo','prev','next','submit']) {
            const node = document.querySelector('button[aria-label="' + name[0].toUpperCase() + name.slice(1) + '"]')
                ?? document.querySelector('.remote-btn.' + ({prev:'up',next:'down',undo:'left',redo:'right',submit:'ok'}[name]));
            if (!node) continue;
            const allowed = !active || (name === 'submit' && controller.phase === 'proposal');
            if (active) node.classList.toggle('inactive', !allowed);
            node.disabled = active && !allowed;
            if (name === 'submit') {
                node.setAttribute('aria-label', active ? 'OK' : 'Submit');
                node.title = active ? 'OK — commit the validated AI turn' : 'Submit [↵]';
                node.querySelector('.desktop-remote-label').textContent = active ? 'OK' : 'Submit';
                const mobile = node.querySelector('.mobile-remote-label span');
                if (mobile) mobile.textContent = active ? 'OK' : 'Submit';
            }
        }
    }
    const controller = new MatchController({rpc,render:view => {
        el('aiStatus').textContent = view.message ?? '';
        el('aiExplanation').textContent = view.explanation;
        el('aiPlayers').textContent = view.models.White
            ? 'White: ' + findModel(view.models.White).label + ' · Black: ' + findModel(view.models.Black).label : '';
        const proposal = view.state?.proposal;
        if (proposal && proposal.id !== focusedProposal) {
            focusedProposal = proposal.id;
            requestAnimationFrame(onRefresh);
        }
        el('aiMoves').hidden = !proposal;
        el('aiMoves').textContent = proposal ? proposal.moves.map(m => {
            const pos = p => '(L' + p.l + 'T' + p.t + ')' + String.fromCharCode(97+p.x) + (p.y+1);
            return pos(m.from) + ' → ' + pos(m.to) + (m.promote_to ? '=' + String.fromCharCode(m.promote_to) : '');
        }).join('\n') : '';
        const u = view.usage;
        el('aiUsage').textContent = u.requests + '/' + view.limit + ' requests · ' + u.input + ' input / ' + u.output
            + ' output tokens · ~$' + u.cost.toFixed(4) + ' reported usage';
        el('aiPause').disabled = !['thinking','proposal'].includes(view.phase);
        el('aiResume').hidden = view.phase !== 'paused';
        el('aiResume').disabled = !controller.players[view.state?.side];
        el('aiStop').disabled = !ready || ['starting','committing','pausing'].includes(view.phase);
        updateControls();
        panel.dataset.phase = view.phase;
        gate();
    }});
    const fail = error => { el('aiStatus').textContent = error.message; };
    el('aiWhiteModel').addEventListener('change',updateControls);
    el('aiBlackModel').addEventListener('change',updateControls);
    el('aiKey').addEventListener('input',updateControls);
    el('aiKeyFile').addEventListener('change', async event => {
        try {
            const file = event.target.files[0];
            if (!file || file.size > 65536) throw Error('Select a small local JSON key file.');
            const value = JSON.parse(await file.text()).apiKey;
            if (typeof value !== 'string' || !value.trim()) throw Error('The JSON file needs a nonempty apiKey field.');
            pendingKey = value.trim();
            el('aiKey').value = '';
            el('aiStatus').textContent = 'Key loaded into memory. Choose both models, then start the match.';
        } catch { pendingKey = ''; fail(Error('Could not load the key. Use a JSON file with an apiKey field.')); }
        event.target.value = '';
        updateControls();
    });
    el('aiStart').addEventListener('click', async () => {
        try {
            const whiteModel = el('aiWhiteModel').value, blackModel = el('aiBlackModel').value;
            if (!findModel(whiteModel) || !findModel(blackModel)) throw Error('Choose a model for both White and Black first.');
            const key = el('aiKey').value.trim() || pendingKey;
            if (!key) throw Error('Paste a key or load your local JSON file first.');
            pendingKey = ''; el('aiKey').value = '';
            active = true; onMode(true);
            el('aiConfig').open = false;
            await controller.start({key,whiteModel,blackModel,limit:Number(el('aiLimit').value)});
        } catch (error) { fail(error); }
    });
    el('aiPause').addEventListener('click', () => controller.pause().catch(fail));
    el('aiResume').addEventListener('click', () => controller.resume(Number(el('aiLimit').value)).catch(fail));
    el('aiStop').addEventListener('click', async () => {
        try { await controller.stop(); active = false; pendingKey = ''; el('aiKey').value = ''; gate(); onMode(false); }
        catch (error) { fail(error); }
    });
    const dispose = () => { pendingKey = ''; el('aiKey').value = ''; controller.dispose(); };
    window.addEventListener('pagehide', dispose);
    window.addEventListener('beforeunload', dispose);
    window.addEventListener('pageshow', event => {
        if (event.persisted && active) controller.pause('Page restored. Load your key and start a new match; pending requests were cancelled.').catch(fail);
    });
    gate();
    return {
        get active() { return active; },
        gate,
        boot:async () => { try { await controller.boot(); ready = true; controller.emit(); } catch (error) { fail(error); } },
        ok:() => controller.ok().catch(fail),
    };
}
