// The store page: the requests of the store index, its signature and the media lists start at once, while the
// modules of the page still load (a request to the store takes most of the wait). js/store/ui.js takes each response
// one time (takeEarly); a failed request here is asked again there.
(function () {
  const urls = ['store/index.json', 'store/index.sig', 'media/apps/media.json', 'media/apps/icons.json'];
  const early = {};
  for (const u of urls) {
    early[u] = fetch(u, { cache: u.indexOf('store/') === 0 ? 'no-store' : 'no-cache' }).catch(function () {
      return null;
    });
  }
  window.tfnStoreEarly = early;
})();
