const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

const set = new Set();
for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  const re = /https?:\/\/[a-zA-Z0-9.-]+\/[a-zA-Z0-9_\/.-]*(?:profile|player)[a-zA-Z0-9_\/.-]*/gi;
  let m;
  while ((m = re.exec(content)) !== null) {
    set.add(m[0]);
  }
}
console.log('URLs with profile/player:', Array.from(set));
