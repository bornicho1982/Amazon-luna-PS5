"""Luna web client probe (diagnostic only, user's own account).

Launches Chrome with a dedicated profile + CDP, injects hooks and records:
- EME usage (requestMediaKeySystemAccess, MediaKeys, 'encrypted' events)  -> DRM yes/no
- RTCPeerConnection config, SDP offers/answers, data channels              -> media transport
- WebSocket URLs and Luna/AWS network endpoints                             -> signaling/session API
Secrets (Authorization, cookies, SigV4 query params) are redacted before writing.

Usage: python luna_probe.py   (log in to Luna in the opened window and start a game)
Output: luna_probe.jsonl next to this script.
"""
import json
import os
import re
import subprocess
import threading
import time
import urllib.request

import websocket

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE = os.path.join(HERE, "chrome_profile")
OUT = os.path.join(HERE, "luna_probe.jsonl")
PORT = 9333
INTERESTING = re.compile(r"luna|amazon|kinesis|aws|gamelift|webrtc|turn|stun|license|widevine|playready",
                         re.I)
SECRET_QS = re.compile(r"(X-Amz-[A-Za-z]+|token|access_token|session|signature|credential)=[^&\s\"]+", re.I)

HOOK = r"""
(() => {
  if (window.__lunaProbe) return; window.__lunaProbe = true;
  const L = (kind, data) => { try { console.debug('__LP__' + JSON.stringify({kind, data})); } catch (e) {} };
  if (navigator.requestMediaKeySystemAccess) {
    const o = navigator.requestMediaKeySystemAccess.bind(navigator);
    navigator.requestMediaKeySystemAccess = (ks, cfg) => { L('eme.request', {ks, cfg}); return o(ks, cfg)
      .then(a => { L('eme.granted', {ks}); return a; }, e => { L('eme.denied', {ks, e: String(e)}); throw e; }); };
  }
  if (window.HTMLMediaElement) {
    const sm = HTMLMediaElement.prototype.setMediaKeys;
    if (sm) HTMLMediaElement.prototype.setMediaKeys = function (k) { L('eme.setMediaKeys', {}); return sm.call(this, k); };
    document.addEventListener('encrypted', e => L('eme.encrypted', {type: e.initDataType}), true);
  }
  const PC = window.RTCPeerConnection;
  if (PC) {
    window.RTCPeerConnection = function (cfg, ...r) {
      const safe = cfg ? {...cfg, iceServers: (cfg.iceServers || []).map(s => ({urls: s.urls}))} : cfg;
      L('pc.new', safe);
      const pc = new PC(cfg, ...r);
      const wrap = (n) => { const f = pc[n].bind(pc); pc[n] = (d, ...a) => { L('pc.' + n, d && {type: d.type, sdp: d.sdp}); return f(d, ...a); }; };
      wrap('setLocalDescription'); wrap('setRemoteDescription');
      const cdc = pc.createDataChannel.bind(pc);
      pc.createDataChannel = (label, o) => { L('pc.datachannel', {label, o}); return cdc(label, o); };
      pc.addEventListener('datachannel', e => L('pc.remote_datachannel', {label: e.channel.label}));
      pc.addEventListener('track', e => L('pc.track', {kind: e.track.kind}));
      pc.addEventListener('connectionstatechange', () => L('pc.state', pc.connectionState));
      return pc;
    };
    window.RTCPeerConnection.prototype = PC.prototype;
  }
  const WS = window.WebSocket;
  window.WebSocket = function (url, p) {
    L('ws.open', {url: String(url).split('?')[0]});
    const ws = p ? new WS(url, p) : new WS(url);
    let n = 0;
    ws.addEventListener('message', e => { if (n++ < 30) L('ws.msg_in', {url: String(url).split('?')[0], d: String(e.data).slice(0, 600)}); });
    const s = ws.send.bind(ws); let m = 0;
    ws.send = d => { if (m++ < 30) L('ws.msg_out', {url: String(url).split('?')[0], d: String(d).slice(0, 600)}); return s(d); };
    return ws;
  };
  window.WebSocket.prototype = WS.prototype;
  Object.assign(window.WebSocket, {CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3});
})();
"""

lock = threading.Lock()


def redact(s):
    return SECRET_QS.sub(lambda m: m.group(1) + "=<redacted>", s)


def write(rec):
    rec["t"] = time.strftime("%H:%M:%S")
    line = redact(json.dumps(rec, ensure_ascii=False))
    with lock, open(OUT, "a", encoding="utf-8") as f:
        f.write(line + "\n")
    print(line[:220], flush=True)


def main():
    subprocess.Popen([CHROME, f"--remote-debugging-port={PORT}", f"--user-data-dir={PROFILE}",
                      "--no-first-run", "https://luna.amazon.com/"])
    for _ in range(30):
        try:
            info = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/version"))
            break
        except OSError:
            time.sleep(1)
    ws = websocket.create_connection(info["webSocketDebuggerUrl"], suppress_origin=True)
    mid = [0]

    def send(method, params=None, session=None):
        mid[0] += 1
        msg = {"id": mid[0], "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        ws.send(json.dumps(msg))

    send("Target.setDiscoverTargets", {"discover": True})
    send("Target.setAutoAttach", {"autoAttach": True, "waitForDebuggerOnStart": True, "flatten": True})
    attached = set()

    def attach(tid):
        if tid not in attached:
            attached.add(tid)
            send("Target.attachToTarget", {"targetId": tid, "flatten": True})

    send("Target.getTargets")
    write({"kind": "probe.start"})
    while True:
        msg = json.loads(ws.recv())
        method, p, sid = msg.get("method"), msg.get("params", {}), msg.get("sessionId")
        if "result" in msg and "targetInfos" in msg["result"]:
            for t in msg["result"]["targetInfos"]:
                if t["type"] == "page":
                    attach(t["targetId"])
        elif method == "Target.targetCreated" and p["targetInfo"]["type"] in ("page", "iframe"):
            attach(p["targetInfo"]["targetId"])
        elif method == "Target.attachedToTarget":
            s = p["sessionId"]
            send("Page.enable", session=s)
            send("Page.addScriptToEvaluateOnNewDocument", {"source": HOOK, "runImmediately": True}, s)
            send("Runtime.enable", session=s)
            send("Network.enable", session=s)
            send("Target.setAutoAttach", {"autoAttach": True, "waitForDebuggerOnStart": True, "flatten": True}, s)
            send("Runtime.evaluate", {"expression": HOOK}, s)
            send("Runtime.runIfWaitingForDebugger", session=s)
        elif method == "Runtime.consoleAPICalled":
            args = p.get("args", [])
            if args and str(args[0].get("value", "")).startswith("__LP__"):
                write(json.loads(args[0]["value"][6:]))
        elif method == "Network.requestWillBeSent":
            r = p["request"]
            if INTERESTING.search(r["url"]) and p.get("type") in ("XHR", "Fetch", "WebSocket", "Other", None):
                body = (r.get("postData") or "")[:800]
                write({"kind": "net.req", "data": {"m": r["method"], "url": r["url"].split("?")[0],
                                                   "body": body}})
        elif method == "Network.responseReceived":
            r = p["response"]
            if INTERESTING.search(r["url"]) and p.get("type") in ("XHR", "Fetch"):
                write({"kind": "net.resp", "data": {"url": r["url"].split("?")[0], "status": r["status"],
                                                    "mime": r.get("mimeType")}})


if __name__ == "__main__":
    main()
