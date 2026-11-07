// test.html: run the flasher checks in the browser and show one row for each scenario.
import { SCENARIOS } from '../js/flasher/scenarios.js';
import { runAll } from './run.js';

function cell(tr, text, href) {
  const td = document.createElement('td');
  if (href) {
    const a = document.createElement('a');
    a.href = href;
    a.textContent = text;
    td.appendChild(a);
  } else {
    td.textContent = text;
  }
  tr.appendChild(td);
}

const results = await runAll();
const body = document.getElementById('results');
let bad = 0;
for (const r of results) {
  if (r.fails.length) {
    bad++;
  }
  const tr = document.createElement('tr');
  cell(tr, r.fails.length ? 'FAIL' : 'PASS');
  cell(tr, r.label);
  cell(tr, r.fails.join('; ') || '-');
  cell(tr, SCENARIOS[r.name] ? 'install page' : '', SCENARIOS[r.name] ? 'install.html?test=' + r.name : '');
  body.appendChild(tr);
}
const summary = bad ? 'FLASHER TESTS FAILED: ' + bad : 'FLASHER TESTS OK (' + results.length + ')';
document.getElementById('summary').textContent = summary;
