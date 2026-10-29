#!/usr/bin/env python3
"""
Interactive Page Sniffer & Reverse Engineering Suite for Amazon Luna.
Captures full network API traffic, DOM click/action telemetry,
downloaded assets (images, fonts, videos, HLS streams via FFmpeg),
in-DOM SVG icons, computed CSS tokens, and automated hover sweeps.

Fixed Architecture:
- Main thread owns sync_playwright and pumps page.wait_for_timeout(100).
- Secondary thread only reads stdin and puts into cmd_queue (0 greenlet errors).
- In-browser Floating Control HUD for one-click Sweep, Snap, and live stats.
"""

import argparse
import datetime
import hashlib
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("[!] Playwright is not installed. Install via: pip install playwright")
    sys.exit(1)

HERE = Path(__file__).resolve().parent
PROJECT_ROOT = HERE.parent.parent
DEFAULT_PROFILE_DIR = HERE / "chrome_profile" if (HERE / "chrome_profile").exists() else (HERE / "browser_profile")
CAPTURES_BASE_DIR = HERE / "captures"

# Amazon Luna & AWS Network Patterns
API_URL_PATTERNS = [
    re.compile(r"tempo\.digital\.a2z\.com", re.I),
    re.compile(r"\.a2z\.com", re.I),
    re.compile(r"api\.amazon\.", re.I),
    re.compile(r"luna\.amazon\.", re.I),
    re.compile(r"\.amazonaws\.com", re.I),
    re.compile(r"project-tempo", re.I),
]

VIDEO_URL_PATTERNS = [
    re.compile(r"\.(mp4|webm|m3u8|mpd|mov|ts|m4v)(\?.*)?$", re.I),
    re.compile(r"/video/", re.I),
]

ASSET_EXTS = {
    "images": [".png", ".jpg", ".jpeg", ".webp", ".avif", ".gif"],
    "fonts": [".woff2", ".woff", ".ttf", ".otf"],
    "videos": [".mp4", ".webm", ".m3u8", ".mpd", ".mov", ".ts", ".m4v"],
    "icons": [".svg"],
}

