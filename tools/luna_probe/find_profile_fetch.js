const fs = require('fs');
const content = fs.readFileSync('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'utf8');

let p = 0;
while ((p = content.indexOf('getAppConfig', p)) !== -1) {
  console.log(content.slice(Math.max(0, p - 50), Math.min(content.length, p + 200)));
  console.log('---');
  p += 12;
}
