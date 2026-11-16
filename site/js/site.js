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
    // (only the pictures before this one go: with fast clicks a newer picture is already there, and the timer of
    // each picture removed all the others, so two timers removed both pictures and the screen was black)
    setTimeout(() => {
      while (next.isConnected && next.previousElementSibling) {
        next.previousElementSibling.remove();
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

// The trailer: one video (the sharp copy at the shape of the 3.49 screen). The background of the hero is a canvas that
// draws the frames of the same video (one download, the same moment); CSS makes it dark and soft. The button turns on
// the sound. With "reduce motion", nothing moves until the user plays the video (the poster picture stays behind).
function initReel() {
  const reel = document.querySelector('[data-reel]');
  const bg = document.querySelector('[data-reel-bg]');
  const snd = document.querySelector('[data-reel-snd]');
  if (!reel) {
    return;
  }
  const still = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (still) {
    reel.removeAttribute('autoplay');
    reel.pause();
    reel.setAttribute('controls', '');
  }
  if (bg && !still) {
    const ctx = bg.getContext('2d');
    let last = 0;
    const draw = (t) => {
      // (about 20 frames a second is enough for a soft background)
      if (!reel.paused && reel.readyState >= 2 && t - last > 45) {
        last = t;
        ctx.drawImage(reel, 0, 0, bg.width, bg.height);
        bg.parentElement.classList.add('live');
      }
      requestAnimationFrame(draw);
    };
    requestAnimationFrame(draw);
  }
  if (snd) {
    snd.addEventListener('click', () => {
      const on = reel.muted;
      reel.muted = !on;
      if (on && reel.paused) {
        reel.play().catch(() => {});
      }
      snd.setAttribute('aria-pressed', on ? 'true' : 'false');
      snd.textContent = on ? 'Sound off' : 'Sound on';
    });
  }
}

initTheme();
initShelf();
initThumbs();
initTabs();
initReel();
