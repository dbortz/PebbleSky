/* Assembles the studio: live controls + all watch screens + icon gallery.
   Tweak state persists to localStorage and drives CSS color roles + re-render. */
(function () {
  const F = window.PebbleFace;
  const PI = window.PebbleIcons;

  // ---------- persistent tweak state ----------
  const DEF = { format12: false, unitsF: true, dateMD: true, stale: false, font: 'grotesque', accent: '#00AAFF', warning: '#FFFF00', zoom: 1.7 };
  let S;
  try { S = Object.assign({}, DEF, JSON.parse(localStorage.getItem('pebbleface') || '{}')); }
  catch (e) { S = Object.assign({}, DEF); }
  function save() { localStorage.setItem('pebbleface', JSON.stringify(S)); }

  // palette-safe swatch choices (Pebble 64-color: channels 00/55/AA/FF)
  const ACCENTS = ['#00AAFF', '#00FFAA', '#55FF00', '#FFAA00', '#FF55AA', '#AA55FF'];
  const WARNINGS = ['#FFFF00', '#FFAA00', '#FF5500', '#FF0055', '#FF55AA'];

  // ---------- demo data ----------
  const FACE_DRY = { h: 10, m: 42, wd: 3, mo: 6, day: 4, battery: 82, tempF: 72, highF: 78, icon: 'partly-cloudy-day', steps: 8432, goal: 10000, precip: [0, 0, 1, 0, 0, 1, 0, 0], rain: false };
  const FACE_RAIN = { h: 9, m: 5, wd: 3, mo: 6, day: 4, battery: 64, tempF: 64, highF: 70, icon: 'rain', steps: 5120, goal: 10000, precip: [0, 1, 2, 3, 4, 4, 3, 2], rain: true, warnText: 'RAIN IN 20M' };
  const PRECIP_DETAIL = { headline: 'RAIN IN ~20 MIN', precip: [0, 1, 2, 3, 4, 4, 3, 2], probs: [10, 35, 70, 90, 80] };
  const NOW = { icon: 'partly-cloudy-day', tempF: 72, feelsF: 74, cond: 'Partly Cloudy', wind: '8 mph', humid: 54, highF: 78, updated: '10:38' };
  const DAYS = [
    { wd: 'WED', icon: 'partly-cloudy-day', hiT: 78, loT: 61 },
    { wd: 'THU', icon: 'rain', hiT: 70, loT: 58 },
    { wd: 'FRI', icon: 'thunderstorm', hiT: 74, loT: 60 },
    { wd: 'SAT', icon: 'clear-day', hiT: 82, loT: 63 },
    { wd: 'SUN', icon: 'cloudy', hiT: 79, loT: 64 },
  ];
  (function rangeBars() {
    const lo = Math.min(...DAYS.map(d => d.loT)), hi = Math.max(...DAYS.map(d => d.hiT));
    DAYS.forEach(d => { d.lo = Math.round((d.loT - lo) / (hi - lo) * 100); d.hi = Math.round((d.hiT - lo) / (hi - lo) * 100); });
  })();

  // ---------- apply CSS roles ----------
  function applyVars() {
    const r = document.documentElement.style;
    r.setProperty('--accent', S.accent);
    r.setProperty('--warning', S.warning);
    r.setProperty('--zoom', S.zoom);
  }
  const ffClass = () => 'ff-' + S.font;

  // ---------- render the screens ----------
  function paint() {
    applyVars();
    const t = { format12: S.format12, unitsF: S.unitsF, dateMD: S.dateMD };
    const ff = ffClass();
    const dry = Object.assign({}, FACE_DRY, { stale: S.stale });
    const rain = Object.assign({}, FACE_RAIN, { stale: S.stale });

    mount('mount-dry', F.renderFace(dry, t), ff);
    mount('mount-rain', F.renderFace(rain, t), ff);
    mount('mount-precip', F.renderPrecipDetail(PRECIP_DETAIL, t), ff);
    mount('mount-5day', F.renderForecast({ days: DAYS }, t), ff);
    mount('mount-now', F.renderNow(NOW, t), ff);
    buildRoles();
  }
  function mount(id, html, ff) {
    const el = document.getElementById(id);
    if (el) { el.className = 'screen-mount'; el.innerHTML = `<div class="${ff} screen-root">${html}</div>`; }
  }

  // ---------- controls ----------
  function seg(label, opts, getVal, setVal) {
    const wrap = document.createElement('div'); wrap.className = 'ctl';
    wrap.innerHTML = `<span class="ctl-label">${label}</span>`;
    const seg = document.createElement('div'); seg.className = 'seg';
    opts.forEach(([val, txt]) => {
      const b = document.createElement('button'); b.textContent = txt;
      if (getVal() === val) b.classList.add('on');
      b.onclick = () => { setVal(val); save(); rebuildControls(); paint(); };
      seg.appendChild(b);
    });
    wrap.appendChild(seg); return wrap;
  }
  function swatches(label, list, getVal, setVal) {
    const wrap = document.createElement('div'); wrap.className = 'ctl';
    wrap.innerHTML = `<span class="ctl-label">${label}</span>`;
    const row = document.createElement('div'); row.className = 'swrow';
    list.forEach(hex => {
      const b = document.createElement('button'); b.className = 'sw' + (getVal() === hex ? ' on' : '');
      b.style.background = hex; b.title = hex;
      b.onclick = () => { setVal(hex); save(); rebuildControls(); paint(); };
      row.appendChild(b);
    });
    wrap.appendChild(row); return wrap;
  }
  function rebuildControls() {
    const bar = document.getElementById('controls');
    bar.innerHTML = '';
    bar.appendChild(seg('Clock', [[false, '24H'], [true, '12H']], () => S.format12, v => S.format12 = v));
    bar.appendChild(seg('Units', [[true, '°F'], [false, '°C']], () => S.unitsF, v => S.unitsF = v));
    bar.appendChild(seg('Date', [[true, 'M/D'], [false, 'D/M']], () => S.dateMD, v => S.dateMD = v));
    bar.appendChild(swatches('Accent', ACCENTS, () => S.accent, v => S.accent = v));
    bar.appendChild(swatches('Warning', WARNINGS, () => S.warning, v => S.warning = v));
    bar.appendChild(seg('Data', [[false, 'Fresh'], [true, 'Stale']], () => S.stale, v => S.stale = v));
    bar.appendChild(seg('Zoom', [[1, '1×'], [1.7, '1.7×'], [2.4, '2.4×']], () => S.zoom, v => S.zoom = v));
  }

  // ---------- icon gallery ----------
  function buildIcons() {
    const grid = document.getElementById('icongrid');
    grid.innerHTML = PI.ORDER.map(nm =>
      `<div class="icocell"><div class="pair">${PI.icon(nm, 44, 'var(--accent)')}${PI.icon(nm, 24, 'var(--accent)')}</div><div class="nm">${nm}</div></div>`
    ).join('');
  }

  // ---------- color roles ----------
  function buildRoles() {
    const roles = [
      ['background', '#000000', 'var(--bg)', 'Face fill. Pure black = max contrast + lowest always-on power.', true],
      ['text-primary', '#FFFFFF', 'var(--text-primary)', 'Time, current temp, step count — the glanceable layer.'],
      ['text-secondary', '#AAAAAA', 'var(--text-secondary)', 'Date, battery, high temp, labels, axis. Recedes from primary.'],
      ['accent', S.accent, 'var(--accent)', 'Goal bar, forecast range, “now” marker, icon tint. Configurable.'],
      ['warning', S.warning, 'var(--warning)', 'Rain-incoming alert line + companion headline. Configurable.'],
    ];
    document.getElementById('roles').innerHTML = roles.map(r => `
      <div class="rolecard">
        <div class="chip" style="background:${r[1]};${r[4] ? 'border-bottom:1px solid #2a2c33' : ''}"></div>
        <div class="meta"><b>${r[0]}</b><div class="hex">${r[1].toUpperCase()}</div><div class="use">${r[3]}</div></div>
      </div>`).join('');

    document.getElementById('ramp').innerHTML = `
      <div style="font-size:12.5px;font-weight:800;letter-spacing:.3px">precip intensity ramp <span style="color:#7c7e87;font-weight:600">· fixed sub-palette, decoupled from accent</span></div>
      <div class="ramp">
        <i style="background:var(--p0)"></i><i style="background:var(--p1)"></i><i style="background:var(--p2)"></i><i style="background:var(--p3)"></i><i style="background:var(--p4)"></i>
      </div>
      <div class="lbl"><span>NONE #555</span><span>TRACE #05A</span><span>LIGHT #0AF</span><span>MOD #5AF</span><span>HEAVY #AFF</span></div>`;
  }

  // ---------- spec table ----------
  function buildSpec() {
    const rows = [
      ['Status bar', 'x0–200 · y0–26 · h26', 'Archivo 700 · 15px', 'text-secondary', 'Date left, battery right. 9px side padding.'],
      ['Time', 'centered · y26–108 · h82', 'Archivo 800 · 66px (-2 track)', 'text-primary', '12h = 24h layout, hours wrap 1–12; tabular nums.'],
      ['Weather row', 'x9 · y108–154 · h46', 'Archivo 800 · 36 / 18px', 'text-primary / -secondary', '44×44 icon · current temp · H-high right.'],
      ['Steps', 'bottom-left · y154–219', 'Archivo 800 · 25px + 10px cap', 'text-primary / -secondary', 'Footprint + STEPS caption, count below. No goal meter.'],
      ['Warning line', 'bottom-right · top', 'Archivo 800 · 15px', 'warning', 'Rain state only. e.g. RAIN IN 20M.'],
      ['Precip strip', 'bottom-right · 8 bars flex', 'Archivo 700 · — ', 'precip ramp', '8×15-min buckets, 5 levels, 2px axis.'],
      ['Battery', 'top-right · 16×10 + %', 'Archivo 700 · 14px', 'text-secondary', 'Shell + fill proportional to %.'],
      ['Stale dot', 'on icon · 7px', '—', 'text-secondary', 'Weather block dims to 42% + dim dot.'],
    ];
    document.getElementById('spectable').innerHTML = `
      <thead><tr><th>Element</th><th>Position / size</th><th>Font</th><th>Color role</th><th>Notes</th></tr></thead>
      <tbody>${rows.map(r => `<tr><td>${r[0]}</td><td><code>${r[1]}</code></td><td>${r[2]}</td><td>${r[3]}</td><td>${r[4]}</td></tr>`).join('')}</tbody>`;
  }

  // ---------- boot ----------
  rebuildControls();
  buildIcons();
  buildRoles();
  buildSpec();
  paint();
  window.PebbleApp = { paint, S };
})();
