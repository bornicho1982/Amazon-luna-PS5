"""PS5 browser bridge: LAN DNS + HTTP launcher page.

- Redirects the PS5 User's Guide domains to this PC (serves a URL launcher page).
- Blocks every other Sony/PSN domain (returns 0.0.0.0) to keep the console off PSN.
- Forwards any other query to an upstream resolver.

Run as administrator (ports 53/udp and 80/tcp):
    python bridge.py --ip 192.168.18.2
Then set the PS5 primary DNS to that IP.
"""
import argparse
import http.server
import socket
import socketserver
import struct
import threading
from datetime import datetime

REDIRECT_DOMAINS = ("manuals.playstation.net", "ecode.np.cs.playstation.net")
BLOCK_SUFFIXES = (
    "playstation.net", "playstation.com", "sonyentertainmentnetwork.com",
    "scea.com", "sony.com", "np.community.playstation.net",
)
UPSTREAM = ("1.1.1.1", 53)

PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>PS5 Bridge</title>
<style>body{background:#140a2a;color:#eee;font-family:sans-serif;text-align:center;padding:40px}
a,button{display:block;margin:18px auto;width:60%;padding:22px;font-size:28px;border-radius:12px;
background:#2bb37f;color:#000;text-decoration:none;border:0}input{width:60%;font-size:26px;padding:14px}
#ua{font-size:14px;color:#999;margin-top:30px;word-break:break-all}</style></head><body>
<h1>PS5 Browser Bridge</h1>
<a href="https://luna.amazon.com/">Amazon Luna</a>
<a href="https://www.google.com/">Google</a>
<a href="https://html5test.com/">Test HTML5 / WebRTC</a>
<form onsubmit="location.href=document.getElementById('u').value;return false">
<input id="u" value="https://"><button>Abrir URL</button></form>
<div id="ua"></div>
<script>
var info = 'UA: ' + navigator.userAgent +
  ' | WebRTC: ' + (typeof RTCPeerConnection !== 'undefined') +
  ' | EME: ' + (typeof navigator.requestMediaKeySystemAccess !== 'undefined') +
  ' | MSE: ' + (typeof MediaSource !== 'undefined') +
  ' | Gamepad: ' + (typeof navigator.getGamepads !== 'undefined');
document.getElementById('ua').textContent = info;
var x = new XMLHttpRequest(); x.open('POST', '/report'); x.send(info);
</script></body></html>"""


def log(msg):
    print(f"[{datetime.now():%H:%M:%S}] {msg}", flush=True)


def parse_qname(data):
    labels, i = [], 12
    while data[i] != 0:
        n = data[i]
        labels.append(data[i + 1:i + 1 + n].decode(errors="replace"))
        i += n + 1
    qtype = struct.unpack(">H", data[i + 1:i + 3])[0]
    return ".".join(labels).lower(), qtype, i + 5


def build_a_reply(data, qend, ip, qtype):
    tid = data[:2]
    flags = b"\x81\x80"
    answer = b""
    ancount = 0
    if qtype == 1:  # A record only; other types get empty answer
        answer = b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + socket.inet_aton(ip)
        ancount = 1
    header = tid + flags + struct.pack(">HHHH", 1, ancount, 0, 0)
    return header + data[12:qend] + answer


def matches(name, suffixes):
    return any(name == s or name.endswith("." + s) for s in suffixes)


def dns_server(bind_ip, own_ip):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind_ip, 53))
    log(f"DNS listening on {bind_ip}:53")
    while True:
        data, addr = sock.recvfrom(4096)
        try:
            name, qtype, qend = parse_qname(data)
        except Exception:
            continue
        if matches(name, REDIRECT_DOMAINS):
            log(f"REDIRECT {name} -> {own_ip}")
            sock.sendto(build_a_reply(data, qend, own_ip, qtype), addr)
        elif matches(name, BLOCK_SUFFIXES):
            log(f"BLOCK    {name}")
            sock.sendto(build_a_reply(data, qend, "0.0.0.0", qtype), addr)
        else:
            threading.Thread(target=forward, args=(sock, data, addr, name), daemon=True).start()


def forward(sock, data, addr, name):
    up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    up.settimeout(3)
    try:
        up.sendto(data, UPSTREAM)
        resp, _ = up.recvfrom(4096)
        sock.sendto(resp, addr)
        log(f"FORWARD  {name}")
    except OSError as e:
        log(f"FAIL     {name}: {e}")
    finally:
        up.close()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        log(f"HTTP GET {self.headers.get('Host')}{self.path} UA={self.headers.get('User-Agent')}")
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        log(f"REPORT   {self.rfile.read(n).decode(errors='replace')}")
        self.send_response(204)
        self.end_headers()

    def log_message(self, *args):
        pass


def make_self_signed(path_dir):
    """Generate a self-signed cert for the redirected domains (test only)."""
    import datetime as dt
    import os
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    cert_p, key_p = os.path.join(path_dir, "cert.pem"), os.path.join(path_dir, "key.pem")
    if os.path.exists(cert_p):
        return cert_p, key_p
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, REDIRECT_DOMAINS[0])])
    now = dt.datetime.now(dt.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - dt.timedelta(days=1)).not_valid_after(now + dt.timedelta(days=365))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName(d) for d in REDIRECT_DOMAINS]), False)
            .sign(key, hashes.SHA256()))
    with open(cert_p, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    with open(key_p, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                  serialization.NoEncryption()))
    return cert_p, key_p


class LoggingTLSServer(socketserver.ThreadingTCPServer):
    """HTTPS server that logs handshake failures (to see if the PS5 rejects the cert)."""
    ctx = None

    def get_request(self):
        sock, addr = self.socket.accept()
        log(f"TLS      connection from {addr[0]}")
        try:
            return self.ctx.wrap_socket(sock, server_side=True), addr
        except Exception as e:
            log(f"TLS      handshake failed from {addr[0]}: {e}")
            sock.close()
            raise OSError("handshake failed")


def https_server(cert_dir):
    import ssl
    cert_p, key_p = make_self_signed(cert_dir)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert_p, key_p)
    LoggingTLSServer.ctx = ctx
    srv = LoggingTLSServer(("0.0.0.0", 443), Handler)
    log("HTTPS listening on :443 (self-signed)")
    srv.serve_forever()


def main():
    import os
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", required=True, help="LAN IP of this PC")
    a = ap.parse_args()
    threading.Thread(target=dns_server, args=("0.0.0.0", a.ip), daemon=True).start()
    threading.Thread(target=https_server, args=(os.path.dirname(os.path.abspath(__file__)),),
                     daemon=True).start()
    srv = socketserver.ThreadingTCPServer(("0.0.0.0", 80), Handler)
    log("HTTP listening on :80")
    srv.serve_forever()


if __name__ == "__main__":
    main()
