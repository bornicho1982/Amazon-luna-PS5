const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'utf8');

const targets = ['createPlayerFromProfile', 'createPlayer', 'setPlayer', 'generatePlayerName'];
for (const t of targets) {
  const idx = content.indexOf(t + '(');
  if (idx !== -1) {
    console.log(`=== ${t} ===`);
    console.log(content.slice(idx - 50, idx + 400));
  }
}
