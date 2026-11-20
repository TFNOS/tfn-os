// The small parts of the pages: the theme button, the short videos of the game screens (they play only while a mouse
// points at a screen) and the trailer (it loads only after a click). The pages work with no script; the script only
// adds these parts.

const THEMES = ['auto', 'light', 'dark'];

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

// True when the page can play a short video for a pointer: a mouse (a phone has no hover), no "reduce motion", and
// no "save data" of the browser.
export function clipsOk() {
  const m = (q) => window.matchMedia(q).matches;
  const saveData = navigator.connection && navigator.connection.saveData;
  return m('(hover: hover) and (pointer: fine)') && !m('(prefers-reduced-motion: reduce)') && !saveData;
}

// A screen with data-clip="<the mp4 file>" gets a muted video over its picture while the mouse points at it. The video
// loads at the first point (about 20 to 200 KB), not with the page.
export function addClip(box, src) {
  let video = null;
  box.addEventListener('pointerenter', () => {
    if (!video) {
      video = document.createElement('video');
      video.muted = true;
      video.loop = true;
      video.playsInline = true;
      video.preload = 'auto';
      video.setAttribute('aria-hidden', 'true');
      video.src = src;
      video.addEventListener('playing', () => video.classList.add('on'));
      box.appendChild(video);
    }
    video.play().catch(() => {});
  });
  box.addEventListener('pointerleave', () => {
    if (video) {
      video.pause();
      video.classList.remove('on');
    }
  });
}

// The hero screen (data-hero="<the mp4 file>"): the video of the games plays in a loop over its poster. No video with
// "reduce motion" or "save data": the poster stays.
function initHero() {
  const box = document.querySelector('[data-hero]');
  const m = (q) => window.matchMedia(q).matches;
  const saveData = navigator.connection && navigator.connection.saveData;
  if (!box || m('(prefers-reduced-motion: reduce)') || saveData) {
    return;
  }
  const video = document.createElement('video');
  video.muted = true;
  video.loop = true;
  video.playsInline = true;
  video.autoplay = true;
  video.setAttribute('aria-hidden', 'true');
  video.src = box.dataset.hero;
  video.addEventListener('playing', () => video.classList.add('on'));
  box.appendChild(video);
  video.play().catch(() => {});
}

function initClips() {
  if (!clipsOk()) {
    return;
  }
  for (const box of document.querySelectorAll('[data-clip]')) {
    addClip(box, box.dataset.clip);
  }
}

// The trailer: the link opens a window with the video; the video file loads only now. With no script, the link
// opens the video file.
function initTrailer() {
  const dlg = document.getElementById('trailer');
  if (!dlg || typeof dlg.showModal !== 'function') {
    return;
  }
  const video = dlg.querySelector('video');
  for (const a of document.querySelectorAll('[data-trailer]')) {
    a.addEventListener('click', (e) => {
      e.preventDefault();
      if (!video.getAttribute('src')) {
        // (the poster too loads only now: a poster in the page loads with the page)
        if (video.dataset.poster) {
          video.poster = video.dataset.poster;
        }
        video.src = a.getAttribute('href');
      }
      dlg.showModal();
      video.play().catch(() => {});
    });
  }
  const close = () => dlg.close();
  for (const b of dlg.querySelectorAll('[data-trailer-close]')) {
    b.addEventListener('click', close);
  }
  dlg.addEventListener('click', (e) => {
    if (e.target === dlg) {
      close();
    }
  });
  dlg.addEventListener('close', () => video.pause());
}

initTheme();
initHero();
initClips();
initTrailer();
