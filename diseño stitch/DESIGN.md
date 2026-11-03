---
name: Luna Console OS
colors:
  surface: '#0e1321'
  surface-dim: '#0e1321'
  surface-bright: '#343948'
  surface-container-lowest: '#090e1c'
  surface-container-low: '#161b2a'
  surface-container: '#1a1f2e'
  surface-container-high: '#252a39'
  surface-container-highest: '#303444'
  on-surface: '#dee2f6'
  on-surface-variant: '#c1c6d5'
  inverse-surface: '#dee2f6'
  inverse-on-surface: '#2b303f'
  outline: '#8b919e'
  outline-variant: '#414752'
  surface-tint: '#a7c8ff'
  primary: '#a7c8ff'
  on-primary: '#003060'
  primary-container: '#0070d1'
  on-primary-container: '#f2f5ff'
  inverse-primary: '#005eb2'
  secondary: '#dbb8ff'
  on-secondary: '#470083'
  secondary-container: '#6807ba'
  on-secondary-container: '#d0a6ff'
  tertiary: '#00dfc1'
  on-tertiary: '#00382f'
  tertiary-container: '#007f6d'
  on-tertiary-container: '#d3fff2'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#d5e3ff'
  primary-fixed-dim: '#a7c8ff'
  on-primary-fixed: '#001c3b'
  on-primary-fixed-variant: '#004788'
  secondary-fixed: '#efdbff'
  secondary-fixed-dim: '#dbb8ff'
  on-secondary-fixed: '#2b0052'
  on-secondary-fixed-variant: '#6600b7'
  tertiary-fixed: '#26fedc'
  tertiary-fixed-dim: '#00dfc1'
  on-tertiary-fixed: '#00201a'
  on-tertiary-fixed-variant: '#005144'
  background: '#0e1321'
  on-background: '#dee2f6'
  surface-variant: '#303444'
typography:
  display-hero:
    fontFamily: Outfit
    fontSize: 56px
    fontWeight: '700'
    lineHeight: 64px
    letterSpacing: -0.02em
  headline-xl:
    fontFamily: Outfit
    fontSize: 40px
    fontWeight: '700'
    lineHeight: 48px
    letterSpacing: -0.015em
  headline-lg:
    fontFamily: Outfit
    fontSize: 32px
    fontWeight: '600'
    lineHeight: 40px
    letterSpacing: -0.01em
  headline-md:
    fontFamily: Outfit
    fontSize: 26px
    fontWeight: '600'
    lineHeight: 34px
    letterSpacing: 0em
  title-card:
    fontFamily: Outfit
    fontSize: 22px
    fontWeight: '600'
    lineHeight: 28px
    letterSpacing: 0.005em
  body-lg:
    fontFamily: Outfit
    fontSize: 20px
    fontWeight: '400'
    lineHeight: 28px
    letterSpacing: 0em
  body-md:
    fontFamily: Outfit
    fontSize: 17px
    fontWeight: '400'
    lineHeight: 24px
    letterSpacing: 0.01em
  label-controller:
    fontFamily: Outfit
    fontSize: 15px
    fontWeight: '600'
    lineHeight: 20px
    letterSpacing: 0.04em
  caption-meta:
    fontFamily: Outfit
    fontSize: 13px
    fontWeight: '500'
    lineHeight: 18px
    letterSpacing: 0.05em
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  gutter: 1.5rem
  gutter-tv: 2rem
  margin: 3.5rem
  margin-overscan: 4.5rem
  space-xs: 0.5rem
  space-sm: 0.75rem
  space-md: 1.25rem
  space-lg: 2rem
  space-xl: 3rem
---

## Brand & Style

This design system defines a next-generation TV console dashboard interface built explicitly for 10-foot viewing experiences at native 16:9 ratios (1080p base, scaling cleanly to 4K). It fuses the cinematic, atmospheric immersion of modern flagship console dashboards with the instant-access agility of cloud gaming hubs.

### Aesthetic Persona
- **Atmospheric & Cinematic:** The UI serves as an understated framing device for edge-to-edge dynamic game artwork, video backdrops, and interactive media planes. UI elements emerge out of deep obsidian spaces rather than sitting flatly on top of them.
- **Precision Engineered:** High-fidelity micro-interactions, calibrated controller navigation loops, crisp tactile feedback borders, and luminous spatial elevation recreate the immediate responsiveness of native hardware.
- **Emotional Response:** Inspires anticipation, high-performance capability, and frictionless immersion. The interface eliminates cognitive load from television distances through unmistakable focus anchors and razor-sharp typographic hierarchy.

### Design Movement
- **Atmospheric Glassmorphism & Bioluminescent Precision:** Translucent smoked-slate surfaces (`rgba(10, 15, 29, 0.72)` to `rgba(5, 8, 17, 0.88)`), structural ultra-thin borders (`1px` solid translucent white/cyan), and electric chromatic light pools. Focus shifts produce physical scale expansions and soft ambient blooms rather than hard flat color fills.

