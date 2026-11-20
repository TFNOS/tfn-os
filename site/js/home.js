// The front page: the board of the hero tilts a few degrees after the mouse (TFN OS is made for tilt). No tilt with
// "reduce motion" or on a touch screen.
const art = document.querySelector('.hero-art[data-tilt]');
const mq = (q) => window.matchMedia(q).matches;
if (art && !mq('(prefers-reduced-motion: reduce)') && mq('(hover: hover) and (pointer: fine)')) {
  const dev = art.querySelector('.dev');
  const hero = document.querySelector('.home-hero') || art;
  hero.addEventListener('pointermove', (e) => {
    const r = art.getBoundingClientRect();
    const dx = Math.max(-1, Math.min(1, (e.clientX - (r.left + r.width / 2)) / (r.width / 1.2)));
    const dy = Math.max(-1, Math.min(1, (e.clientY - (r.top + r.height / 2)) / (r.height * 2)));
    dev.style.setProperty('--ry', (dx * 9).toFixed(2) + 'deg');
    dev.style.setProperty('--rx', (-dy * 7).toFixed(2) + 'deg');
  });
  hero.addEventListener('pointerleave', () => {
    dev.style.setProperty('--ry', '0deg');
    dev.style.setProperty('--rx', '0deg');
  });
}
