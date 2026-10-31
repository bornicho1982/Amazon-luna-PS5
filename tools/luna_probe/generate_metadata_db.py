import glob
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../.."))
OUTPUT_H = os.path.join(ROOT, "apps/luna-ps5/src/session/luna_catalog_metadata_db.h")

games = {}

def clean_str(s):
    if not s: return ''
    s = str(s).replace('\r', ' ').replace('\n', ' ').strip()
    s = re.sub(r'\s+', ' ', s)
    return s

def escape_cpp(s):
    if not s: return '""'
    s = clean_str(s)
    s = s.replace('\\', '\\\\').replace('"', '\\"')
    return f'"{s}"'

def parse_pd(s):
    try: return json.loads(s)
    except: return None

def add_entry(title, asin, desc, pub, dev, pegi, genres, trailer, bg_land, tile_port, modes):
    if not title: return
    t = clean_str(title)
    if not t or t == "FEATURED": return
    # Skip category / time / filter tags
    if re.match(r"^(\d+[\s-]?jugadores|\d+\s*min)$", t, re.IGNORECASE): return
    if asin and asin.startswith("browse_genre"): return
    if t in ["FEATURED", "Acción", "Aventura", "Arcade", "Carreras", "Disparos", "Deportes", "Familia", "Lucha", "Lógica", "Rol", "Simulación", "Terror"]:
        if not trailer and not desc: return

    if t not in games:
        games[t] = {
            'title': t,
            'asin': clean_str(asin),
            'desc': clean_str(desc),
            'pub': clean_str(pub),
            'dev': clean_str(dev),
            'pegi': clean_str(pegi),
            'genres': clean_str(genres),
            'trailer': clean_str(trailer),
            'bg': clean_str(bg_land),
            'cover': clean_str(tile_port),
            'modes': clean_str(modes)
        }
    else:
        g = games[t]
        if asin and not g['asin']: g['asin'] = clean_str(asin)
        if desc and len(clean_str(desc)) > len(g['desc']): g['desc'] = clean_str(desc)
        if pub and not g['pub']: g['pub'] = clean_str(pub)
        if dev and not g['dev']: g['dev'] = clean_str(dev)
        if pegi and not g['pegi']: g['pegi'] = clean_str(pegi)
        if genres and not g['genres']: g['genres'] = clean_str(genres)
        if trailer and not g['trailer']: g['trailer'] = clean_str(trailer)
        if bg_land and not g['bg']: g['bg'] = clean_str(bg_land)
        if tile_port and not g['cover']: g['cover'] = clean_str(tile_port)
        if modes and not g['modes']: g['modes'] = clean_str(modes)

# 1. Dumps
for f in glob.glob(os.path.join(HERE, "dumps/real_detail_*.json")):
    try:
        d = json.load(open(f, encoding='utf-8'))
        widgets = d.get('response', {}).get('pageMemberGroups', {}).get('mainContent', {}).get('widgets', [])
        for w in widgets:
            if 'item_game_details' in w.get('id', ''):
                pd = parse_pd(w.get('presentationData'))
                if pd:
                    add_entry(pd.get('title'), pd.get('asin'), pd.get('description'),
                              pd.get('publishersText'), pd.get('developersText'),
                              pd.get('ageRating', {}).get('categoryId'), '', '',
                              pd.get('backgroundImageLandscape'), pd.get('tileImagePortrait'), pd.get('gameplayModes'))
    except Exception as e:
        print(f"Error reading dump {f}: {e}")

