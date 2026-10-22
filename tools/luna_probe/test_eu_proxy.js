const https = require('https');

function test(host, name, path, body, headers) {
  const payload = JSON.stringify(body);
  const req = https.request({
    hostname: host,
    port: 443,
    path: path,
    method: 'POST',
    headers: Object.assign({
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload)
    }, headers)
  }, (res) => {
    let data = '';
    res.on('data', chunk => data += chunk);
    res.on('end', () => console.log(`[${host}] ${name} -> STATUS: ${res.statusCode} BODY: ${data.slice(0, 300)}`));
  });
  req.on('error', err => console.error(name, 'ERR:', err));
  req.write(payload);
  req.end();
}

test('proxy-prod.eu-west-1.tempo.digital.a2z.com', 'createGameSessionJob dummy', '/createGameSessionJob', {
  gameId: 'amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16'
}, {
  'x-amz-access-token': 'dummy'
});

test('proxy-prod.eu-west-1.tempo.digital.a2z.com', 'getIotConnectionInfo dummy', '/getIotConnectionInfo', {}, {
  'x-amz-access-token': 'dummy'
});
