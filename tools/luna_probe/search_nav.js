const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'bundles');
const files = fs.readdirSync(dir);

function inspectBundle(filename, terms) {
  const content = fs.readFileSync(path.join(dir, filename), 'utf8');
  for (const t of terms) {
    let idx = 0;
    console.log(`=== In ${filename} search: ${t} ===`);
    while ((idx = content.indexOf(t, idx)) !== -1) {
      console.log(content.slice(Math.max(0, idx - 150), Math.min(content.length, idx + 250)).replace(/[\r\n]+/g, ' '));
      console.log('---');
      idx += t.length;
      if (idx > content.indexOf(t) + 1000) break; // just first few
    }
  }
}

inspectBundle('K8SKn-RxBBUBcPb.js_name_initial_app-7413f8ad', ['NavItemOverride', 'remote_play_dark_blue']);
