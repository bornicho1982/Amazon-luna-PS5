const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  const idx = content.indexOf('setTdiHeader(');
  if (idx !== -1) {
    console.log('setTdiHeader in', f, ':', content.slice(idx - 50, idx + 200));
  }
}