# Injected JavaScript: Action Tracker, Video Observer, and Floating Control HUD
INJECTED_CLIENT_JS = r"""
(() => {
    if (window.__lunaSnifferInjected) return;
    window.__lunaSnifferInjected = true;

    // --- 1. FLOATING CONTROL HUD ---
    function injectHUD() {
        if (document.getElementById('__luna_sniffer_hud')) return;
        const hud = document.createElement('div');
        hud.id = '__luna_sniffer_hud';
        hud.style.cssText = `
            position: fixed;
            bottom: 12px;
            left: 12px;
            z-index: 2147483647;
            background: rgba(14, 11, 22, 0.95);
            color: #ffffff;
            border: 1px solid #9146FF;
            border-radius: 8px;
            padding: 8px 12px;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            font-size: 12px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.7);
            display: flex;
            flex-direction: column;
            gap: 6px;
            backdrop-filter: blur(8px);
            user-select: none;
        `;
        hud.innerHTML = `
            <div style="display:flex; align-items:center; justify-content:space-between; gap:12px;">
                <span style="font-weight:bold; color:#A78BFA; display:flex; align-items:center; gap:4px;">
                    <span style="display:inline-block; width:8px; height:8px; background:#10B981; border-radius:50%;"></span>
                    Luna Sniffer
                </span>
                <span id="__luna_sniffer_hud_stats" style="font-size:11px; color:#94A3B8;">APIs: 0 | Img: 0 | Vid: 0 | SVG: 0</span>
            </div>
            <div style="display:flex; gap:6px;">
                <button id="__btn_sweep" style="background:#9146FF; color:#fff; border:none; padding:5px 10px; border-radius:4px; cursor:pointer; font-size:11px; font-weight:600;">🚀 Sweep Página</button>
                <button id="__btn_snap" style="background:#2563EB; color:#fff; border:none; padding:5px 10px; border-radius:4px; cursor:pointer; font-size:11px; font-weight:600;">📸 Snap Completo</button>
                <button id="__btn_status" style="background:#334155; color:#fff; border:none; padding:5px 10px; border-radius:4px; cursor:pointer; font-size:11px;">📊 Estado</button>
            </div>
        `;
        document.body.appendChild(hud);

        document.getElementById('__btn_sweep').onclick = (e) => {
            e.stopPropagation();
            if (window.__sniffer_cmd) window.__sniffer_cmd('sweep');
        };
        document.getElementById('__btn_snap').onclick = (e) => {
            e.stopPropagation();
            if (window.__sniffer_cmd) window.__sniffer_cmd('snap');
        };
        document.getElementById('__btn_status').onclick = (e) => {
            e.stopPropagation();
            if (window.__sniffer_cmd) window.__sniffer_cmd('status');
        };
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', injectHUD);
    } else {
        injectHUD();
    }
    setInterval(injectHUD, 2000);

    // --- 2. ACTION TRACKER (Clicks, Hover, Navigation) ---
    function getElementSummary(el) {
        if (!el || el === document.body || el === document.documentElement) return null;
        const tag = el.tagName ? el.tagName.toLowerCase() : '';
        const id = el.id ? '#' + el.id : '';
        const classes = el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).slice(0, 3).join('.') : '';
        const role = el.getAttribute('role') || '';
        const ariaLabel = el.getAttribute('aria-label') || '';
        const testId = el.getAttribute('data-testid') || el.getAttribute('data-test') || el.getAttribute('data-asin') || '';
        const text = (el.innerText || el.textContent || '').trim().replace(/\s+/g, ' ').substring(0, 40);
        const href = el.getAttribute('href') || (el.closest('a') ? el.closest('a').getAttribute('href') : null);

        const interactive = el.closest('button, a, [role="button"], [role="tab"], [role="link"], [data-testid], li, .card, .tile');

        return {
            tag,
            selector: tag + id + classes,
            id: el.id || null,
            role,
            ariaLabel,
            testId,
            text,
            href,
            isInteractiveParent: interactive && interactive !== el,
            parentSelector: interactive ? (interactive.tagName.toLowerCase() + (interactive.id ? '#' + interactive.id : '')) : null
        };
    }

    document.addEventListener('click', (evt) => {
        try {
            if (evt.target.closest('#__luna_sniffer_hud')) return;
            const summary = getElementSummary(evt.target);
            if (!summary) return;
            const payload = {
                type: 'click',
                timestamp: Date.now(),
                url: window.location.href,
                target: summary,
                x: evt.clientX,
                y: evt.clientY
            };
            if (window.__recordAction) window.__recordAction(JSON.stringify(payload));
        } catch (e) {}
    }, true);

    let lastHoverTime = 0;
    let lastHoverEl = null;
    document.addEventListener('mouseover', (evt) => {
        try {
            if (evt.target.closest('#__luna_sniffer_hud')) return;
            const now = Date.now();
            if (now - lastHoverTime < 200) return;
            const target = evt.target.closest('a, button, [role="button"], [data-testid], .tile, .card, [data-asin]');
            if (!target || target === lastHoverEl) return;
            lastHoverEl = target;
            lastHoverTime = now;

            const summary = getElementSummary(target);
            if (!summary) return;
            const payload = {
                type: 'hover',
                timestamp: now,
                url: window.location.href,
                target: summary
            };
            if (window.__recordAction) window.__recordAction(JSON.stringify(payload));
        } catch (e) {}
    }, true);

    const origPushState = history.pushState;
    history.pushState = function() {
        origPushState.apply(this, arguments);
        if (window.__recordAction) {
            window.__recordAction(JSON.stringify({
                type: 'route_change',
                timestamp: Date.now(),
                url: window.location.href,
                path: arguments[2]
            }));
        }
    };
    window.addEventListener('popstate', () => {
        if (window.__recordAction) {
            window.__recordAction(JSON.stringify({
                type: 'route_change',
                timestamp: Date.now(),
                url: window.location.href
            }));
        }
    });

    // --- 3. VIDEO ELEMENT TRACKING ---
    function inspectVideoElement(vid, triggerEvent) {
        try {
            const src = vid.currentSrc || vid.src || (vid.querySelector('source') ? vid.querySelector('source').src : '');
            if (!src) return;

            const card = vid.closest('.card, .tile, [data-testid], [data-asin], [role="article"], div[class*="game"]');
            let cardTitle = '';
            let asin = '';
            if (card) {
                asin = card.getAttribute('data-asin') || card.getAttribute('data-test-asin') || '';
                const titleEl = card.querySelector('h1, h2, h3, h4, [class*="title"], [class*="name"]');
                if (titleEl) cardTitle = titleEl.innerText.trim();
            }

            const section = vid.closest('section, [class*="row"], [class*="carousel"], [class*="hero"]');
            let sectionTitle = '';
            if (section) {
                const secH = section.querySelector('h1, h2, h3, header');
                if (secH) sectionTitle = secH.innerText.trim();
            }

            const data = {
                type: 'video_detected',
                event: triggerEvent,
                timestamp: Date.now(),
                src: src,
                videoWidth: vid.videoWidth || 0,
                videoHeight: vid.videoHeight || 0,
                duration: vid.duration || 0,
                cardTitle: cardTitle,
                asin: asin,
                section: sectionTitle
            };

            if (window.__recordVideo) window.__recordVideo(JSON.stringify(data));
        } catch (e) {}
    }

    function attachVideoListeners(vid) {
        if (!vid || vid.__lunaTracked) return;
        vid.__lunaTracked = true;
        inspectVideoElement(vid, 'added_to_dom');
        vid.addEventListener('play', () => inspectVideoElement(vid, 'play'));
        vid.addEventListener('loadeddata', () => inspectVideoElement(vid, 'loadeddata'));
        vid.addEventListener('playing', () => inspectVideoElement(vid, 'playing'));
    }

    const videoObserver = new MutationObserver((mutations) => {
        for (const m of mutations) {
            for (const node of m.addedNodes) {
                if (node.nodeType === 1) {
                    if (node.tagName && node.tagName.toLowerCase() === 'video') {
                        attachVideoListeners(node);
                    } else if (node.querySelectorAll) {
                        node.querySelectorAll('video').forEach(attachVideoListeners);
                    }
                }
            }
        }
    });
    videoObserver.observe(document.documentElement, { childList: true, subtree: true });
    document.querySelectorAll('video').forEach(attachVideoListeners);
})();
"""

