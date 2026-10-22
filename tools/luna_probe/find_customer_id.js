const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('setCustomerId(', idx)) !== -1) {
    console.log(`[${f}] @ ${idx}:`);
    console.log(content.slice(Math.max(0, idx - 150), Math.min(content.length, idx + 250)));
    console.log('-----------------------------------------');
    idx += 'setCustomerId('.length;
  }
}
