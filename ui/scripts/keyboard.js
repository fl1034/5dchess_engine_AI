// Route shortcuts without stealing text entry, native control activation, or browser commands.
export function createKeyboardShortcuts({ onAction, isBlocked, now = () => performance.now() }) {
    const actions = {
        ArrowUp: 'prev', ArrowDown: 'next', ArrowLeft: 'undo', ArrowRight: 'redo',
        Enter: 'submit', h: 'hint', f: 'focus',
        w: 'up', a: 'left', s: 'down', d: 'right'
    };
    const directions = new Set(['up', 'left', 'down', 'right']);
    let lastSpace = null;
    let lastDirectionTime = -Infinity;

    function reset() {
        lastSpace = null;
        lastDirectionTime = -Infinity;
    }

    function handleKeyDown(event) {
        const target = event.target;
        const editing = target?.isContentEditable
            || target?.closest?.('input, textarea, select, [role="textbox"], [role="combobox"]');
        const nativeActivation = target?.closest?.('button, a[href], summary, [role="button"]')
            && (event.key === 'Enter' || event.key === ' ');
        if (event.defaultPrevented || event.isComposing || event.keyCode === 229
            || event.metaKey || event.ctrlKey || event.altKey || editing
            || nativeActivation || isBlocked()) {
            reset();
            return;
        }
        const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
        if (key === ' ') {
            event.preventDefault();
            if (event.repeat) return;
            const time = now();
            const doubleSpace = !event.shiftKey && lastSpace !== null && time - lastSpace <= 300;
            onAction('nearby', { wide: event.shiftKey || doubleSpace, reuse: doubleSpace });
            lastSpace = event.shiftKey || doubleSpace ? null : time;
            return;
        }
        lastSpace = null;
        const action = actions[key];
        if (!action) return;
        event.preventDefault();
        if (event.repeat && action === 'focus') return;
        if (directions.has(action)) {
            const time = now();
            if (event.repeat && time - lastDirectionTime < 120) return;
            lastDirectionTime = time;
        }
        onAction(action);
    }
    return { handleKeyDown, reset };
}
