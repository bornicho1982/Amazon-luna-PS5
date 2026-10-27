const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, 'bundles');
const files = fs.readdirSync(dir);

function searchInAll(terms) {
  for (const term of terms) {
    console.log(`\n=================== SEARCH: ${term} ===================`);
    let matches = 0;
    for (const f of files) {
      if (!f.includes('.js')) continue;
      const content = fs.readFileSync(path.join(dir, f), 'utf8');
      let idx = 0;
      while ((idx = content.indexOf(term, idx)) !== -1) {
        const snippet = content.slice(Math.max(0, idx - 120), Math.min(content.length, idx + 200)).replace(/[\r\n]+/g, ' ');
        console.log(`[${f}]: ...${snippet}...`);
        matches++;
        if (matches >= 4) break;
        idx += term.length;
      }
      if (matches >= 4) break;
    }
  }
}

searchInAll([
  'collection_trailer_screenshots',
  'trailer',
  'videoUrl',
  'previewVideo',
  'collection_similar_products',
  'item_game_details',
  'synopsis'
]);