class LunaPageSniffer:
    def __init__(self, capture_name=None, start_url="https://luna.amazon.es", mode="desktop", profile_dir=None, headless=False, test_commands=None):
        self.start_url = start_url
        self.mode = mode
        self.headless = headless
        self.test_commands = test_commands
        self.profile_dir = Path(profile_dir) if profile_dir else DEFAULT_PROFILE_DIR
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        self.capture_name = capture_name or f"session_{ts}"
        self.session_dir = CAPTURES_BASE_DIR / self.capture_name
        self.api_dir = self.session_dir / "api"
        self.actions_dir = self.session_dir / "actions"
        self.assets_dir = self.session_dir / "assets"
        self.videos_dir = self.assets_dir / "videos"
        self.manifests_dir = self.videos_dir / "manifests"
        self.styles_dir = self.session_dir / "styles"

        for d in [
            self.session_dir, self.api_dir, self.actions_dir,
            self.assets_dir / "images", self.assets_dir / "icons",
            self.assets_dir / "fonts", self.videos_dir, self.manifests_dir,
            self.styles_dir
        ]:
            d.mkdir(parents=True, exist_ok=True)

        self.api_counter = 0
        self.video_counter = 0
        self.svg_counter = 0
        self.asset_cache = set()
        self.video_cache = set()
        self.actions_file = open(self.actions_dir / "user_actions.jsonl", "a", encoding="utf-8")
        self.videos_detected_file = open(self.actions_dir / "videos_detected.jsonl", "a", encoding="utf-8")
        self.api_index_file = open(self.api_dir / "index.jsonl", "a", encoding="utf-8")
        self.videos_index_file = open(self.videos_dir / "videos_index.jsonl", "a", encoding="utf-8")
        self.recent_actions = []
        self.detected_videos = {}
        self.lock = threading.Lock()
        self.cmd_queue = queue.Queue()
        self.running = True

        print(f"[*] Suite de captura inicializada en: {self.session_dir}")
        print(f"[*] Perfil de navegador: {self.profile_dir}")

    def is_api_request(self, url: str) -> bool:
        return any(p.search(url) for p in API_URL_PATTERNS)

    def is_video_request(self, url: str, content_type: str) -> bool:
        ct = content_type.lower()
        if "video/" in ct or "mpegurl" in ct or "dash+xml" in ct:
            return True
        path = urllib.parse.urlparse(url).path.lower()
        if any(path.endswith(ext) for ext in [".mp4", ".webm", ".m3u8", ".mpd", ".mov", ".ts", ".m4v"]):
            return True
        return any(p.search(path) for p in VIDEO_URL_PATTERNS)

    def sanitize_filename(self, s: str, max_len=60) -> str:
        s = re.sub(r"[^A-Za-z0-9_-]", "_", s)
        return s[:max_len]

    def on_request(self, request):
        pass

    def on_response(self, response):
        url = response.url
        content_type = response.headers.get("content-type", "").lower()
        res_type = response.request.resource_type

        # 1. Video and HLS Streams Interception
        if self.is_video_request(url, content_type):
            try:
                self.handle_video_response(response)
                return
            except Exception:
                pass

        # 2. Standard Asset Downloading (Images, Fonts, Icons)
        # Handle images/fonts before API so we don't treat CDN images as JSON APIs
        if any(url.lower().endswith(ext) for ext in ASSET_EXTS["images"] + ASSET_EXTS["fonts"] + ASSET_EXTS["icons"]) or \
           "image/" in content_type or "font/" in content_type:
            try:
                self.handle_asset_response(response)
                return
            except Exception:
                pass

        # 3. API Calls (JSON / XHR / Fetch across luna.amazon.*, *.a2z.com, *.amazonaws.com)
        is_api = (
            res_type in ["xhr", "fetch"] or
            "application/json" in content_type or
            "application/x-amz" in content_type or
            self.is_api_request(url) and ("json" in content_type or "text" in content_type or not content_type)
        )

        if is_api:
            try:
                self.save_api_exchange(response)
            except Exception:
                pass

    def handle_video_response(self, response):
        url = response.url
        content_type = response.headers.get("content-type", "").lower()
        parsed = urllib.parse.urlparse(url)
        path = parsed.path.lower()

        now = time.time() * 1000
        correlated_info = {}
        with self.lock:
            for v_src, v_meta in self.detected_videos.items():
                if v_src in url or (v_meta.get("asin") and v_meta.get("asin") in url):
                    correlated_info = v_meta
                    break
            if not correlated_info:
                for act in reversed(self.recent_actions):
                    if now - act.get("timestamp", 0) < 3000:
                        correlated_info = act.get("target", {})
                        break

        url_hash = hashlib.md5(url.encode()).hexdigest()[:10]
        if url_hash in self.video_cache:
            return

        card_title = correlated_info.get("cardTitle") or correlated_info.get("text") or "video"
        clean_title = self.sanitize_filename(card_title, max_len=30)

        # A) HLS Stream Manifest (.m3u8)
        if ".m3u8" in path or "mpegurl" in content_type:
            try:
                body = response.body()
                if len(body) > 0:
                    self.video_cache.add(url_hash)
                    with self.lock:
                        self.video_counter += 1
                        v_idx = self.video_counter
                    base_name = f"{v_idx:03d}_{clean_title}_{url_hash}"
                    manifest_path = self.manifests_dir / f"{base_name}.m3u8"
                    with open(manifest_path, "wb") as f:
                        f.write(body)
                    print(f"\033[96m[VIDEO:HLS]\033[0m Manifiesto guardado: {manifest_path.name}")

                    mp4_target = self.videos_dir / f"{base_name}.mp4"
                    threading.Thread(
                        target=self.download_hls_stream_ffmpeg,
                        args=(url, mp4_target, correlated_info),
                        daemon=True
                    ).start()
            except Exception:
                pass
            return

        # B) Direct Video File (.mp4, .webm, .mov)
        ext = ".mp4"
        if ".webm" in path or "webm" in content_type:
            ext = ".webm"
        elif ".mov" in path:
            ext = ".mov"

        try:
            body = response.body()
            if len(body) > 1024:
                self.video_cache.add(url_hash)
                with self.lock:
                    self.video_counter += 1
                    v_idx = self.video_counter
                base_name = f"{v_idx:03d}_{clean_title}_{url_hash}"
                target_file = self.videos_dir / f"{base_name}{ext}"
                with open(target_file, "wb") as f:
                    f.write(body)
                size_mb = len(body) / (1024 * 1024)
                print(f"\033[92m[VIDEO:DOWNLOAD]\033[0m \033[1m{target_file.name}\033[0m ({size_mb:.2f} MB) -> Juego: '{card_title}'")

                meta = {
                    "index": v_idx,
                    "timestamp": datetime.datetime.now().isoformat(),
                    "file": target_file.name,
                    "size_bytes": len(body),
                    "size_mb": round(size_mb, 2),
                    "url": url,
                    "content_type": content_type,
                    "game_title": card_title,
                    "asin": correlated_info.get("asin", "")
                }
                with self.lock:
                    self.videos_index_file.write(json.dumps(meta, ensure_ascii=False) + "\n")
                    self.videos_index_file.flush()
        except Exception:
            pass

    def download_hls_stream_ffmpeg(self, m3u8_url: str, output_path: Path, correlated_info: dict):
        cmd = [
            "ffmpeg", "-y",
            "-loglevel", "error",
            "-headers", "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
            "-i", m3u8_url,
            "-c", "copy",
            "-bsf:a", "aac_adtstoasc",
            str(output_path)
        ]
        try:
            res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
            if res.returncode == 0 and output_path.exists() and output_path.stat().st_size > 1024:
                size_mb = output_path.stat().st_size / (1024 * 1024)
                card_title = correlated_info.get("cardTitle") or correlated_info.get("text") or "stream"
                print(f"\033[92m[VIDEO:HLS->MP4]\033[0m \033[1m{output_path.name}\033[0m ({size_mb:.2f} MB) descargado exitosamente!")
                meta = {
                    "timestamp": datetime.datetime.now().isoformat(),
                    "file": output_path.name,
                    "size_bytes": output_path.stat().st_size,
                    "size_mb": round(size_mb, 2),
                    "source_m3u8": m3u8_url,
                    "game_title": card_title,
                    "asin": correlated_info.get("asin", "")
                }
                with self.lock:
                    self.videos_index_file.write(json.dumps(meta, ensure_ascii=False) + "\n")
                    self.videos_index_file.flush()
        except Exception:
            pass

    def record_video(self, video_json_str: str):
        try:
            data = json.loads(video_json_str)
            src = data.get("src", "")
            title = data.get("cardTitle", "") or "Desconocido"
            asin = data.get("asin", "")
            w = data.get("videoWidth", 0)
            h = data.get("videoHeight", 0)
            dur = data.get("duration", 0)
            event = data.get("event", "")

            with self.lock:
                self.videos_detected_file.write(video_json_str + "\n")
                self.videos_detected_file.flush()
                self.detected_videos[src] = data

            print(f"\033[95m[VIDEO:DOM]\033[0m \033[1m'{title}'\033[0m (ASIN: {asin or 'N/A'}) - {w}x{h} ({dur:.1f}s) Evento: {event}")
        except Exception:
            pass

    def save_api_exchange(self, response):
        with self.lock:
            self.api_counter += 1
            idx = self.api_counter

        req = response.request
        url = response.url
        method = req.method

        now = time.time() * 1000
        correlated_action = None
        with self.lock:
            for act in reversed(self.recent_actions):
                if now - act.get("timestamp", 0) < 1500:
                    correlated_action = act
                    break

        req_headers = {k: v for k, v in req.headers.items()}
        res_headers = {k: v for k, v in response.headers.items()}

        post_data = None
        try:
            post_data = req.post_data
            if post_data and post_data.startswith("{"):
                post_data = json.loads(post_data)
        except Exception:
            pass

        res_body = None
        try:
            body_bytes = response.body()
            res_str = body_bytes.decode("utf-8", errors="replace")
            try:
                res_body = json.loads(res_str)
            except Exception:
                res_body = res_str[:5000]
        except Exception as e:
            res_body = f"<Error reading body: {e}>"

        parsed = urllib.parse.urlparse(url)
        path_name = self.sanitize_filename(parsed.path.strip("/").replace("/", "_")) or "root"
        qs_name = self.sanitize_filename(parsed.query.replace("&", "_").replace("=", "-"))
        if qs_name:
            fname = f"{idx:03d}_{method}_{path_name}_{qs_name}.json"
        else:
            fname = f"{idx:03d}_{method}_{path_name}.json"

        exchange_data = {
            "index": idx,
            "timestamp": datetime.datetime.now().isoformat(),
            "url": url,
            "method": method,
            "status": response.status,
            "correlated_action": correlated_action,
            "request": {
                "headers": req_headers,
                "payload": post_data
            },
            "response": {
                "headers": res_headers,
                "body": res_body
            }
        }

        file_path = self.api_dir / fname
        with open(file_path, "w", encoding="utf-8") as f:
            json.dump(exchange_data, f, indent=2, ensure_ascii=False)

        summary = {
            "index": idx,
            "method": method,
            "status": response.status,
            "url": url,
            "file": fname,
            "action": correlated_action.get("target", {}).get("text", "") if correlated_action else ""
        }
        with self.lock:
            self.api_index_file.write(json.dumps(summary, ensure_ascii=False) + "\n")
            self.api_index_file.flush()

        print(f"\033[92m[API #{idx:03d}]\033[0m {method} {parsed.netloc}{parsed.path}?{parsed.query[:50]} -> \033[94m{response.status}\033[0m")

    def handle_asset_response(self, response):
        url = response.url
        parsed = urllib.parse.urlparse(url)
        path = parsed.path.lower()
        content_type = response.headers.get("content-type", "").lower()

        cat = None
        ext = None
        for category, extensions in ASSET_EXTS.items():
            if category == "videos":
                continue
            for e in extensions:
                if path.endswith(e):
                    cat = category
                    ext = e
                    break
            if cat:
                break

        if not cat:
            if "image/" in content_type:
                cat = "images"
                ext = ".jpg" if "jpeg" in content_type else (".png" if "png" in content_type else ".webp")
            elif "font/" in content_type:
                cat = "fonts"
                ext = ".woff2"

        if not cat or ext not in ASSET_EXTS[cat]:
            return

        url_hash = hashlib.md5(url.encode()).hexdigest()[:10]
        if url_hash in self.asset_cache:
            return
        self.asset_cache.add(url_hash)

        basename = os.path.basename(parsed.path) or "asset"
        clean_base = self.sanitize_filename(os.path.splitext(basename)[0], max_len=30)
        filename = f"{clean_base}_{url_hash}{ext}"
        target_path = self.assets_dir / cat / filename

        try:
            body = response.body()
            if len(body) > 0:
                with open(target_path, "wb") as f:
                    f.write(body)
                if cat in ["icons", "fonts"]:
                    print(f"\033[95m[ASSET:{cat.upper()}]\033[0m {filename} ({len(body)} bytes)")
        except Exception:
            pass

    def record_action(self, action_json_str: str):
        try:
            action = json.loads(action_json_str)
            with self.lock:
                self.actions_file.write(action_json_str + "\n")
                self.actions_file.flush()
                self.recent_actions.append(action)
                if len(self.recent_actions) > 20:
                    self.recent_actions.pop(0)

            act_type = action.get("type", "")
            tgt = action.get("target", {})
            sel = tgt.get("selector", "")
            txt = tgt.get("text", "")
            aria = tgt.get("ariaLabel", "")
            desc = txt or aria or sel
            if act_type == "click":
                print(f"\033[93m[ACTION:CLICK]\033[0m \033[1m'{desc}'\033[0m ({sel}) -> URL: {action.get('url')}")
            elif act_type == "hover":
                print(f"\033[90m[ACTION:HOVER]\033[0m '{desc}'")
            elif act_type == "route_change":
                print(f"\033[96m[ROUTE CHANGE]\033[0m {action.get('url')}")
        except Exception:
            pass

    def scrape_dom_assets(self, page, snap_name="dom_scrape"):
        """Extracts in-DOM SVG icons, computed styles, screenshot and HTML snapshot (run on main thread)."""
        print(f"\n[*] Extrayendo assets in-DOM, iconos SVG y estilos computados ({snap_name})...")
        page.wait_for_timeout(1000)
        
        # 1. Extract in-DOM SVGs
        svgs = page.evaluate("""() => {
            const results = [];
            document.querySelectorAll('svg').forEach((svg, idx) => {
                if (svg.closest('#__luna_sniffer_hud')) return;
                const parent = svg.closest('button, a, [role="button"], [aria-label], div');
                const label = (parent ? parent.getAttribute('aria-label') : null) || 
                              svg.getAttribute('aria-label') || 
                              svg.getAttribute('id') || 
                              (svg.querySelector('title') ? svg.querySelector('title').textContent : null) || 
                              ('icon_' + idx);
                results.push({
                    name: label.trim().replace(/[^a-zA-Z0-9_-]/g, '_').substring(0, 35),
                    outerHTML: svg.outerHTML,
                    viewBox: svg.getAttribute('viewBox'),
                    width: svg.getAttribute('width') || svg.clientWidth,
                    height: svg.getAttribute('height') || svg.clientHeight
                });
            });
            return results;
        }""")

        svg_dir = self.assets_dir / "icons"
        saved_svgs = 0
        for item in svgs:
            name = item.get("name") or "icon"
            svg_content = item.get("outerHTML", "")
            if not svg_content:
                continue
            h = hashlib.md5(svg_content.encode()).hexdigest()[:6]
            file_path = svg_dir / f"{name}_{h}.svg"
            if not file_path.exists():
                with open(file_path, "w", encoding="utf-8") as f:
                    f.write(svg_content)
                saved_svgs += 1
                self.svg_counter += 1

        print(f"  -> {saved_svgs} nuevos iconos SVG guardados en {svg_dir}")

        # 2. Extract Computed CSS Tokens
        style_tokens = page.evaluate("""() => {
            const selectors = [
                'body', 'header', 'nav', 'main', 'button', 'a',
                '[role="tab"]', '[role="button"]',
                '.card', '.tile', 'input', 'h1', 'h2', 'h3', 'p'
            ];
            const tokens = {};
            selectors.forEach(sel => {
                const els = document.querySelectorAll(sel);
                if (!els.length) return;
                const sample = els[0];
                const s = window.getComputedStyle(sample);
                tokens[sel] = {
                    fontFamily: s.fontFamily,
                    fontSize: s.fontSize,
                    fontWeight: s.fontWeight,
                    lineHeight: s.lineHeight,
                    color: s.color,
                    backgroundColor: s.backgroundColor,
                    borderRadius: s.borderRadius,
                    boxShadow: s.boxShadow,
                    padding: s.padding,
                    margin: s.margin
                };
            });
            return tokens;
        }""")

        with open(self.styles_dir / f"{snap_name}_computed_styles.json", "w", encoding="utf-8") as f:
            json.dump(style_tokens, f, indent=2, ensure_ascii=False)
        print(f"  -> Estilos computados guardados en {self.styles_dir}")

        # 3. Screenshot and full HTML
        page.screenshot(path=str(self.session_dir / f"{snap_name}.png"), full_page=False)
        with open(self.session_dir / f"{snap_name}.html", "w", encoding="utf-8") as f:
            f.write(page.content())
        print(f"  -> Captura de pantalla y snapshot HTML guardados como {snap_name}.png/html\n")

    def sweep_page(self, page, max_cards=40):
        """Automated walkthrough: smoothly sweeps hero slides and hover-previews on game cards (run on main thread)."""
        print("\n" + "=" * 70)
        print("   INICIANDO BARRIDO AUTOMATICO (SWEEP / AUTO-HOVER) DE LA PAGINA")
        print("=" * 70)

        # 1. Hero Banner Slides Sweep
        hero_buttons = page.query_selector_all(
            '[class*="hero"] button, [class*="banner"] button, [class*="indicator"], '
            '[class*="pagination"] button, [aria-label*="slide"], [aria-label*="banner"], '
            '[data-testid*="banner"], [data-testid*="hero"]'
        )
        if hero_buttons:
            print(f"[*] Encontrados {len(hero_buttons)} controles de banner Hero...")
            for i, btn in enumerate(hero_buttons[:6]):
                try:
                    if btn.is_visible():
                        btn.click()
                        print(f"  -> Slide {i+1} activado. Esperando 2.5s a que cargue el vídeo/banner...")
                        page.wait_for_timeout(2500)
                except Exception:
                    pass

        # 2. Game Cards / Tiles Sweep
        cards = page.query_selector_all(
            'a[href*="/game/"], a[href*="/games/"], [data-testid*="card"], [data-testid*="tile"], '
            '[data-asin], [class*="gameCard"], [class*="tile"], [role="article"]'
        )
        total = min(len(cards), max_cards)
        print(f"[*] Encontradas {len(cards)} tarjetas de juego en el DOM (recorriendo primeras {total})...")
        visited = 0
        for idx in range(total):
            card = cards[idx]
            try:
                card.scroll_into_view_if_needed(timeout=2000)
                page.wait_for_timeout(200)
                if card.is_visible():
                    txt = card.inner_text().split("\n")[0].strip() or f"Tarjeta #{idx+1}"
                    print(f"  [{idx+1}/{total}] Auto-hover en: '{txt[:35]}' (2s)...")
                    card.hover(timeout=2000)
                    page.wait_for_timeout(2000)
                    visited += 1
            except Exception:
                pass

        print(f"[+] Barrido completado con éxito. Se inspeccionaron {visited} tarjetas interactivas.\n")

    def update_browser_hud(self, page):
        try:
            page.evaluate("""(stats) => {
                const el = document.getElementById('__luna_sniffer_hud_stats');
                if (el) el.innerText = `APIs: ${stats.apis} | Img: ${stats.imgs} | Vid: ${stats.vids} | SVG: ${stats.svgs}`;
            }""", {
                "apis": self.api_counter,
                "imgs": len(self.asset_cache),
                "vids": self.video_counter,
                "svgs": self.svg_counter
            })
        except Exception:
            pass

    def execute_command_on_main_thread(self, page, cmd_line: str):
        """Executes a console or HUD command safely on the main thread inside Playwright's greenlet."""
        parts = cmd_line.split(maxsplit=1)
        action = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if action in ["exit", "quit", "q"]:
            print("[*] Cerrando suite de captura...")
            self.running = False
        elif action == "wait":
            secs = float(arg) if arg else 2.0
            print(f"[*] Esperando {secs:.1f}s...")
            page.wait_for_timeout(int(secs * 1000))
        elif action in ["sweep", "auto_hover"]:
            max_c = 10 if self.test_commands else 40
            self.sweep_page(page, max_cards=max_c)
        elif action == "snap":
            sname = arg or f"snapshot_{int(time.time())}"
            self.scrape_dom_assets(page, snap_name=sname)
        elif action == "assets":
            self.scrape_dom_assets(page, snap_name="manual_scrape")
        elif action == "videos":
            print(f"\n--- VIDEOS DESCARGADOS Y DETECTADOS ({self.video_counter}) ---")
            for k, v in self.detected_videos.items():
                print(f"  * [{v.get('cardTitle', 'N/A')}] {v.get('videoWidth')}x{v.get('videoHeight')} - Evento: {v.get('event')} - URL: {k[:80]}")
            print(f"Directorio de videos: {self.videos_dir}\n")
        elif action == "status":
            print(f"\n--- ESTADISTICAS DE SESION ---")
            print(f"Llamadas API registradas:   {self.api_counter}")
            print(f"Videos descargados/HLS:     {self.video_counter}")
            print(f"Videos detectados en DOM:   {len(self.detected_videos)}")
            print(f"Assets descargados:         {len(self.asset_cache)}")
            print(f"Iconos SVG extraídos:       {self.svg_counter}")
            print(f"Acciones registradas:       {len(self.recent_actions)}")
            print(f"Directorio de captura:      {self.session_dir}\n")
        else:
            print(f"[?] Comando desconocido: '{action}'. Opciones: sweep, snap <nombre>, assets, videos, status, exit")

    def run(self):
        print("=" * 80)
        print("   AMAZON LUNA - SUITE INTERACTIVA DE INGENIERIA INVERSA Y CAPTURA TOTAL")
        print("=" * 80)
        print(f"Directorio de sesion: {self.session_dir}")
        print(f"Perfil de navegador:  {self.profile_dir}")
        print(f"URL inicial:          {self.start_url}")
        print("-" * 80)
        print("Comandos disponibles en la terminal (o usa la barra flotante dentro de Chrome):")
        print("  sweep / auto_hover -> Recorre automaticamente tarjetas y Hero banners con hover")
        print("  snap <nombre>      -> Guarda snapshot completo (HTML + PNG + SVG + CSS Tokens)")
        print("  assets             -> Extrae todos los iconos SVG y estilos computados")
        print("  videos             -> Muestra lista de videos detectados y descargados")
        print("  status             -> Estadisticas de trafico API, videos y assets")
        print("  exit / quit        -> Cierra la sesion limpiamente")
        print("-" * 80)

        with sync_playwright() as p:
            args = [
                "--disable-blink-features=AutomationControlled",
                "--start-maximized",
                "--no-default-browser-check"
            ]

            user_agent = None
            if self.mode == "smarttv":
                user_agent = "Mozilla/5.0 (Web0S; SmartTV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36 WebAppManager LGDeviceInformation"

            context = p.chromium.launch_persistent_context(
                user_data_dir=str(self.profile_dir),
                channel="chrome",
                headless=self.headless,
                viewport={"width": 1920, "height": 1080},
                user_agent=user_agent,
                args=args
            )

            page = context.pages[0] if context.pages else context.new_page()

            # Enlace de funciones nativas entre DOM de Chrome y Python
            page.expose_function("__recordAction", self.record_action)
            page.expose_function("__recordVideo", self.record_video)
            page.expose_function("__sniffer_cmd", lambda cmd: self.cmd_queue.put(cmd))

            # Inyectar script de captura y panel HUD en el DOM
            page.add_init_script(INJECTED_CLIENT_JS)

            # Listeners de red
            page.on("request", self.on_request)
            page.on("response", self.on_response)

            print(f"[*] Navegando a {self.start_url} ...")
            try:
                page.goto(self.start_url, wait_until="domcontentloaded", timeout=30000)
            except Exception as e:
                print(f"[!] Aviso al cargar URL inicial: {e}")

            # Encolar comandos de prueba automática si se especificaron
            if self.test_commands:
                for c in self.test_commands.split(","):
                    c = c.strip()
                    if c:
                        self.cmd_queue.put(c)

            # Hilo secundario para leer stdin de forma no bloqueante (SIN tocar playwright)
            def stdin_worker():
                while self.running:
                    try:
                        line = sys.stdin.readline()
                        if not line:
                            break
                        cmd = line.strip()
                        if cmd:
                            self.cmd_queue.put(cmd)
                    except Exception:
                        break

            if not self.test_commands:
                t_stdin = threading.Thread(target=stdin_worker, daemon=True)
                t_stdin.start()

            # BUCLE PRINCIPAL (Ejecutado estrictamente en el hilo principal de Playwright)
            last_hud_update = 0
            while self.running:
                try:
                    # Bombea el bucle de eventos de Playwright/CDP cada 100ms
                    page.wait_for_timeout(100)
                except Exception:
                    print("[*] Ventana de navegador cerrada o proceso finalizado.")
                    break

                # Procesar comandos pendientes encolados desde terminal o desde el botón del navegador
                while not self.cmd_queue.empty():
                    try:
                        cmd = self.cmd_queue.get_nowait()
                        self.execute_command_on_main_thread(page, cmd)
                    except queue.Empty:
                        break
                    except Exception as err:
                        print(f"[!] Error ejecutando comando en hilo principal: {err}")

                # Actualizar estadísticas del panel HUD en el navegador
                now = time.time()
                if now - last_hud_update > 1.5:
                    last_hud_update = now
                    self.update_browser_hud(page)

            self.running = False
            try:
                self.actions_file.close()
                self.videos_detected_file.close()
                self.api_index_file.close()
                self.videos_index_file.close()
            except Exception:
                pass
            try:
                context.close()
            except Exception:
                pass
            print(f"\n[+] Sesion finalizada. Todos los datos fueron archivados en:\n    {self.session_dir}")

def main():
    parser = argparse.ArgumentParser(description="Suite de Sniffing e Ingenieria Inversa Interactiva de Amazon Luna con Captura Total de Videos")
    parser.add_argument("--name", type=str, default=None, help="Nombre de la sesion de captura (ej: captura_pagina_1_inicio)")
    parser.add_argument("--url", type=str, default="https://luna.amazon.es", help="URL inicial de Amazon Luna")
    parser.add_argument("--mode", type=str, choices=["desktop", "smarttv"], default="desktop", help="Modo de navegacion (desktop o smarttv)")
    parser.add_argument("--profile", type=str, default=None, help="Ruta al perfil de usuario de Chrome (por defecto: chrome_profile)")
    parser.add_argument("--headless", action="store_true", help="Ejecutar sin ventana grafica (para tests)")
    parser.add_argument("--test-commands", type=str, default=None, help="Comandos encolados separados por coma para test automatico (ej: 'status,sweep,snap test,exit')")
    args = parser.parse_args()

    sniffer = LunaPageSniffer(
        capture_name=args.name,
        start_url=args.url,
        mode=args.mode,
        profile_dir=args.profile,
        headless=args.headless,
        test_commands=args.test_commands
    )
    sniffer.run()

if __name__ == "__main__":
    main()
