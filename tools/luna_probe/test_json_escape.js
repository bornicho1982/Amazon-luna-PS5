const fs = require('fs');

function escapeJson(str) {
    let out = '';
    for (const c of str) {
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
const escapedOffer = escapeJson(sdpOffer);

const signalingJson = "{\"type\":\"offer\",\"sdp\":\"" + escapedOffer + "\"," +
    "\"extras\":{\"features\":[],\"requestedChannels\":[\"display\",\"audio\",\"input\",\"participant\"]," +
    "\"clientContext\":{\"browserClientRole\":\"BROWSER\",\"browserType\":\"Chrome\",\"browserVersion\":\"120.0.0.0\"," +
    "\"deviceModel\":\"LG TV\",\"deviceType\":\"smarttv\",\"osName\":\"webOS\",\"osVersion\":\"9.0\"}}}";

const doubleEscapedSignaling = escapeJson(signalingJson);

console.log("Single escaped valid JSON test:");
const payload1 = JSON.stringify({
  gameId: "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",
  signalingRequest: signalingJson,
  timeout: 15000,
  clientFeatures: { h264: true, opus: true, dataChannels: true },
  clientContext: { clientType: "BROWSER", deviceType: "SMART_TV", deviceModel: "LG TV" }
});

try {
  JSON.parse(payload1);
  console.log("payload1 parses cleanly in JSON!");
} catch(e) {
  console.log("payload1 FAILED to parse:", e);
}

// Now test C++ version:
// in C++:
// std::string signalingJson = "{\\\"type\\\":\\\"offer\\\"...\"" + escapedOffer + ...
// payload = "{\n  \"gameId\": ... \"signalingRequest\": \"" + signalingJson + "\" ...
const cppSignalingJson = "{\\\"type\\\":\\\"offer\\\",\\\"sdp\\\":\\\"" + escapedOffer + "\\\"}";
const cppPayload = "{\n  \"signalingRequest\": \"" + cppSignalingJson + "\"\n}";
console.log("cppPayload:");
console.log(cppPayload);
try {
  const p = JSON.parse(cppPayload);
  console.log("cppPayload parses! Value of signalingRequest:", p.signalingRequest);
  // Can signalingRequest itself be parsed as JSON?
  const inner = JSON.parse(p.signalingRequest);
  console.log("inner parsed cleanly! sdp starts with:", inner.sdp.slice(0, 10));
} catch(e) {
  console.log("cppPayload FAILED:", e.message);
}
