const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('profileId', idx)) !== -1) {
    const s = content.slice(Math.max(0, idx - 100), Math.min(content.length, idx + 150));
    if (s.includes('customer') || s.includes('directedId') || s.includes('Account') || s.includes('Amazon') || s.includes('default')) {
      console.log(`[${f}]:`, s.trim());
    }
    idx += 9;
  }
}
