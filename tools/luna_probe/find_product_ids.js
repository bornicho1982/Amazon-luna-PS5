const fs = require('fs');
const path = require('path');
const dir = 'tools/luna_probe/bundles';

const productIds = new Set();
for (const f of fs.readdirSync(dir)) {
  const content = fs.readFileSync(path.join(dir, f), 'utf8');
  const re = /amzn1\.luna\.product\.[a-zA-Z0-9_-]+/g;
  let m;
  while ((m = re.exec(content)) !== null) {
    productIds.add(m[0]);
  }
}

console.log('Product IDs found in bundles:', Array.from(productIds));