# 2. Blueprint
bp_file = os.path.join(HERE, "captures/captura_pagina_1_inicio/inicio_blueprint.json")
if os.path.exists(bp_file):
    try:
        bp = json.load(open(bp_file, encoding='utf-8'))
        for s in bp.get('sections', []):
            for it in s.get('items', []):
                t = it.get('title')
                asin = it.get('asin')
                raw = it.get('raw_pd', {})
                h = raw.get('hoverDetails', {})
                trailer = it.get('video_url') or h.get('trailerUrl')
                pegi = it.get('pegi_id') or h.get('ageRating', {}).get('categoryId')
                pub = h.get('publisher') or raw.get('publishersText') or (h.get('publishers', [None])[0] if isinstance(h.get('publishers'), list) else '')
                dev = raw.get('developersText')
                desc = h.get('description') or raw.get('description')
                modes = h.get('gameInputsString') or it.get('inputs')
                bg_land = it.get('bg_landscape') or raw.get('backgroundImageLandscape')
                tile_port = it.get('portrait_img') or raw.get('tileImagePortrait')
                add_entry(t, asin, desc, pub, dev, pegi, '', trailer, bg_land, tile_port, modes)
    except Exception as e:
        print(f"Error reading blueprint: {e}")

# 3. API
def scan_obj(o):
    if isinstance(o, dict):
        if 'presentationData' in o and isinstance(o['presentationData'], str):
            pd = parse_pd(o['presentationData'])
            if pd:
                title = pd.get('title') or pd.get('primaryText')
                if title:
                    h = pd.get('hoverDetails', {})
                    t_url = h.get('trailerUrl') or pd.get('videoUrl')
                    desc = pd.get('description') or h.get('description') or pd.get('secondaryText')
                    pub = pd.get('publishersText') or h.get('publisher') or (pd.get('publishers', [None])[0] if pd.get('publishers') else '')
                    dev = pd.get('developersText') or (pd.get('developers', [None])[0] if pd.get('developers') else '')
                    pegi = pd.get('ageRating', {}).get('categoryId') or h.get('ageRating', {}).get('categoryId')
                    asin = pd.get('asin') or pd.get('productId')
                    bg = pd.get('backgroundImageLandscape')
                    cover = pd.get('tileImagePortrait')
                    modes = h.get('gameInputsString') or pd.get('gameplayModes')
                    add_entry(title, asin, desc, pub, dev, pegi, '', t_url, bg, cover, modes)
        for v in o.values(): scan_obj(v)
    elif isinstance(o, list):
        for it in o: scan_obj(it)

for p in glob.glob(os.path.join(HERE, "captures/captura_pagina_1_inicio/api/*.json")):
    try:
        d = json.load(open(p, encoding='utf-8'))
        scan_obj(d)
    except: pass

print(f"Aggregated {len(games)} catalog items. Generating C++ DB...")

lines = [
    "#pragma once",
    "",
    "#include <string>",
    "#include <string_view>",
    "#include <algorithm>",
    "#include <cctype>",
    '#include "display/display_manager.h"',
    "",
    "namespace luna::session {",
    "",
    "struct CatalogMetadataEntry {",
    "    const char* asin;",
    "    const char* title;",
    "    const char* publisher;",
    "    const char* developer;",
    "    const char* pegi;",
    "    const char* genres;",
    "    const char* modes;",
    "    const char* trailerUrl;",
    "    const char* backdropUrl;",
    "    const char* coverUrl;",
    "    const char* synopsis;",
    "};",
    "",
    f"inline constexpr size_t kCatalogMetadataCount = {len(games)};",
    "inline const CatalogMetadataEntry kCatalogMetadataTable[] = {"
]

for g in sorted(games.values(), key=lambda x: x['title'].lower()):
    # Format pegi nicely: PEGI_AGE_12 -> PEGI 12
    pegi_clean = g['pegi']
    if "18" in pegi_clean: pegi_clean = "PEGI 18"
    elif "16" in pegi_clean: pegi_clean = "PEGI 16"
    elif "12" in pegi_clean: pegi_clean = "PEGI 12"
    elif "7" in pegi_clean: pegi_clean = "PEGI 7"
    elif "3" in pegi_clean: pegi_clean = "PEGI 3"
    else: pegi_clean = "PEGI 12"

    pub_clean = g['pub'] if g['pub'] else "Amazon Luna"
    dev_clean = g['dev'] if g['dev'] else pub_clean
    desc_clean = g['desc']
    if not desc_clean or "Disfruta de " in desc_clean:
        desc_clean = f"Disponible en Amazon Luna Cloud con graficos en alta definicion y soporte completo para el mando DualSense."

    modes_clean = g['modes'] if g['modes'] else "1 jugador • DualSense"
    genres_clean = g['genres'] if g['genres'] else "Accion • Aventura"

    entry_str = f"    {{ {escape_cpp(g['asin'])}, {escape_cpp(g['title'])}, {escape_cpp(pub_clean)}, {escape_cpp(dev_clean)}, {escape_cpp(pegi_clean)}, {escape_cpp(genres_clean)}, {escape_cpp(modes_clean)}, {escape_cpp(g['trailer'])}, {escape_cpp(g['bg'])}, {escape_cpp(g['cover'])}, {escape_cpp(desc_clean)} }},"
    lines.append(entry_str)

