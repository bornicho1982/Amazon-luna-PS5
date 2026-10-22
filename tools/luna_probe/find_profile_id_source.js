const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'utf8');

const idx = content.indexOf('HardCodedPageType=');
if (idx !== -1) {
  console.log(content.slice(idx, idx + 1000));
} else {
  const p = content.indexOf('HardCodedPageType');
  console.log(content.slice(p, p + 1000));
}
