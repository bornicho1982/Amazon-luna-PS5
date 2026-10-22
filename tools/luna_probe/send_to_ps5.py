"""Quick tool to send a Luna session JSON directly to the PS5 homebrew app.

Usage:
    python send_to_ps5.py [--ps5 192.168.18.21] [--port 8085] [--file session.json]
If --file is omitted, prompts you to paste the JSON or uses the last recorded probe session.
"""
import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def send_session(ps5_ip, port, raw_json):
    # Verify the JSON is valid
    try:
        data = json.loads(raw_json)
        gs = data.get("gameSession", {})
        public_ip = gs.get("publicIp", "unknown")
        print(f"[+] Sesion valida detectada:")
        print(f"    - IP publica del servidor AWS: {public_ip}")
        print(f"    - ID de sesion: {gs.get('sessionId', 'N/A')}")
        print(f"    - Juego: {gs.get('gameId', 'N/A')}")
    except Exception as e:
        print(f"[!] Advertencia: el texto pegado no es JSON estricto ({e}), enviando en crudo...")

    url = f"http://{ps5_ip}:{port}/api/session"
    print(f"[*] Enviando sesion a la PS5 en {url} ...")

    # Send as form-urlencoded (matching the web dashboard form)
    body = urllib.parse.urlencode({"sessionJson": raw_json}).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"}
    )

    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            content = resp.read().decode("utf-8", errors="replace")
            print(f"[OK] Respuesta de la PS5 (HTTP {resp.status}):")
            print("     Sesion enviada y recibida por la consola con exito!")
            print("     Observa la pantalla de tu TV para ver la conexion.")
            return True
    except urllib.error.URLError as e:
        print(f"[ERROR] No se pudo conectar a la PS5 en {url}: {e}")
        print("        Asegurate de que la app PPSA99825 este abierta en la PS5 y ambos esten en la misma red Wi-Fi/LAN.")
        return False


def main():
    parser = argparse.ArgumentParser(description="Send Amazon Luna session to PS5 native app")
    parser.add_argument("--ps5", default="192.168.18.21", help="IP address of the PS5")
    parser.add_argument("--port", type=int, default=8085, help="Port of the PS5 dashboard server")
    parser.add_argument("--file", help="Path to JSON file containing getGameSessionJob response")
    args = parser.parse_args()

    content = ""
    if args.file and os.path.isfile(args.file):
        with open(args.file, "r", encoding="utf-8") as f:
            content = f.read().strip()
    else:
        # Check if session.json exists in this directory
        default_file = os.path.join(HERE, "session.json")
        if os.path.isfile(default_file):
            print(f"[*] Leyendo sesion desde {default_file} ...")
            with open(default_file, "r", encoding="utf-8") as f:
                content = f.read().strip()
        else:
            print("Pega aqui la respuesta JSON de getGameSessionJob (o presiona Enter para cancelar):")
            try:
                content = sys.stdin.read().strip()
            except KeyboardInterrupt:
                return

    if not content:
        print("[!] No se proporciono ninguna sesion. Saliendo.")
        return

    send_session(args.ps5, args.port, content)


if __name__ == "__main__":
    main()
