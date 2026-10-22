const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'utf8');

const target = 'getPerRequestHeaders(e){';
const idx = content.indexOf(target);
if (idx !== -1) {
  console.log(content.slice(idx, idx + 2500));
}
