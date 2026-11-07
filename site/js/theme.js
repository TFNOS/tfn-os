// The colour theme before the first paint: "light" or "dark" from the last choice, else the system theme.
(function () {
  var t = null;
  try {
    t = localStorage.getItem('tfnos-theme');
  } catch (e) {
    t = null;
  }
  if (t === 'light' || t === 'dark') {
    document.documentElement.setAttribute('data-theme', t);
  }
})();
