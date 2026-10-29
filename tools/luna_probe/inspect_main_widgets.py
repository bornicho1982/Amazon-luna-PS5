import json

d = json.load(open('tools/luna_probe/captures/captura_pagina_1_inicio/api/011_POST_getPage.json', encoding='utf-8'))
pmg = d.get('response', {}).get('body', {}).get('pageMemberGroups', {})
main_widgets = pmg.get('mainContent', {}).get('widgets', [])

print(f'Total widgets in mainContent: {len(main_widgets)}')
for i, w in enumerate(main_widgets):
    wid = w.get('id')
    wtype = w.get('__type')
    raw_w = json.dumps(w)
    
    # check for subwidgets or items in this widget
    sub_widgets = w.get('widgets', [])
    items = w.get('items', [])
    
    print(f'\n--- [Widget {i}] id={wid} (type: {wtype[:40]}) ---')
    print(f'    sub-widgets: {len(sub_widgets)}, items: {len(items)}')
    
    # If it has sub-widgets:
    for s_idx, sw in enumerate(sub_widgets[:6]):
        sw_id = sw.get('id')
        pd_str = sw.get('presentationData')
        title = ''
        if pd_str:
            try:
                pd = json.loads(pd_str)
                title = pd.get('title') or pd.get('primaryText') or pd.get('header') or ''
                trailer = pd.get('hoverDetails', {}).get('trailerUrl') or ''
                if trailer:
                    title += f' (VIDEO: {trailer})'
            except:
                pass
        print(f'      sub [{s_idx}] id={sw_id} -> title={title}')
