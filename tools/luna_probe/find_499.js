const https = require('https');

const endpoints = [
  'proxy-prod.tempo.digital.a2z.com',
  'proxy-prod.eu-west-1.tempo.digital.a2z.com'
];

function testCombo(host, name, headers, body) {
  const payload = JSON.stringify(body);
  const req = https.request({
    hostname: host,
    port: 443,
    path: '/createGameSessionJob',
    method: 'POST',
    headers: Object.assign({
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload)
    }, headers)
  }, (res) => {
    let d = '';
    res.on('data', chunk => d += chunk);
    res.on('end', () => {
      if (res.statusCode === 499 || res.statusCode !== 403) {
        console.log(`*** HIT *** [${host}] ${name} -> HTTP ${res.statusCode}: '${d}'`);
      } else {
        console.log(`[${host}] ${name} -> HTTP ${res.statusCode}: '${d}'`);
      }
    });
  });
  req.on('error', e => console.error(name, e.message));
  req.write(payload);
  req.end();
}

for (const host of endpoints) {
  testCombo(host, 'no headers', {}, {});
  testCombo(host, 'smart_tv_web LG TV', {
    'x-amz-access-token': 'Atnr|fake',
    'x-amz-platform': 'smart_tv_web',
    'x-amz-device-type': 'A2U9WJDSJD7QG9',
    'x-amz-device-model': 'LG TV'
  }, {
    gameId: 'amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16',
    signalingRequest: JSON.stringify({ type: 'offer', sdp: 'v=0...' })
  });
  testCombo(host, 'web Chrome', {
    'x-amz-access-token': 'Atnr|fake',
    'x-amz-platform': 'web',
    'x-amz-device-type': 'A1PY8QQU9P0WJV'
  }, {
    gameId: 'amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16',
    signalingRequest: JSON.stringify({ type: 'offer', sdp: 'v=0...' })
  });
}
