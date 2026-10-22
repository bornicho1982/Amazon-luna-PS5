const https = require('https');

function escapeJson(str) {
    let out = '';
    for (let i = 0; i < str.length; i++) {
        const c = str[i];
        if (c === '"') out += '\\"';
        else if (c === '\\') out += '\\\\';
        else if (c === '\r') out += '\\r';
        else if (c === '\n') out += '\\n';
        else if (c === '\t') out += '\\t';
        else out += c;
    }
    return out;
}

const sdpOffer = "v=0\r\no=- 12345 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n";

// 1. Malformed (the one C++ was sending):
const escapedOffer = escapeJson(sdpOffer);
const badSignalingJson = "{\"type\":\"offer\",\"sdp\":\"" + escapedOffer + "\"}";
const badPayload = '{\n  "gameId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",\n  "signalingRequest": "' + badSignalingJson + '"\n}';

// 2. Clean properly double-escaped:
const goodSignalingInner = '{"type":"offer","sdp":"' + escapeJson(sdpOffer) + '"}';
const goodPayload = '{\n  "gameId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",\n  "signalingRequest": "' + escapeJson(goodSignalingInner) + '"\n}';

function send(name, host, payload) {
  const req = https.request({
    hostname: host,
    port: 443,
    path: '/createGameSessionJob',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload),
      'x-amz-access-token': 'dummy'
    }
  }, (res) => {
    let d = '';
    res.on('data', chunk => d += chunk);
    res.on('end', () => console.log(`[${name}] ${host} -> HTTP ${res.statusCode}: ${d}`));
  });
  req.on('error', e => console.error(name, e));
  req.write(payload);
  req.end();
}

send('BAD PAYLOAD', 'proxy-prod.tempo.digital.a2z.com', badPayload);
send('GOOD PAYLOAD', 'proxy-prod.tempo.digital.a2z.com', goodPayload);
send('BAD PAYLOAD', 'proxy-prod.eu-west-1.tempo.digital.a2z.com', badPayload);
send('GOOD PAYLOAD', 'proxy-prod.eu-west-1.tempo.digital.a2z.com', goodPayload);
