const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('getClientContext(t', idx)) !== -1) {
    console.log(`[${f}] getClientContext(t:`, content.slice(idx - 20, idx + 400));
    idx += 20;
  }
}
