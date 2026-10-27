const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'bundles');
const files = fs.readdirSync(dir);

for (const f of files) {
  if (!f.includes('.js')) continue;
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  let idx = 0;
  while ((idx = content.indexOf('"game_detail"', idx)) !== -1) {
    console.log(f + ': ' + content.slice(Math.max(0, idx - 80), Math.min(content.length, idx + 180)).replace(/[\r\n]+/g, ' '));
    idx += 13;
  }
}
