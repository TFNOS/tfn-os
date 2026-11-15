// The statistics of the site, to the site itself (no third party, no cookie). Only a page with
// <meta name="tfn-stats" content="/s"> sends them: the page sends GET <content>?ev=<event>&v=<visit>&... and the web
// server writes the request to its log (site/README.md "Statistics"). The visit is a random ID of this page load; it
// links the events of one install. A page with no tag sends nothing.
const META = document.querySelector('meta[name="tfn-stats"]');
const URL_BASE = META && META.content ? META.content : '';
const VISIT = URL_BASE ? Math.random().toString(36).slice(2, 10) : '';

export function stat(ev, data) {
  if (!URL_BASE) {
    return;
  }
  const q = new URLSearchParams({ ev, v: VISIT });
  for (const [k, val] of Object.entries(data || {})) {
    if (val !== undefined && val !== null && val !== '') {
      q.set(k, String(val).slice(0, 160));
    }
  }
  try {
    fetch(URL_BASE + '?' + q.toString(), { method: 'GET', keepalive: true, cache: 'no-store', credentials: 'same-origin' })
      .catch(() => {});
  } catch (e) {
    // (a failed statistic never stops the page)
  }
}

// a short ID of a board (the first 8 hex digits of the SHA-256 of its MAC): the same board gives the same ID, and the
// MAC itself does not go to the server
export async function boardId(mac) {
  if (!mac || !(globalThis.crypto && crypto.subtle)) {
    return '';
  }
  const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(String(mac).toLowerCase()));
  return Array.from(new Uint8Array(d).slice(0, 4), (b) => b.toString(16).padStart(2, '0')).join('');
}
