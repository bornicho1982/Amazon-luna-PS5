#!/usr/bin/env python3
import sys

path = "apps/luna-ps5/src/auth/auth_server.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

start_marker = "std::string AuthServer::generateLunaWebPage() const {"
end_marker = "bool AuthServer::start(int port) {"

start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx == -1 or end_idx == -1:
    print(f"Error: markers not found (start={start_idx}, end={end_idx})")
    sys.exit(1)

new_func = r'''std::string AuthServer::generateLunaWebPage() const {
    std::string html = R"raw(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Amazon Luna - PlayStation 5</title>
<style>
  :root {
    --bg-main: #0E0B16;
    --card-bg: #1A1429;
    --card-hover: #261D3D;
    --accent-purple: #9146FF;
    --accent-cyan: #02FDDB;
    --text-primary: #FFFFFF;
    --text-secondary: #94A3B8;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; outline: none; }
  body {
    background-color: var(--bg-main);
    color: var(--text-primary);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    overflow-x: hidden;
    user-select: none;
    -webkit-user-select: none;
  }
  header {
    position: sticky;
    top: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 48px;
    background: rgba(14, 11, 22, 0.96);
    border-bottom: 1px solid #231B38;
    backdrop-filter: blur(12px);
  }
  .header-left { display: flex; align-items: center; gap: 32px; }
  .logo-area { display: flex; align-items: center; gap: 12px; cursor: pointer; }
  .logo-badge {
    width: 24px; height: 24px;
    background: linear-gradient(135deg, var(--accent-cyan), var(--accent-purple));
    border-radius: 6px;
    transform: rotate(45deg);
  }
  .logo-text { font-size: 26px; font-weight: 800; letter-spacing: -0.5px; }
  nav { display: flex; gap: 28px; font-size: 15px; font-weight: 600; color: var(--text-secondary); }
  nav a { color: inherit; text-decoration: none; cursor: pointer; padding: 6px 12px; border-radius: 20px; transition: 0.2s; }
  nav a.active, nav a:hover, nav a.focused { color: #FFF; background: rgba(255,255,255,0.08); }
  nav a.focused { box-shadow: 0 0 0 2px var(--accent-cyan); }
  .header-right { display: flex; align-items: center; gap: 20px; }
  .search-box {
    display: flex;
    align-items: center;
    background: #1C152E;
    border: 1px solid #2D2248;
    border-radius: 20px;
    padding: 6px 16px;
    gap: 8px;
  }
  .search-box input {
    background: transparent;
    border: none;
    color: #FFF;
    font-size: 14px;
    width: 180px;
  }
  .search-box.focused { border-color: var(--accent-cyan); box-shadow: 0 0 0 2px rgba(2,253,219,0.3); }
  .profile-badge {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 14px;
    background: #1C152E;
    border: 1px solid #2D2248;
    border-radius: 20px;
    font-size: 13px;
    font-weight: 600;
  }
  .profile-badge .dot {
    width: 8px; height: 8px;
    background: #10B981;
    border-radius: 50%;
    box-shadow: 0 0 8px #10B981;
  }

  /* HERO BANNER */
  .hero-banner {
    position: relative;
    width: 100%;
    height: 520px;
    background-size: cover;
    background-position: center top;
    display: flex;
    align-items: flex-end;
    padding: 40px 60px;
    background-color: #140F24;
    transition: background-image 0.4s ease-in-out;
  }
  .hero-gradient {
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, #0E0B16 0%, rgba(14,11,22,0.85) 45%, rgba(14,11,22,0.15) 100%),
                linear-gradient(0deg, #0E0B16 0%, transparent 60%);
  }
  .hero-content {
    position: relative;
    z-index: 10;
    max-width: 720px;
  }
  .hero-badge {
    display: inline-block;
    background: var(--accent-purple);
    color: #FFF;
    padding: 4px 12px;
    border-radius: 4px;
    font-size: 12px;
    font-weight: 700;
    margin-bottom: 14px;
    letter-spacing: 0.5px;
  }
  .hero-title { font-size: 44px; font-weight: 800; line-height: 1.15; margin-bottom: 12px; text-shadow: 0 2px 10px rgba(0,0,0,0.6); }
  .hero-desc { font-size: 15px; color: #CBD5E1; line-height: 1.5; margin-bottom: 24px; max-width: 600px; text-shadow: 0 2px 6px rgba(0,0,0,0.8); }
  .hero-actions { display: flex; gap: 16px; align-items: center; }
  .btn-play {
    background: #FFFFFF;
    color: #0E0B16;
    border: none;
    padding: 14px 32px;
    border-radius: 30px;
    font-size: 16px;
    font-weight: 800;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 10px;
    box-shadow: 0 4px 16px rgba(255,255,255,0.25);
    transition: transform 0.15s, background 0.15s, box-shadow 0.15s;
  }
  .btn-play::before { content: '▶'; font-size: 14px; color: var(--accent-purple); }
  .btn-play:hover, .btn-play.focused {
    background: #F1F5F9;
    transform: scale(1.05);
    box-shadow: 0 0 0 3px #FFF, 0 8px 24px rgba(255,255,255,0.4);
  }
  .btn-info {
    background: rgba(255,255,255,0.12);
    color: #FFF;
    border: 1px solid rgba(255,255,255,0.25);
    padding: 14px 26px;
    border-radius: 30px;
    font-size: 15px;
    font-weight: 700;
    cursor: pointer;
    backdrop-filter: blur(8px);
    transition: 0.15s;
  }
  .btn-info:hover, .btn-info.focused {
    background: rgba(255,255,255,0.25);
    transform: scale(1.05);
    box-shadow: 0 0 0 2px #FFF;
  }

  /* SWIMLANES & HORIZONTAL ROWS */
  .swimlane { padding: 24px 60px; }
  .swimlane-title { font-size: 24px; font-weight: 800; margin-bottom: 18px; color: #FFFFFF; }
  .swimlane-row {
    display: flex;
    gap: 22px;
    overflow-x: auto;
    padding: 10px 4px 24px 4px;
    scroll-behavior: smooth;
    scrollbar-width: none;
  }
  .swimlane-row::-webkit-scrollbar { display: none; }

  /* GAME CARD */
  .game-card-wrapper { display: flex; align-items: flex-end; flex-shrink: 0; }
  .rank-number {
    font-size: 96px;
    font-weight: 900;
    line-height: 0.8;
    color: #332650;
    margin-right: -24px;
    z-index: 1;
    font-style: italic;
    text-shadow: 2px 2px 0px #0E0B16, -1px -1px 0 #4B3A70;
  }
  .game-card {
    width: 200px;
    background: var(--card-bg);
    border-radius: 12px;
    overflow: hidden;
    cursor: pointer;
    transition: transform 0.2s, box-shadow 0.2s;
    border: 2px solid transparent;
    flex-shrink: 0;
    position: relative;
    z-index: 2;
  }
  .game-card:hover, .game-card.focused {
    transform: translateY(-8px) scale(1.04);
    box-shadow: 0 0 0 3px #FFFFFF, 0 14px 30px rgba(145, 70, 255, 0.6);
    border-color: #FFFFFF;
    z-index: 10;
  }
  .card-cover {
    width: 100%;
    height: 275px;
    object-fit: cover;
    background: #231B38;
    display: block;
  }
  .card-info { padding: 12px 14px; }
  .card-title { font-size: 14px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; color: #FFF; }
  .card-sub { font-size: 12px; color: var(--text-secondary); margin-top: 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

  /* MODAL FICHA DE JUEGO */
  .modal-backdrop {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 200;
    background: rgba(14, 11, 22, 0.88);
    backdrop-filter: blur(12px);
    align-items: center;
    justify-content: center;
    padding: 20px;
  }
  .modal {
    background: #181226;
    border: 1px solid #3B2A60;
    border-radius: 20px;
    max-width: 850px;
    width: 100%;
    overflow: hidden;
    box-shadow: 0 25px 60px rgba(0,0,0,0.9);
    position: relative;
  }
  .modal-hero {
    position: relative;
    width: 100%;
    height: 320px;
    background-size: cover;
    background-position: center;
    background-color: #231B38;
  }
  .modal-hero-gradient {
    position: absolute;
    inset: 0;
    background: linear-gradient(0deg, #181226 0%, rgba(24,18,38,0.7) 50%, rgba(24,18,38,0.2) 100%);
  }
  .modal-body { padding: 28px 36px; }
  .modal-title { font-size: 34px; font-weight: 800; margin-bottom: 8px; color: #FFF; }
  .modal-meta-row { display: flex; gap: 16px; align-items: center; font-size: 14px; color: var(--text-secondary); margin-bottom: 16px; flex-wrap: wrap; }
  .modal-pegi { background: #3B2A60; color: #FFF; padding: 2px 8px; border-radius: 4px; font-weight: 700; font-size: 12px; }
  .modal-synopsis { font-size: 15px; color: #CBD5E1; line-height: 1.6; margin-bottom: 28px; max-height: 140px; overflow-y: auto; }
  .modal-actions { display: flex; justify-content: space-between; align-items: center; }
  .btn-close {
    background: transparent;
    border: 1px solid #4B3A70;
    color: #CBD5E1;
    padding: 12px 28px;
    border-radius: 30px;
    font-weight: 700;
    cursor: pointer;
    transition: 0.15s;
  }
  .btn-close:hover, .btn-close.focused { background: rgba(255,255,255,0.1); border-color: #FFF; color: #FFF; }

  /* OVERLAY INICIANDO PARTIDA */
  .launch-overlay {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 500;
    background: radial-gradient(circle at center, #1E1238 0%, #070510 100%);
    flex-direction: column;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 40px;
  }
  .launch-spinner {
    width: 64px; height: 64px;
    border: 5px solid rgba(145, 70, 255, 0.2);
    border-top-color: var(--accent-cyan);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin-bottom: 24px;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .launch-title { font-size: 32px; font-weight: 800; margin-bottom: 14px; }
  .launch-sub { font-size: 18px; color: var(--accent-cyan); margin-bottom: 24px; font-weight: 700; }
  .launch-box {
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.15);
    border-radius: 16px;
    padding: 20px 32px;
    max-width: 640px;
    font-size: 16px;
    line-height: 1.6;
    color: #F1F5F9;
  }
  .launch-box strong { color: #FCD34D; font-size: 18px; }

  /* DUALSENSE HUD */
  .hud-hints {
    position: fixed;
    bottom: 20px;
    right: 30px;
    z-index: 90;
    display: flex;
    gap: 16px;
    background: rgba(14, 11, 22, 0.85);
    border: 1px solid #231B38;
    padding: 8px 18px;
    border-radius: 20px;
    font-size: 13px;
    color: #94A3B8;
    backdrop-filter: blur(8px);
  }
  .hud-hints span { display: inline-flex; align-items: center; gap: 6px; }
  .hud-key {
    background: #2D2248;
    color: #FFF;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 11px;
    font-weight: 800;
  }
</style>
</head>
<body>

<header>
  <div class="header-left">
    <div class="logo-area" onclick="scrollToTop()">
      <div class="logo-badge"></div>
      <div class="logo-text">luna</div>
    </div>
    <nav id="navTabs">
      <a class="active nav-item" data-tab="home">Inicio</a>
      <a class="nav-item" data-tab="library">Biblioteca</a>
      <a class="nav-item" data-tab="gamenight">GameNight</a>
    </nav>
  </div>
  <div class="header-right">
    <div class="search-box" id="searchBox">
      <span>🔍</span>
      <input type="text" id="searchInput" placeholder="Buscar juegos..." oninput="onSearchInput(this.value)">
    </div>
    <div class="profile-badge">
      <div class="dot"></div>
      <span id="profileName">Bornicho #8663</span>
    </div>
  </div>
</header>

<div class="hero-banner" id="heroBanner">
  <div class="hero-gradient"></div>
  <div class="hero-content">
    <div class="hero-badge" id="heroBadge">DESTACADO EN AMAZON LUNA</div>
    <div class="hero-title" id="heroTitle">Cargando catálogo oficial...</div>
    <div class="hero-desc" id="heroDesc">Conectando con el motor nativo de PS5 a 1080p 60 FPS...</div>
    <div class="hero-actions">
      <button class="btn-play focusable" id="heroPlayBtn" onclick="launchCurrentHero()">Jugar ahora en PS5</button>
      <button class="btn-info focusable" id="heroInfoBtn" onclick="openCurrentHeroInfo()">Más información</button>
    </div>
  </div>
</div>

<div id="swimlanesContainer"></div>

<!-- Modal Detalle -->
<div class="modal-backdrop" id="gameModal" onclick="closeModal(event)">
  <div class="modal" onclick="event.stopPropagation()">
    <div class="modal-hero" id="mHero">
      <div class="modal-hero-gradient"></div>
    </div>
    <div class="modal-body">
      <div class="modal-title" id="mTitle"></div>
      <div class="modal-meta-row">
        <span class="modal-pegi" id="mPegi">PEGI 12</span>
        <span id="mPublisher">Amazon Games</span>
        <span>•</span>
        <span id="mGenres">Acción</span>
        <span>•</span>
        <span>🎮 Mando DualSense</span>
      </div>
      <div class="modal-synopsis" id="mSynopsis"></div>
      <div class="modal-actions">
        <button class="btn-close focusable" id="mCloseBtn" onclick="closeModal()">Cerrar</button>
        <button class="btn-play focusable" id="mPlayBtn">Jugar ahora en PS5</button>
      </div>
    </div>
  </div>
</div>

<!-- Launch Overlay -->
<div class="launch-overlay" id="launchOverlay">
  <div class="launch-spinner"></div>
  <div class="launch-title" id="launchTitle">¡Iniciando juego en el motor nativo de PS5!</div>
  <div class="launch-sub" id="launchSub">Conectando WebRTC a 1080p 60 FPS...</div>
  <div class="launch-box">
    <strong>Pulsa CÍRCULO (O) para cerrar esta ventana del navegador</strong><br>
    y pasar directamente a la pantalla completa del juego con decodificación por hardware libSceVideodec2 y SceAudioOut.
  </div>
</div>

<div class="hud-hints">
  <span><span class="hud-key">X</span> Seleccionar / Jugar</span>
  <span><span class="hud-key">O</span> Volver / Salir</span>
  <span><span class="hud-key">L1/R1</span> Pestañas</span>
  <span><span class="hud-key">▲/▼/◄/►</span> Navegar</span>
</div>

<script>
let catalogGames = [];
let collectionsData = [];
let heroGame = null;
let currentModalGame = null;
let focusables = [];
let focusIndex = 0;

function scrollToTop() {
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

function updateFocusables() {
  focusables = Array.from(document.querySelectorAll('.focusable, .game-card, .nav-item'));
  if (document.getElementById('gameModal').style.display === 'flex') {
    focusables = Array.from(document.querySelectorAll('#gameModal .focusable'));
  }
}

function setFocus(index) {
  updateFocusables();
  if (focusables.length === 0) return;
  if (index < 0) index = 0;
  if (index >= focusables.length) index = focusables.length - 1;
  focusIndex = index;
  focusables.forEach((el, idx) => {
    if (idx === focusIndex) {
      el.classList.add('focused');
      el.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
    } else {
      el.classList.remove('focused');
    }
  });
}

function launchGame(gameId, gameTitle) {
  const overlay = document.getElementById('launchOverlay');
  document.getElementById('launchTitle').innerText = '¡Iniciando ' + (gameTitle || 'juego') + ' en PS5!';
  overlay.style.display = 'flex';

  fetch('/api/play?asin=' + encodeURIComponent(gameId), { method: 'POST' })
    .then(r => r.json())
    .then(res => {
      console.log('Juego lanzado:', res);
      // Intenta cerrar la ventana web del navegador de PS5 automáticamente
      try { window.close(); } catch(e) {}
    })
    .catch(err => {
      console.error('Error lanzando juego:', err);
    });
}

function launchCurrentHero() {
  if (heroGame) launchGame(heroGame.id, heroGame.title);
}

function openCurrentHeroInfo() {
  if (heroGame) openModal(heroGame);
}

function openModal(game) {
  currentModalGame = game;
  document.getElementById('mTitle').innerText = game.title;
  document.getElementById('mPegi').innerText = game.pegiRating || 'PEGI 12';
  document.getElementById('mPublisher').innerText = game.publisher || 'Amazon Luna';
  document.getElementById('mGenres').innerText = game.genres || 'Cloud Gaming';
  document.getElementById('mSynopsis').innerText = game.synopsis || 'Disfruta de este título en alta fidelidad 1080p 60 FPS mediante streaming de baja latencia WebRTC.';
  
  const bg = game.backdropUrl || game.coverUrl || '';
  document.getElementById('mHero').style.backgroundImage = bg ? 'url(' + bg + ')' : 'none';
  
  document.getElementById('mPlayBtn').onclick = () => {
    closeModal();
    launchGame(game.id, game.title);
  };
  
  const modal = document.getElementById('gameModal');
  modal.style.display = 'flex';
  setTimeout(() => {
    setFocus(1); // Foco en botón Jugar del modal
  }, 50);
}

function closeModal(e) {
  if (e && e.target && e.target !== document.getElementById('gameModal') && !e.target.classList.contains('btn-close')) {
    return;
  }
  document.getElementById('gameModal').style.display = 'none';
  currentModalGame = null;
  updateFocusables();
}

function renderCatalog(games) {
  catalogGames = games;
  if (!games || games.length === 0) return;

  heroGame = games[0];
  document.getElementById('heroTitle').innerText = heroGame.title;
  document.getElementById('heroDesc').innerText = heroGame.synopsis || (heroGame.publisher + ' • ' + (heroGame.genres || 'Destacado'));
  if (heroGame.backdropUrl || heroGame.coverUrl) {
    document.getElementById('heroBanner').style.backgroundImage = 'url(' + (heroGame.backdropUrl || heroGame.coverUrl) + ')';
  }

  // Agrupar en colecciones (Rankings + Géneros / Colecciones)
  const container = document.getElementById('swimlanesContainer');
  container.innerHTML = '';

  // 1. Fila de Rankings (Las mejores novedades en juegos)
  const rankLane = document.createElement('div');
  rankLane.className = 'swimlane';
  rankLane.innerHTML = '<div class="swimlane-title">Las mejores novedades en juegos</div>';
  const rankRow = document.createElement('div');
  rankRow.className = 'swimlane-row';

  const top6 = games.slice(0, 10);
  top6.forEach((g, idx) => {
    const wrap = document.createElement('div');
    wrap.className = 'game-card-wrapper';
    wrap.innerHTML = '<div class="rank-number">' + (idx + 1) + '</div>';

    const card = document.createElement('div');
    card.className = 'game-card focusable';
    card.tabIndex = 0;
    card.onclick = () => openModal(g);

    card.innerHTML = `
      <img class="card-cover" loading="lazy" src="${g.coverUrl || g.backdropUrl || ''}" alt="${g.title}">
      <div class="card-info">
        <div class="card-title">${g.title}</div>
        <div class="card-sub">${g.publisher || 'Amazon Luna'}</div>
      </div>
    `;
    wrap.appendChild(card);
    rankRow.appendChild(wrap);
  });
  rankLane.appendChild(rankRow);
  container.appendChild(rankLane);

  // 2. Fila Seguir Jugando / Destacados
  const recLane = document.createElement('div');
  recLane.className = 'swimlane';
  recLane.innerHTML = '<div class="swimlane-title">Recomendados para ti</div>';
  const recRow = document.createElement('div');
  recRow.className = 'swimlane-row';

  games.slice(5).concat(games.slice(0, 5)).forEach(g => {
    const card = document.createElement('div');
    card.className = 'game-card focusable';
    card.tabIndex = 0;
    card.onclick = () => openModal(g);

    card.innerHTML = `
      <img class="card-cover" loading="lazy" src="${g.coverUrl || g.backdropUrl || ''}" alt="${g.title}">
      <div class="card-info">
        <div class="card-title">${g.title}</div>
        <div class="card-sub">${g.genres || g.publisher || 'Cloud Gaming'}</div>
      </div>
    `;
    recRow.appendChild(card);
  });
  recLane.appendChild(recRow);
  container.appendChild(recLane);

  updateFocusables();
}

function onSearchInput(query) {
  if (!query || query.trim() === '') {
    renderCatalog(catalogGames);
    return;
  }
  const q = query.toLowerCase().trim();
  const filtered = catalogGames.filter(g => 
    (g.title && g.title.toLowerCase().includes(q)) ||
    (g.publisher && g.publisher.toLowerCase().includes(q)) ||
    (g.genres && g.genres.toLowerCase().includes(q))
  );
  renderCatalog(filtered);
}

// Cargar catálogo desde la API local del puente PS5
fetch('/api/catalog')
  .then(r => r.json())
  .then(data => {
    if (data.profile) {
      document.getElementById('profileName').innerText = (data.profile.displayName || 'Bornicho') + ' ' + (data.profile.discriminator || '#8663');
    }
    renderCatalog(data.games || []);
  })
  .catch(err => {
    console.error('Error cargando catálogo:', err);
  });

// DUALSENSE & KEYBOARD NAVIGATION LOOP
let prevButtons = {};
let lastNavTime = 0;

function pollGamepad() {
  const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
  const gp = gamepads[0] || gamepads[1];
  const now = Date.now();

  if (gp && now - lastNavTime > 160) {
    // D-Pad / Stick Izquierdo
    const up = gp.buttons[12]?.pressed || gp.axes[1] < -0.5;
    const down = gp.buttons[13]?.pressed || gp.axes[1] > 0.5;
    const left = gp.buttons[14]?.pressed || gp.axes[0] < -0.5;
    const right = gp.buttons[15]?.pressed || gp.axes[0] > 0.5;

    // Botones
    const btnCross = gp.buttons[0]?.pressed; // (X)
    const btnCircle = gp.buttons[1]?.pressed; // (O)
    const btnSquare = gp.buttons[2]?.pressed; // ([ ])
    const btnL1 = gp.buttons[4]?.pressed;
    const btnR1 = gp.buttons[5]?.pressed;

    if (right) {
      setFocus(focusIndex + 1);
      lastNavTime = now;
    } else if (left) {
      setFocus(focusIndex - 1);
      lastNavTime = now;
    } else if (down) {
      setFocus(focusIndex + 4);
      lastNavTime = now;
    } else if (up) {
      setFocus(focusIndex - 4);
      lastNavTime = now;
    }

    if (btnCross && !prevButtons[0]) {
      if (focusables[focusIndex]) focusables[focusIndex].click();
      lastNavTime = now + 100;
    }
    if (btnCircle && !prevButtons[1]) {
      closeModal();
      lastNavTime = now + 100;
    }
    if (btnSquare && !prevButtons[2]) {
      document.getElementById('searchInput').focus();
      lastNavTime = now + 100;
    }

    prevButtons[0] = btnCross;
    prevButtons[1] = btnCircle;
    prevButtons[2] = btnSquare;
  }

  requestAnimationFrame(pollGamepad);
}
requestAnimationFrame(pollGamepad);

// Navegación con teclado del navegador PS5
window.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowRight') { setFocus(focusIndex + 1); e.preventDefault(); }
  else if (e.key === 'ArrowLeft') { setFocus(focusIndex - 1); e.preventDefault(); }
  else if (e.key === 'ArrowDown') { setFocus(focusIndex + 4); e.preventDefault(); }
  else if (e.key === 'ArrowUp') { setFocus(focusIndex - 4); e.preventDefault(); }
  else if (e.key === 'Enter') { if (focusables[focusIndex]) focusables[focusIndex].click(); }
  else if (e.key === 'Escape' || e.key === 'Backspace') { closeModal(); }
});
</script>

</body>
</html>
)raw";
    return html;
}
'''

content = content[:start_idx] + new_func + "\n" + content[end_idx:]

with open(path, "w", encoding="utf-8") as f:
    f.write(content)

print("generateLunaWebPage replaced successfully!")
