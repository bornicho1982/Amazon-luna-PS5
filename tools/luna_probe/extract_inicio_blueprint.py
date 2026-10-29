#!/usr/bin/env python3
"""
Extract Inicio Blueprint & Download All Videos/Assets from captura_pagina_1_inicio.
Scans all 1,370 API JSON files, HTML snapshots, and computed CSS tokens.
"""

import base64
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAPTURES_DIR = HERE / "captures" / "captura_pagina_1_inicio"
API_DIR = CAPTURES_DIR / "api"
ASSETS_DIR = CAPTURES_DIR / "assets"
VIDEOS_DIR = ASSETS_DIR / "videos"
MANIFESTS_DIR = VIDEOS_DIR / "manifests"
IMAGES_DIR = ASSETS_DIR / "images"
ICONS_DIR = ASSETS_DIR / "icons"
STYLES_DIR = CAPTURES_DIR / "styles"

for d in [VIDEOS_DIR, MANIFESTS_DIR, IMAGES_DIR, ICONS_DIR]:
    d.mkdir(parents=True, exist_ok=True)

VIDEO_EXT_REGEX = re.compile(r"\.(mp4|webm|m3u8|mpd|mov|ts)(\?.*)?$", re.I)
IMAGE_EXT_REGEX = re.compile(r"\.(png|jpg|jpeg|webp|avif)(\?.*)?$", re.I)

