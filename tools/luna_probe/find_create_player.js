const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('proxyServiceClient.', idx)) !== -1) {
    const s = content.slice(idx, idx + 60);
    if (s.includes('Player') || s.includes('player') || s.includes('Profile') || s.includes('profile')) {
      console.log(`[${f}]:`, s);
    }
    idx += 19;
  }
}
