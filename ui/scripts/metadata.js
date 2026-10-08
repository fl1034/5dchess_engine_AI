// Read-only game headers, independent of the position currently being viewed.
const panel = document.getElementById('gameMetadata');
const summary = document.getElementById('gameMetadataSummary');
const details = document.getElementById('gameMetadataDetails');
const toggle = document.getElementById('gameMetadataToggle');
const icon = toggle.querySelector('path');
const expandIcon = 'M6.41421 5H10V3H3V10H5V6.41421L9.29289 10.7071L10.7071 9.29289L6.41421 5ZM21 14H19V17.5858L14.7071 13.2929L13.2929 14.7071L17.5858 19H14V21H21V14Z';
const foldIcon = 'M9.00008 4.00008H11.0001V11.0001H4.00008V9.00008H7.58586L3.29297 4.70718L4.70718 3.29297L9.00008 7.58586V4.00008ZM20 15H16.4142L20.7071 19.2929L19.2929 20.7071L15 16.4142V20H13V13H20V15Z';
let expanded = false;

function setExpanded(value) {
    expanded = value;
    panel.classList.toggle('expanded', expanded);
    summary.hidden = expanded;
    details.hidden = !expanded;
    toggle.setAttribute('aria-expanded', String(expanded));
    toggle.setAttribute('aria-label', expanded ? 'Fold game information' : 'Expand game information');
    toggle.title = toggle.getAttribute('aria-label');
    icon.setAttribute('d', expanded ? foldIcon : expandIcon);
}

toggle.addEventListener('click', () => setExpanded(!expanded));
// Native button activation must not also submit a turn or focus the board.
toggle.addEventListener('keydown', event => {
    if (event.key === 'Enter' || event.key === ' ') event.stopPropagation();
});
setExpanded(false);

// On smaller windows place this panel below the existing HUD instead of covering it.
function positionPanel() {
    const hud = document.getElementById('hudWrapper').getBoundingClientRect();
    const controls = document.getElementById('controlPanel').getBoundingClientRect();
    // Reserve the expanded width so toggling never moves the top-left anchor.
    const right = 18 + Math.min(380, window.innerWidth - 36);
    const overlapping = [hud, controls].filter(rect => rect.left < right + 12);
    const top = Math.max(18, ...overlapping.map(rect => rect.bottom + 12));
    // Keep the toggle reachable even when the other panels fill a short viewport.
    const boundedTop = Math.min(top, Math.max(18, window.innerHeight - 130));
    panel.style.top = `${boundedTop}px`;
    panel.style.maxHeight = `${Math.max(44, window.innerHeight - boundedTop - 18)}px`;
}

const layoutObserver = new ResizeObserver(positionPanel);
layoutObserver.observe(document.getElementById('hudWrapper'));
layoutObserver.observe(document.getElementById('controlPanel'));
window.addEventListener('resize', positionPanel);
// Preserve the anchor when extending; only viewport/other-panel changes reposition it.
positionPanel();

export function setGameMetadata(metadata = {}) {
    const headers = Object.fromEntries(Object.entries(metadata).map(([key, value]) => [key.toLowerCase(), value]));
    for (const element of panel.querySelectorAll('[data-game-header]')) {
        const key = element.dataset.gameHeader;
        const value = String(headers[key] ?? '').trim();
        const known = value !== '' && value !== '?';
        const isPlayer = key === 'white' || key === 'black';
        element.textContent = known ? value : (isPlayer ? 'Unknown Player' : '—');
        element.classList.toggle('game-metadata-unknown', isPlayer && !known);
        element.title = known ? value : '';
        if (key === 'result') {
            const missing = !known || value === '*';
            const showSeparator = missing && details.contains(element);
            element.hidden = missing && !showSeparator;
            element.classList.toggle('game-metadata-result-empty', showSeparator);
            if (showSeparator) element.textContent = '/';
        }
        if (key === 'event') element.hidden = !known;
    }
}
