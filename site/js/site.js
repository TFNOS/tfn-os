// The small parts of the pages: the theme button, the screens of the shelf, the screen choice of each board card and
// the app tabs. The pages work with no script; the script only adds these parts.

const SHOTS = 'img/screens/';
const CYCLE_MS = 3600;
const THEMES = ['auto', 'light', 'dark'];

function shotUrl(name) {
  return SHOTS + name + '.webp';
}

function savedTheme() {
  try {
    return localStorage.getItem('tfnos-theme') || 'auto';
  } catch (e) {
    return 'auto';
  }
}

function applyTheme(t) {
  if (t === 'auto') {
    document.documentElement.removeAttribute('data-theme');
  } else {
    document.documentElement.setAttribute('data-theme', t);
  }
  try {
    localStorage.setItem('tfnos-theme', t);
  } catch (e) {
    // (no storage: the choice stays for this page only)
  }
  for (const b of document.querySelectorAll('[data-theme-btn]')) {
    b.textContent = t[0].toUpperCase() + t.slice(1);
    b.setAttribute('aria-label', 'Colour theme: ' + t + '. Click to change.');
  }
}

export function initTheme() {
  applyTheme(savedTheme());
  for (const b of document.querySelectorAll('[data-theme-btn]')) {
    b.addEventListener('click', () => {
      applyTheme(THEMES[(THEMES.indexOf(savedTheme()) + 1) % THEMES.length]);
    });
  }
}

// Show the next picture: a second img fades in over the first one, then takes its place.
function showShot(screen, name) {
  const cur = screen.querySelector('img:last-of-type');
  if (!cur || cur.dataset.shot === name) {
    return;
  }
  const next = cur.cloneNode();
  next.dataset.shot = name;
  next.classList.add('off');
  next.src = shotUrl(name);
  next.alt = '';
  next.onload = () => {
    requestAnimationFrame(() => {
      next.classList.remove('off');
    });
    setTimeout(() => {
      for (const old of screen.querySelectorAll('img')) {
        if (old !== next) {
          old.remove();
        }
      }
    }, 800);
  };
  screen.appendChild(next);
}

function initShelf() {
  const still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const figs = Array.from(document.querySelectorAll('[data-shots]'));
  if (still || !figs.length) {
    return;
  }
  const visible = new Set();
  const io = new IntersectionObserver((entries) => {
    for (const e of entries) {
      if (e.isIntersecting) {
        visible.add(e.target);
      } else {
        visible.delete(e.target);
      }
    }
  });
  figs.forEach((f) => io.observe(f));
  let tick = 0;
  setInterval(() => {
    tick++;
    figs.forEach((f, i) => {
      if (!visible.has(f) || document.hidden || (tick + i) % 2) {
        return;
      }
      const list = f.dataset.shots.split(' ');
      const step = Math.floor((tick + i) / 2) % list.length;
      showShot(f.querySelector('.dev-screen'), list[step]);
    });
  }, CYCLE_MS / 2);
}

function initThumbs() {
  for (const box of document.querySelectorAll('[data-thumbs]')) {
    const screen = box.parentElement.querySelector('.dev-screen');
    const names = box.dataset.thumbs.split(' ');
    names.forEach((name, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.setAttribute('aria-pressed', i === 0 ? 'true' : 'false');
      b.setAttribute('aria-label', 'Screen ' + (i + 1) + ' of ' + names.length);
      const im = document.createElement('img');
      im.src = shotUrl(name);
      im.alt = '';
      im.loading = 'lazy';
      b.appendChild(im);
      b.addEventListener('click', () => {
        box.querySelectorAll('button').forEach((x) => x.setAttribute('aria-pressed', 'false'));
        b.setAttribute('aria-pressed', 'true');
        showShot(screen, name);
      });
      box.appendChild(b);
    });
  }
}

function initTabs() {
  const tabs = Array.from(document.querySelectorAll('[role="tab"]'));
  const pick = (tab) => {
    for (const t of tabs) {
      const on = t === tab;
      t.setAttribute('aria-selected', on ? 'true' : 'false');
      t.tabIndex = on ? 0 : -1;
      document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
    }
  };
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => pick(t));
    t.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
        const n = tabs[(i + (e.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length];
        pick(n);
        n.focus();
      }
    });
  });
}

initTheme();
initShelf();
initThumbs();
initTabs();