## Colors

The color palette is engineered exclusively for high dynamic range displays and darkened living room environments, prioritizing low eye strain during long sessions while guaranteeing instantaneous state readability through vibrant spectral highlights.

### Palette Architecture
- **Midnight Canvas & Voids:**
  - `Canvas Deep Void`: `#050811` (Master background, full bleed underlays)
  - `Surface Midnight Base`: `#0A0F1D` (Passive structural panels and card planes)
  - `Surface Smoked Glass`: `rgba(14, 22, 41, 0.65)` (Elevated drawers, modal sheets, and quick-bar docks)
- **Primary Electric Signature (Console Focus & Action):**
  - `Electric Azure`: `#0070D1` (Primary interaction anchor, focused ring borders, active tab pips)
  - `Luminous Sky`: `#38BDF8` (Active glow halos, highlighted controller cues, high-contrast badges)
- **Secondary & Tertiary Bioluminescence (Cloud Gaming Accents):**
  - `Luna Violet`: `#7928CA` (Secondary game pass tagging, cloud tier indicators, ambient background gradient bleeds)
  - `Cyber Cyan`: `#00F5D4` (Instant-play states, zero-latency indicators, audio ping badges, positive metric pips)
- **Neutrals & Text Tiers:**
  - `Text Overcast High`: `#F8FAFC` (Titles, focused card labels, primary readouts)
  - `Text Mist Medium`: `#94A3B8` (Unfocused card meta, developer credits, subheadings)
  - `Text Deep Shadow Low`: `#475569` (Inactive icons, secondary controller glyph bodies)

### Application Guidelines
- Backgrounds never use pure black (`#000000`); instead, dynamic ambient gradients bloom from `#050811` into tinted midnight nodes (`#0D1527`) based on active media art.
- Cyan (`#00F5D4`) is reserved strictly for performance telemetry (ping, cloud sync) and high-priority action pills.
- Active focus states always invoke a dual-layer treatment: a crisp high-contrast border in `#38BDF8` accompanied by an exterior soft bloom in `#0070D1`.

## Typography

Typography is calibrated strictly for the "10-foot" TV viewing paradigm. Small fonts (sub-13px) are systematically excluded to prevent illegibility across varying panel sizes, panel types, and room illuminations.

### Type Scale & Hierarchy Rules
- **Display Hero (56px):** Game title overviews, primary immersive splash screens, and zero-state promotional headliners.
- **Headline XL / LG (40px / 32px):** Top-level hub navigation anchors (e.g., "Games", "Media", "Luna Hub", "Settings") and active tray titles.
- **Headline MD / Title Card (26px / 22px):** Grid card headings, dynamic drawer titles, and confirmation dialog headers. Must maintain structural clarity even when cards are scaled down in unfocused rows.
- **Body LG / MD (20px / 17px):** Narrative game descriptions, system notifications, patch notes, and multiplayer squad rosters.
- **Label Controller (15px):** Action prompts paired adjacent to DualSense glyphs (e.g., "[X] Play Now", "[Triangle] Details"). Displayed in uppercase or clear title case with wide tracking (`0.04em`) to ensure instant peripheral scanning.
- **Caption Meta (13px):** Cloud network status (e.g., "60 FPS • 1080P", "4K HDR"), installation size, and trophy timestamp counters.

## Layout & Spacing

The layout is built upon a standard 1080p canvas grid (1920x1080) designed to autoscale by exactly 2x for native 4K UHD displays (3840x2160) without fractional subpixel rendering.

### Safe Areas & Television Overscan
- **Global TV Safe Margins (`margin-overscan: 4.5rem` / `72px`):** All actionable UI, focus rings, persistent top bars, and controller legends reside inside a 90% title-safe inner bounding box to avoid clipping on legacy TV scalers.
- **Top Navigation Zone:** Fixed horizontal utility bar at `Y: 72px` containing profile status, cloud latency ping, network indicators, search, and system settings.
- **Hero Staging Zone:** Upper 55% of the viewport reserved for full-bleed hero artwork, live title metadata, dynamic background video, and contextual shortcut buttons.
- **Horizontal Content Carousels:** Bottom 45% of the viewport dedicated to infinite horizontal card rails that track horizontally using D-pad or left analog stick input.

### Grid Rhythm & Snapping
- **Carousels:** Utilize `gutter-tv` (`32px` / `2rem`) between cards. Active focus centers or anchors along the left third of the screen (`margin: 3.5rem`), pushing preceding cards off-screen with linear opacity fades (`1.0` down to `0.15`).
- **Vertical Spacing:** Rows space apart by `space-xl` (`48px` / `3rem`). When shifting vertical focus between rows, the active row smoothly translates to vertical center (`Y: ~520px`).

