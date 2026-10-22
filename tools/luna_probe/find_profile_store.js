const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  const idx = content.indexOf('profileStore');
  if (idx !== -1) {
    console.log('profileStore found in', f);
    // Find where profileStore methods are defined
    let pos = 0;
    while ((pos = content.indexOf('profileStore', pos)) !== -1) {
      console.log(content.slice(Math.max(0, pos - 50), Math.min(content.length, pos + 100)));
      console.log('---');
      pos += 12;
      if (pos > idx + 2000) break;
    }
  }
}
