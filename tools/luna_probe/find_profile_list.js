const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/ynBYY5UuOxPBd0D.js_name_initial_app-730a55cb', 'utf8');

let idx = 0;
while ((idx = content.indexOf('loginProfile(', idx)) !== -1) {
  console.log(content.slice(Math.max(0, idx - 100), Math.min(content.length, idx + 200)));
  console.log('---');
  idx += 13;
}
