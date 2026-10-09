// One set of game controls, reparented when a phone-sized viewport needs a compact layout.
export const compactViewport = window.matchMedia('(max-width: 600px), (max-width: 960px) and (max-height: 500px)');

export function initMobileLayout() {
    const shell = document.getElementById('mobileLayout');
    const sheet = document.getElementById('mobileSheet');
    const heading = document.getElementById('mobileSheetTitle');
    const panes = [...shell.querySelectorAll('[data-mobile-pane]')];
    const triggers = [...shell.querySelectorAll('[data-mobile-panel]')];
    const commentButton = document.getElementById('mobileCommentButton');
    const commentIcon = commentButton.querySelector('use');
    const commentText = document.querySelector('.hud-text');
    const metadataIcon = shell.querySelector('.mobile-game-button use');
    const metadata = document.getElementById('gameMetadata');
    const metadataToggle = document.getElementById('gameMetadataToggle');
    const focus = document.getElementById('btnFocus');
    const buttons = document.getElementById('buttonsPanel');
    const placements = [];
    let activePanel = null;
    let returnFocus = null;
    let metadataWasExpanded = false;

    function move(element, destination) {
        const marker = document.createComment('desktop placement');
        element.before(marker);
        placements.push({ element, marker });
        destination.appendChild(element);
    }

    function updateCommentIcon() {
        const hasComment = commentText.textContent.trim().length > 0;
        const icon = hasComment ? 'comment-message' : 'comment-empty';
        commentIcon.setAttribute('href', `#icon-${icon}${activePanel === 'comments' ? '-open' : ''}`);
        commentButton.setAttribute('aria-label', hasComment ? 'Position comment (has comment)' : 'Position comment (empty)');
    }

    // Cover typing, discard, saving, and comments arriving from the game worker.
    const commentObserver = new MutationObserver(updateCommentIcon);
    commentObserver.observe(commentText, { childList: true, characterData: true, subtree: true });
    commentText.addEventListener('input', updateCommentIcon);
    updateCommentIcon();

    function showPanel(name, trigger = null) {
        activePanel = name;
        shell.classList.toggle('has-mobile-panel', Boolean(name));
        metadataIcon.setAttribute('href', name === 'game' ? '#icon-price-tag-3-fill' : '#icon-price-tag-3-line');
        updateCommentIcon();
        if (trigger) returnFocus = trigger;
        sheet.hidden = !name;
        panes.forEach(pane => { pane.hidden = pane.dataset.mobilePane !== name; });
        triggers.forEach(button => button.setAttribute('aria-expanded', String(button.dataset.mobilePanel === name)));
        heading.hidden = name === 'tools';
        heading.textContent = { variations: 'Continuations', comments: 'Position comment', game: 'Game details' }[name] || '';
        if (name === 'tools') sheet.removeAttribute('aria-labelledby');
        else sheet.setAttribute('aria-labelledby', 'mobileSheetTitle');
        if (name && trigger) document.getElementById('mobileSheetClose').focus();
    }

    function closePanel() {
        showPanel(null);
        if (returnFocus?.isConnected) returnFocus.focus();
    }

    triggers.forEach(button => button.addEventListener('click', () => {
        showPanel(activePanel === button.dataset.mobilePanel ? null : button.dataset.mobilePanel, button);
    }));
    document.getElementById('mobileSheetClose').addEventListener('click', closePanel);
    shell.addEventListener('keydown', event => {
        // Native controls must not also activate the application's game shortcuts.
        if ((event.key === 'Enter' || event.key === ' ') && event.target.closest('button')) event.stopPropagation();
        if (event.key === 'Escape' && activePanel) {
            closePanel();
            event.stopPropagation();
        }
    });

    document.addEventListener('metadata-visibility-change', event => {
        if (!event.detail.visible && activePanel === 'game') closePanel();
    });

    // Reuse the existing icon and title for a labeled, touch-sized tools menu.
    [...buttons.children].filter(button => button !== focus).forEach(button => {
        const label = document.createElement('span');
        label.className = 'mobile-tool-label';
        label.textContent = button.title;
        button.appendChild(label);
        button.setAttribute('aria-label', button.title);
    });
    focus.setAttribute('aria-label', 'Focus board');

    function updateLayout() {
        const compact = compactViewport.matches;
        document.body.classList.toggle('compact-layout', compact);
        shell.hidden = !compact;
        if (compact && placements.length === 0) {
            metadataWasExpanded = metadata.classList.contains('expanded');
            if (!metadataWasExpanded) metadataToggle.click();
            const destinations = [
                ['.hud-header', '#mobileStatus'],
                ['#canvas', '#mobileBoard'],
                ['#btnFocus', '#mobileBoard'],
                ['.remote-control', '#mobileNavigation'],
                ['#selectedDisplay', '#mobileContinuation'],
                ['#selectContainer', '[data-mobile-pane="variations"]'],
                ['.hud-content', '[data-mobile-pane="comments"]'],
                ['#hudCommentActions', '[data-mobile-pane="comments"]'],
                ['#buttonsPanel', '[data-mobile-pane="tools"]'],
                ['#gameMetadata', '[data-mobile-pane="game"]']
            ];
            destinations.forEach(([source, target]) => move(document.querySelector(source), shell.querySelector(target)));
        } else if (!compact && placements.length) {
            showPanel(null);
            for (const { element, marker } of placements.reverse()) marker.replaceWith(element);
            placements.length = 0;
            if (!metadataWasExpanded) metadataToggle.click();
            // Desktop positioning depends on the restored elements' dimensions.
            window.dispatchEvent(new Event('resize'));
        }
    }
    compactViewport.addEventListener('change', updateLayout);
    updateLayout();
}
