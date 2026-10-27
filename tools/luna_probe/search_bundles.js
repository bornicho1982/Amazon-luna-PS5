const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'bundles');
const files = fs.readdirSync(dir);

function searchInBundles(term, maxMatches = 5) {
  console.log('=== SEARCH: ' + term + ' ===');
  let count = 0;
  for (const f of files) {
    if (!f.includes('.js')) continue;
    const full = path.join(dir, f);
    const content = fs.readFileSync(full, 'utf8');
    let idx = 0;
    while ((idx = content.indexOf(term, idx)) !== -1) {
      const snippet = content.slice(Math.max(0, idx - 100), Math.min(content.length, idx + 150)).replace(/[\r\n]+/g, ' ');
      console.log('[' + f + ']: ...' + snippet + '...');
      count++;
      if (count >= maxMatches) return;
      idx += term.length;
    }
  }
}

searchInBundles('game_detail', 3);
searchInBundles('multistate_navigation', 4);
searchInBundles('collection_similar_products', 3);
searchInBundles('remote_play', 3);
searchInBundles('mediaGallery', 3);
searchInBundles('previewVideos', 3);
searchInBundles('settings_', 3);
