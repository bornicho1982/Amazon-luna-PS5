const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'bundles');
const terms = ['Ubisoft', 'GOG', 'Epic', 'EA'];
for (const f of fs.readdirSync(dir)) {
  if (!f.includes('.js')) continue;
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  for (const t of terms) {
    let idx = 0;
    while ((idx = content.indexOf(t, idx)) !== -1) {
      console.log(f, t, content.slice(Math.max(0, idx - 50), Math.min(content.length, idx + 100)).replace(/[\r\n]+/g, ' '));
      idx += t.length;
      if (idx > content.indexOf(t) + 300) break;
    }
  }
}
