const https = require('https');

function testUA(ua) {
  const req = https.request({
    hostname: 'proxy-prod.tempo.digital.a2z.com',
    port: 443,
    path: '/createGameSessionJob',
    method: 'POST',
    headers: {
      'User-Agent': ua,
      'Content-Type': 'application/json',
      'Content-Length': 2
    }
  }, (res) => {
    let d = '';
    res.on('data', c => d += c);
    res.on('end', () => console.log(`[UA: ${ua}] -> HTTP ${res.statusCode}: ${d}`));
  });
  req.on('error', e => console.error(ua, e.message));
  req.write('{}');
  req.end();
}

testUA('Luna-Client/1.0');
testUA('Mozilla/5.0 (Web0S; Linux/SmartTV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.119 Safari/537.36 WebAppManager');
