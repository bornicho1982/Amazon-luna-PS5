const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('setDefaultProfileData', idx)) !== -1) {
    console.log(`[${f}] setDefaultProfileData:`, content.slice(Math.max(0, idx - 100), Math.min(content.length, idx + 250)));
    idx += 21;
  }
}