def find_urls_recursively(obj, path=""):
    """Recursively yields (key_path, string_value) for all strings in a nested JSON."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            yield from find_urls_recursively(v, f"{path}.{k}" if path else k)
    elif isinstance(obj, list):
        for idx, item in enumerate(obj):
            yield from find_urls_recursively(item, f"{path}[{idx}]")
    elif isinstance(obj, str):
        if obj.startswith("http://") or obj.startswith("https://") or VIDEO_EXT_REGEX.search(obj) or IMAGE_EXT_REGEX.search(obj):
            yield (path, obj)

def sanitize_filename(s: str, max_len=40) -> str:
    s = re.sub(r"[^A-Za-z0-9_-]", "_", s)
    return s[:max_len]

def download_video(url: str, output_path: Path):
    """Downloads an MP4/WebM file or uses FFmpeg for HLS streams."""
    if output_path.exists() and output_path.stat().st_size > 1024:
        return output_path.stat().st_size

    if ".m3u8" in url.lower():
        # Download HLS with FFmpeg
        cmd = [
            "ffmpeg", "-y",
            "-loglevel", "error",
            "-headers", "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
            "-i", url,
            "-c", "copy",
            "-bsf:a", "aac_adtstoasc",
            str(output_path)
        ]
        try:
            res = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=45)
            if res.returncode == 0 and output_path.exists():
                return output_path.stat().st_size
        except Exception:
            return 0
    else:
        # Direct HTTP download
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"})
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = resp.read()
                if len(data) > 1024:
                    with open(output_path, "wb") as f:
                        f.write(data)
                    return len(data)
        except Exception as e:
            return 0
    return 0

def main():
    print("=" * 80)
    print("   EXTRACCION DEL BLUEPRINT DE 'INICIO' Y AUDITORIA MASIVA DE VIDEOS")
    print("=" * 80)

    json_files = sorted(glob.glob(str(API_DIR / "*.json")))
    print(f"[*] Analizando {len(json_files)} archivos JSON en {API_DIR}...")

    discovered_videos = {}  # url -> {asin, title, key, files: []}
    discovered_images = {}  # url -> {asin, title, key, count}
    game_metadata = {}      # asin -> {title, genres, players, pegi, desc, buttons: [], preview_video, thumbnail}
    
    # Trackers for Inicio Page structure
    home_page_data = None
    navigation_data = None
    hover_calls = []

    for fpath in json_files:
        try:
            with open(fpath, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception:
            continue

        url = data.get("url", "")
        req = data.get("request", {})
        payload = req.get("payload") or {}
        if not isinstance(payload, dict):
            payload = {}
        res_body = data.get("response", {}).get("body", {})
        action = data.get("correlated_action")

        # Decode serviceToken if present
        st = payload.get("serviceToken", "")
        decoded_st = {}
        if st:
            try:
                decoded_st = json.loads(base64.b64decode(st).decode("utf-8", errors="ignore"))
            except Exception:
                pass

        page_ctx = payload.get("pageContext", {})
        page_type = page_ctx.get("pageType") or decoded_st.get("pageType")
        page_id = page_ctx.get("pageId") or decoded_st.get("pageId")
        page_uri = page_ctx.get("pageUri")

        if page_type == "multistate_navigation" or "navigation" in str(page_id):
            if not navigation_data:
                navigation_data = res_body

        if page_uri == "home" or page_id == "default" and page_type == "home":
            if not home_page_data:
                home_page_data = res_body

        # Track hover calls
        if action and action.get("type") == "hover":
            hover_calls.append({
                "file": os.path.basename(fpath),
                "action": action,
                "decoded_serviceToken": decoded_st,
                "pageType": page_type,
                "pageId": page_id
            })

        # Scan for game details / hover popup details
        if isinstance(res_body, dict):
            # Extract metadata from Coral SpiderPork structures
            pmg = res_body.get("pageMemberGroups", {})
            for group_name, group_val in pmg.items():
                if not isinstance(group_val, dict):
                    continue
                for widget in group_val.get("widgets", []):
                    wtype = widget.get("__type", "")
                    wid = widget.get("id", "")
                    # Look for game metadata, titles, buttons
                    title = widget.get("title") or widget.get("header") or widget.get("name")
                    asin = widget.get("asin") or widget.get("gameId") or page_id
                    if asin and title and asin not in game_metadata:
                        game_metadata[asin] = {"title": title, "id": wid, "type": wtype}

        # Recursively search all strings for video and image URLs
        for key_path, val in find_urls_recursively(data):
            # Video detection
            if VIDEO_EXT_REGEX.search(val) or "video" in key_path.lower() or "trailer" in key_path.lower() or "preview" in key_path.lower():
                if val.startswith("http"):
                    if val not in discovered_videos:
                        discovered_videos[val] = {
                            "key_path": key_path,
                            "files": [os.path.basename(fpath)],
                            "page_id": page_id
                        }
                    else:
                        discovered_videos[val]["files"].append(os.path.basename(fpath))

            # Image detection
            elif IMAGE_EXT_REGEX.search(val) and val.startswith("http"):
                if val not in discovered_images:
                    discovered_images[val] = 1
                else:
                    discovered_images[val] += 1

    print(f"\n[+] Auditoria completada:")
    print(f"  - URLs de vídeo únicas descubiertas en JSONs: {len(discovered_videos)}")
    print(f"  - URLs de imágenes únicas descubiertas en JSONs: {len(discovered_images)}")
    print(f"  - Metadatos de juegos extraídos: {len(game_metadata)}")
    print(f"  - Peticiones asociadas a hover interactivo: {len(hover_calls)}")

    # 1. Download all discovered videos
    print("\n" + "=" * 70)
    print("   DESCARGA MASIVA DE TODOS LOS VIDEOS EXTRAIDOS A DISCO")
    print("=" * 70)

    downloaded_videos = []
    for idx, (v_url, v_info) in enumerate(discovered_videos.items(), start=1):
        parsed = urllib.parse.urlparse(v_url)
        v_hash = hashlib.md5(v_url.encode()).hexdigest()[:10]
        ext = ".mp4"
        if ".webm" in parsed.path.lower():
            ext = ".webm"
        elif ".m3u8" in parsed.path.lower():
            ext = ".mp4"  # converted by ffmpeg

        pid = v_info.get("page_id") or "video"
        clean_pid = sanitize_filename(pid.replace("amzn1.luna.product.", "").replace("amzn1.adg.product.", ""), max_len=24)
        target_name = f"{idx:03d}_{clean_pid}_{v_hash}{ext}"
        target_file = VIDEOS_DIR / target_name

        print(f"[{idx}/{len(discovered_videos)}] Descargando: {target_name} <- {v_url[:75]}...")
        sz = download_video(v_url, target_file)
        if sz > 0:
            sz_mb = sz / (1024 * 1024)
            print(f"  -> \033[92mOK\033[0m Guardado ({sz_mb:.2f} MB)")
            downloaded_videos.append({
                "file": target_name,
                "url": v_url,
                "size_mb": round(sz_mb, 2),
                "key_path": v_info.get("key_path"),
                "page_id": v_info.get("page_id")
            })
        else:
            print(f"  -> \033[91mFallo o no accesible\033[0m")

    # 2. Extract Structure of 'Inicio' from home_page_data or files
    print("\n" + "=" * 70)
    print("   ESTRUCTURA EXACTA DE LA PAGINA PRINCIPAL ('INICIO')")
    print("=" * 70)

    home_structure = {
        "navigation": {},
        "hero_rotator": [],
        "game_rows": [],
        "purple_banners": [],
        "categories": [],
        "footer": {}
    }

    # Find the main home page files
    home_files = [f for f in json_files if "008_POST_getPage" in f or "011_POST_getPage" in f]
    for hf in home_files:
        try:
            with open(hf, "r", encoding="utf-8") as f:
                d = json.load(f)
            body = d.get("response", {}).get("body", {})
            pmg = body.get("pageMemberGroups", {})
            main_content = pmg.get("mainContent", {}).get("widgets", [])
            print(f"[*] Analizando widgets en {os.path.basename(hf)}: {len(main_content)} widgets")

            for widx, widget in enumerate(main_content):
                wid = widget.get("id", "")
                wtype = widget.get("__type", "")
                props = widget.get("properties", {}) or {}
                items = widget.get("items", []) or []
                header = widget.get("header") or widget.get("title") or wid

                print(f"  [{widx:02d}] id={wid} | type={wtype[:45]} | items={len(items)}")

                # Classify section
                if "hero" in wid.lower() or "rotator" in wid.lower():
                    home_structure["hero_rotator"].append({
                        "id": wid,
                        "items_count": len(items),
                        "items": items[:5]
                    })
                elif "category" in wid.lower() or "explorar" in str(header).lower():
                    home_structure["categories"].append({
                        "id": wid,
                        "header": header,
                        "items": items
                    })
                elif "banner" in wid.lower() or "brand" in wid.lower():
                    home_structure["purple_banners"].append({
                        "id": wid,
                        "header": header,
                        "widget": widget
                    })
                elif len(items) > 0 or "collection" in wid.lower():
                    home_structure["game_rows"].append({
                        "id": wid,
                        "title": header,
                        "item_count": len(items),
                        "sample_tiles": [it.get("title") or it.get("asin") for it in items[:4]]
                    })
        except Exception as e:
            print(f"[!] Error leyendo {hf}: {e}")

    # Save blueprint JSON
    blueprint_path = CAPTURES_DIR / "inicio_blueprint.json"
    blueprint_data = {
        "timestamp": datetime.datetime.now().isoformat(),
        "total_api_calls": len(json_files),
        "discovered_videos_count": len(discovered_videos),
        "downloaded_videos": downloaded_videos,
        "unique_images_count": len(discovered_images),
        "hover_calls_sample": hover_calls[:20],
        "home_structure": home_structure
    }
    with open(blueprint_path, "w", encoding="utf-8") as f:
        json.dump(blueprint_data, f, indent=2, ensure_ascii=False)

    print(f"\n[+] Blueprint completo guardado en: {blueprint_path}")
    print(f"[+] Total de videos descargados en disco: {len(downloaded_videos)}")

if __name__ == "__main__":
    main()