lines.extend([
    "};",
    "",
    "class LunaCatalogMetadataDB {",
    "public:",
    "    static inline std::string toLowerNorm(std::string_view s) {",
    "        std::string res;",
    "        res.reserve(s.size());",
    "        for (char c : s) {",
    "            if (std::isalnum(static_cast<unsigned char>(c))) {",
    "                res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));",
    "            }",
    "        }",
    "        return res;",
    "    }",
    "",
    "    static const CatalogMetadataEntry* lookup(const std::string& asin, const std::string& title) {",
    "        std::string lowTitle = toLowerNorm(title);",
    "        const CatalogMetadataEntry* bestMatch = nullptr;",
    "        for (size_t i = 0; i < kCatalogMetadataCount; ++i) {",
    "            const auto& e = kCatalogMetadataTable[i];",
    "            if (!asin.empty() && e.asin && asin == e.asin) {",
    "                return &e;",
    "            }",
    "            if (!lowTitle.empty() && e.title) {",
    "                std::string entryLow = toLowerNorm(e.title);",
    "                if (entryLow == lowTitle) {",
    "                    return &e;",
    "                }",
    "                if (!bestMatch && (entryLow.find(lowTitle) != std::string::npos || lowTitle.find(entryLow) != std::string::npos)) {",
    "                    bestMatch = &e;",
    "                }",
    "            }",
    "        }",
    "        return bestMatch;",
    "    }",
    "",
    "    static void enrich(display::GameTile& tile) {",
    "        const auto* meta = lookup(tile.id, tile.title);",
    "        if (!meta) return;",
    "        if (tile.videoUrl.empty() && meta->trailerUrl && meta->trailerUrl[0] != '\\0') {",
    "            tile.videoUrl = meta->trailerUrl;",
    "        }",
    "        if ((tile.synopsis.empty() || tile.synopsis.find(\"Disfruta de \") == 0) && meta->synopsis && meta->synopsis[0] != '\\0') {",
    "            tile.synopsis = meta->synopsis;",
    "        }",
    "        if ((tile.publisher.empty() || tile.publisher == \"Amazon Luna\") && meta->publisher && meta->publisher[0] != '\\0') {",
    "            tile.publisher = meta->publisher;",
    "        }",
    "        if ((tile.developer.empty() || tile.developer == \"Amazon Luna\") && meta->developer && meta->developer[0] != '\\0') {",
    "            tile.developer = meta->developer;",
    "        }",
    "        if ((tile.pegiRating.empty() || tile.pegiRating == \"PEGI 12\") && meta->pegi && meta->pegi[0] != '\\0') {",
    "            tile.pegiRating = meta->pegi;",
    "        }",
    "        if (tile.backdropUrl.empty() && meta->backdropUrl && meta->backdropUrl[0] != '\\0') {",
    "            tile.backdropUrl = meta->backdropUrl;",
    "        }",
    "        if (tile.coverUrl.empty() && meta->coverUrl && meta->coverUrl[0] != '\\0') {",
    "            tile.coverUrl = meta->coverUrl;",
    "        }",
    "    }",
    "};",
    "",
    "} // namespace luna::session",
    ""
])

with open(OUTPUT_H, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print(f"[+] Successfully wrote {OUTPUT_H} with {len(games)} entries.")
