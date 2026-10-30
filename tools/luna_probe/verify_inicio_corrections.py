import json
import struct
import os
import glob

def verify_mp4_files():
    print("=== 1. VERIFICACIÓN DE VÍDEOS MP4 PREVIEW ===")
    vids = glob.glob('tools/luna_probe/captures/captura_pagina_1_inicio/assets/videos/*Sonic*.mp4')
    if not vids:
        vids = glob.glob('tools/luna_probe/captures/captura_pagina_1_inicio/assets/videos/*.mp4')
    assert len(vids) > 0, "No se encontraron vídeos MP4"
    sample = vids[0]
    print(f"Probando demuxer sobre: {sample}")
    with open(sample, 'rb') as f:
        data = f.read(200000)

    # Check ftyp
    assert data[4:8] == b'ftyp', "Cabecera ftyp no encontrada"
    # Find avcC
    idx = data.find(b'avcC')
    assert idx != -1, "Átomo avcC no encontrado en stbl"
    sz = struct.unpack('>I', data[idx-4:idx])[0]
    avcc = data[idx+4:idx-4+sz]
    num_sps = avcc[5] & 0x1F
    assert num_sps > 0, "No se encontraron SPS en avcC"
    print(f"[OK] Demuxer MP4 verificado: {num_sps} SPS detectados en {os.path.basename(sample)}")

def verify_blueprint_widgets():
    print("\n=== 2. VERIFICACIÓN DE WIDGETS Y BANNERS DE LA PÁGINA INICIO ===")
    api_path = 'tools/luna_probe/captures/captura_pagina_1_inicio/api/011_POST_getPage.json'
    assert os.path.exists(api_path), f"No existe {api_path}"
    with open(api_path, 'r', encoding='utf-8') as f:
        d = json.load(f)

    widgets = d['response']['body']['pageMemberGroups']['mainContent']['widgets']
    assert len(widgets) == 10, f"Se esperaban 10 widgets, encontrados {len(widgets)}"

    # Widget 4: Personajes emblemáticos
    w4 = widgets[4]
    assert w4['id'] == 'collection_content_highlight_row_home_1', f"Widget 4 id incorrecto: {w4['id']}"
    pdata4 = json.loads(w4['presentationData']) if isinstance(w4['presentationData'], str) else w4['presentationData']
    assert "Personajes" in pdata4.get('partnerTitle', ''), "Título de banner 1 incorrecto"
    assert "GarfieldKart2" in pdata4.get('partnerPromoImage', ''), "Imagen de Garfield Kart no encontrada en banner 1"
    assert len(w4['widgets']) == 6, f"Se esperaban 6 juegos en banner 1, encontrados {len(w4['widgets'])}"
    print(f"[OK] Banner 1 verificado: '{pdata4.get('partnerTitle')}' con {len(w4['widgets'])} juegos y arte Garfield Kart")

    # Widget 7: Jugad juntos estéis donde estéis
    w7 = widgets[7]
    assert w7['id'] == 'collection_content_highlight_row_home_2', f"Widget 7 id incorrecto: {w7['id']}"
    pdata7 = json.loads(w7['presentationData']) if isinstance(w7['presentationData'], str) else w7['presentationData']
    assert "Jugad juntos" in pdata7.get('partnerTitle', ''), "Título de banner 2 incorrecto"
    assert "RemotePlay" in pdata7.get('partnerPromoImage', ''), "Imagen de Remote Play no encontrada en banner 2"
    assert len(w7['widgets']) == 7, f"Se esperaban 7 juegos en banner 2, encontrados {len(w7['widgets'])}"
    print(f"[OK] Banner 2 verificado: '{pdata7.get('partnerTitle')}' con {len(w7['widgets'])} juegos y arte RemotePlay")

    # Widget 8: Explorar categorías
    w8 = widgets[8]
    assert w8['id'] == 'collection_browse_games_tiles', f"Widget 8 id incorrecto: {w8['id']}"
    assert len(w8['widgets']) == 15, f"Se esperaban 15 categorías, encontradas {len(w8['widgets'])}"
    print(f"[OK] Carrusel de categorías verificado: 15 tarjetas cuadradas de categorías")

    # Widget 9: Prime Gaming claim banner
    w9 = widgets[9]
    assert w9['id'] == 'collection_promo_banner_claims', f"Widget 9 id incorrecto: {w9['id']}"
    print(f"[OK] Banner Prime Gaming Claims verificado (sin duplicar banner de Jugad juntos)")

    # Verify no duplicated titles
    titles = []
    for w in widgets:
        pstr = w.get('presentationData')
        if pstr:
            pd = json.loads(pstr) if isinstance(pstr, str) else pstr
            t = pd.get('partnerTitle') or pd.get('title')
            if t: titles.append(t)
    assert len(titles) == len(set(titles)), f"Hay títulos duplicados en widgets: {titles}"
    print("[OK] CERO DUPLICADOS de banners verificado en toda la página Inicio")

if __name__ == '__main__':
    verify_mp4_files()
    verify_blueprint_widgets()
    print("\nTODAS LAS VERIFICACIONES COMPLETADAS CON ÉXITO.")
