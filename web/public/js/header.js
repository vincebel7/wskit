(function () {
    const cfg = window._headerConfig || {};
    const darkModeKey = 'weather-dark-mode';

    if (cfg.title) document.title = cfg.title + ' \u2014 wskit';

    const isDark = localStorage.getItem(darkModeKey) === 'true';
    if (isDark) document.body.classList.add('dark-mode');

    const navLinks = [
        { href: '/',           label: 'Live',       key: 'live' },
        { href: '/history',    label: 'History',    key: 'history' },
        { href: '/collectors', label: 'Collectors', key: 'collectors' },
    ];
    const navHtml = navLinks
        .map(l => `<a href="${l.href}"${l.key === cfg.nav ? ' class="active"' : ''}>${l.label}</a>`)
        .join('\n    ');

    document.getElementById('page-header').innerHTML =
        `<h1 class="title">${cfg.icon ? cfg.icon + ' ' : ''}${cfg.title || 'wskit'}</h1>` +
        `<button id="toggle-dark" style="margin: 0 auto; display: block;">${isDark ? 'Light Mode' : 'Dark Mode'}</button>` +
        `<br><nav>\n    ${navHtml}\n</nav>` +
        `<a class="github-link" href="https://github.com/vincebel7/wskit" target="_blank">GitHub</a>` +
        `<hr>`;

    document.getElementById('toggle-dark').onclick = function () {
        document.body.classList.toggle('dark-mode');
        const dark = document.body.classList.contains('dark-mode');
        this.textContent = dark ? 'Light Mode' : 'Dark Mode';
        localStorage.setItem(darkModeKey, dark);
    };
}());
