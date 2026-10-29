import glob
import hashlib
import json
import os
import re
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAPTURES_DIR = HERE / "captures" / "captura_pagina_1_inicio"
API_DIR = CAPTURES_DIR / "api"
VIDEOS_DIR = CAPTURES_DIR / "assets" / "videos"
VIDEOS_DIR.mkdir(parents=True, exist_ok=True)

print("=" * 80)
print("   EXTRAYENDO Y DESCARGANDO VIDEOS DE LA PAGINA 1 ('INICIO')")
print("=" * 80)

# Load 011_POST_getPage.json as primary source
home_file = API_DIR / "011_POST_getPage.json"
home_data = json.load(open(home_file, encoding="utf-8"))
pmg = home_data["response"]["body"]["pageMemberGroups"]
main_widgets = pmg["mainContent"]["widgets"]

blueprint = {
    "sections": []
}

total_videos_found = {}

def sanitize_filename(s: str, max_len=30) -> str:
    s = re.sub(r"[^A-Za-z0-9_-]", "_", s)
    return s[:max_len]

def download_video(url: str, target_path: Path):
    if target_path.exists() and target_path.stat().st_size > 1024:
        return target_path.stat().st_size
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"})
        with urllib.request.urlopen(req, timeout=25) as resp:
            data = resp.read()
            if len(data) > 1024:
                with open(target_path, "wb") as f:
                    f.write(data)
                return len(data)
    except Exception as e:
        return 0
    return 0

for widx, widget in enumerate(main_widgets):
    wid = widget.get("id")
    wtype = widget.get("__type")
    sub_widgets = widget.get("widgets", [])
    
    sec_info = {
        "id": wid,
        "index": widx,
        "type": wtype,
        "items": []
    }
    
    for s_idx, sw in enumerate(sub_widgets):
        sw_id = sw.get("id")
        pd_str = sw.get("presentationData")
        if not pd_str:
            continue
        try:
            pd = json.loads(pd_str)
        except:
            continue
            
        title = pd.get("title") or pd.get("primaryText") or pd.get("header") or ""
        asin = pd.get("productId") or pd.get("asin") or sw_id
        hover = pd.get("hoverDetails") or {}
        trailer_url = hover.get("trailerUrl")
        age_rating = hover.get("ageRating") or {}
        pegi_id = age_rating.get("categoryId") or ""
        pegi_img = age_rating.get("categoryImageUrl") or ""
        inputs_str = hover.get("gameInputsString") or ""
        primary_btn = hover.get("primaryButtonDefaultText") or "Jugar ahora"
        sec_btn = hover.get("secondaryButtonText") or "Más información"
        
        # Images
        portrait_img = pd.get("tileImagePortrait") or ""
        landscape_img = pd.get("tileImageLandscape") or ""
        bg_portrait = pd.get("backgroundImagePortrait") or ""
        bg_landscape = pd.get("backgroundImageLandscape") or ""
        
        video_filename = ""
        if trailer_url and trailer_url.startswith("http"):
            u_hash = hashlib.md5(trailer_url.encode()).hexdigest()[:8]
            clean_t = sanitize_filename(title) or "game"
            video_filename = f"{clean_t}_{u_hash}.mp4"
            total_videos_found[trailer_url] = {
                "title": title,
                "asin": asin,
                "filename": video_filename
            }
            
        item_entry = {
            "id": sw_id,
            "title": title,
            "asin": asin,
            "portrait_img": portrait_img,
            "landscape_img": landscape_img,
            "bg_landscape": bg_landscape,
            "video_url": trailer_url,
            "video_file": video_filename,
            "pegi_id": pegi_id,
            "pegi_img": pegi_img,
            "inputs": inputs_str,
            "primary_btn": primary_btn,
            "sec_btn": sec_btn,
            "raw_pd": pd
        }
        sec_info["items"].append(item_entry)
        
    blueprint["sections"].append(sec_info)

print(f"Total secciones estructuradas: {len(blueprint['sections'])}")
print(f"Total videos únicos en la página Inicio: {len(total_videos_found)}")

# Also search all other getPage files for additional videos
json_files = glob.glob(str(API_DIR / "*getPage*.json"))
for jf in json_files:
    try:
        raw = open(jf, encoding="utf-8").read()
        mp4s = re.findall(r'https://m\.media-amazon\.com/images/I/[A-Za-z0-9+-]+\.mp4', raw)
        for u in set(mp4s):
            if u not in total_videos_found:
                u_hash = hashlib.md5(u.encode()).hexdigest()[:8]
                vname = f"preview_{u_hash}.mp4"
                total_videos_found[u] = {
                    "title": "Additional Preview",
                    "asin": "",
                    "filename": vname
                }
    except Exception:
        pass

print(f"Total videos unicos en todo el capture de Inicio: {len(total_videos_found)}")

# Download all videos
downloaded_count = 0
for idx, (v_url, v_info) in enumerate(total_videos_found.items(), start=1):
    target = VIDEOS_DIR / v_info["filename"]
    print(f"[{idx}/{len(total_videos_found)}] Descargando {v_info['filename']} ({v_info['title']})...")
    sz = download_video(v_url, target)
    if sz > 0:
        downloaded_count += 1
        print(f"   -> OK ({sz / (1024*1024):.2f} MB)")
    else:
        print(f"   -> Omitido o ya existente")

blueprint["total_downloaded_videos"] = downloaded_count
bp_path = CAPTURES_DIR / "inicio_blueprint.json"
with open(bp_path, "w", encoding="utf-8") as f:
    json.dump(blueprint, f, indent=2, ensure_ascii=False)

print(f"\n[+] Blueprint guardado en: {bp_path}")
print(f"[+] Total de videos descargados a disco: {downloaded_count}")