## Elevation & Depth

Visual depth is communicated through a 3D z-axis translation combined with bioluminescent light casting, avoiding traditional generic drop shadows.

### Depth Architecture
1. **Background Layer (Z: 0):** Smoked atmospheric void (`#050811`) layered with an adaptive radial blur (120px blur radius, 30% opacity) that samples the prominent key art colors of the currently focused game title.
2. **Resting Card Layer (Z: 10):** Surface Midnight base (`rgba(10, 15, 29, 0.75)`) encased in a hairline ghost border (`1px solid rgba(255, 255, 255, 0.08)`). Content sits calmly in context.
3. **Elevated Trays & Overlays (Z: 20):** Deep frosted glass sheets (`rgba(10, 15, 29, 0.88)` with `backdrop-filter: blur(24px)`) used for bottom drawer options, DualSense quick menus, and trophy pop-ups.
4. **Focused Active State (Z: 50):** 
   - Focused cards scale to **1.08x** via smooth spring curve (`cubic-bezier(0.2, 0.8, 0.2, 1)`).
   - Outer glow halo: `0 0 0 2px #38BDF8, 0 8px 32px rgba(0, 112, 209, 0.55), 0 2px 12px rgba(0, 245, 212, 0.25)`.
   - Resting sibling elements dim down to `opacity: 0.45` to eliminate peripheral visual noise.

## Shapes

The interface balances the geometric precision of high-end home theater hardware with modern ergonomics.

### Curvature Tokens
- **Standard Cards & Game Posters:** Default `rounded-lg` (`16px` / `1rem`). Preserves the natural aspect ratio of game box art without cutting off peripheral typography or badges.
- **Controller Glyph Badges & Pills:** Fully pill-shaped (`9999px`) for contextual action buttons, DualSense shortcut indicators, and stream health tags.
- **Top Utility Rail & Profile Pods:** `rounded-lg` (`16px`) for clean grouping boundaries.
- **Active Focus Rings:** Exact conformal radii tracking the target element, maintaining continuous 3px offset separation to prevent optical clipping of card artwork.

## Components

### 1. DualSense Controller Glyph Badges
- **Visuals:** Circular or squircle badges (`32px x 32px`) rendered in frosted midnight obsidian (`rgba(255, 255, 255, 0.12)`) featuring clean vector console glyphs: Cross (`✕`), Circle (`○`), Square (`□`), Triangle (`△`), Bumpers (`L1`/`R1`), Sticks (`L3`/`R3`), and `Options`.
- **States:** Paired alongside `label-controller` text. When pressed, the badge emits an internal pulse of `Electric Azure` (`#0070D1`) with a momentary scale down to `0.92x`.

### 2. Dashboard Media Cards (Carousels)
- **Aspect Ratios:** Vertical Posters (`2:3` for Luna cloud library games), Horizontal Wide (`16:9` for active media, continue-playing slots, and cloud clips).
- **Structure:** Edge-to-edge artwork container with a bottom internal gradient scrim (`rgba(5, 8, 17, 0.95)` up to transparent).
- **Resting:** Dimmed border (`1px solid rgba(255, 255, 255, 0.08)`), title hidden or subtly displayed in `Text Mist Medium`.
- **Focused:** Scaled `1.08x`, illuminated by dual cyan-azure neon perimeter ring, title transitions to `Text Overcast High`, metadata badges slide into view smoothly from bottom edge.

### 3. Quick Action Buttons
- **Primary Play Button:** Solid pill container, gradient background from `#0070D1` to `#7928CA`. Focused state initiates a continuous shimmering specular edge in `#00F5D4`.
- **Secondary Actions (Options / Cloud Sync):** Frosted glass pill with `1px solid rgba(255, 255, 255, 0.18)` and `Text Overcast High` typography. Focus adds a solid `#38BDF8` border.

### 4. Cloud Telemetry & Stream Status Chips
- **Layout:** Micro-pill chip (`height: 28px`) positioned in the top-right metadata tray or card badge corner.
- **Details:** Features an animated Luna violet and cyan sync dot alongside real-time metrics (`1080P • 60 FPS • LUNA SPEED: 12ms`). Indicates optimal network connection without obtrusive warnings.

### 5. Horizontal Top Utility Bar
- **Elements:** Profile avatar, PlayStation Plus / Luna+ subscription tier badges, Party Voice chat pod, Universal Search icon, and System Clock.
- **Behavior:** Minimalist resting state; snaps down into active focus when D-pad Up is commanded from the main carousel.

### 6. Modal Sheets & System Overlays
- **Structure:** Slides out from the screen right edge (`width: 480px`, full height `1080px`) using deep smoked glass blur (`backdrop-filter: blur(32px)`, surface `#0A0F1D` at `85%` opacity).
- **Usage:** Game settings, DualSense trigger resistance customization, cloud channel switching, and instant party invites.