(function () {
    const cfg = window._headerConfig || {};

    if (cfg.title) document.title = cfg.title + ' — wskit';

    const navLinks = [
        { href: '/',         label: 'Live',     key: 'live' },
        { href: '/history',  label: 'History',  key: 'history' },
        { href: '/settings', label: 'Settings', key: 'settings' },
    ];
    const navHtml = navLinks
        .map(l => `<a href="${l.href}"${l.key === cfg.nav ? ' class="active"' : ''}>${l.label}</a>`)
        .join('\n    ');

    document.getElementById('page-header').innerHTML =
        `<header class="site-header">` +
          `<div class="site-header-inner">` +
            `<a class="brand" href="/">wskit</a>` +
            `<nav>${navHtml}</nav>` +
            `<div class="header-controls">` +
              `<a class="github-link" href="https://github.com/vincebel7/wskit" target="_blank">GitHub</a>` +
            `</div>` +
          `</div>` +
        `</header>` +
        (cfg.title ? `<h1 class="title">${cfg.icon ? cfg.icon + ' ' : ''}${cfg.title}</h1>` : '');
}());
