/* Pebble weather icon set — 12 solid flat glyphs, authored on a 44×44 grid.
   Every shape uses flat fills (currentColor by default) so a glyph inherits
   whatever color role it is placed in. No gradients, strokes-as-hairlines,
   or AA-dependent detail — chunky pixel-friendly forms only. */
(function () {
  // ---- shape helpers -------------------------------------------------------
  // Chunky cloud built from overlapping circles + a base bar. Same fill merges.
  function cloud(cx, cy, s, fill) {
    s = s || 1;
    const f = fill || 'currentColor';
    function c(x, y, r) {
      return `<circle cx="${cx + x * s}" cy="${cy + y * s}" r="${r * s}" fill="${f}"/>`;
    }
    const bx = cx - 14 * s, by = cy + 0 * s, bw = 28 * s, bh = 9 * s;
    return (
      c(-8, 1, 8) + c(7, 1, 9) + c(-1, -5, 8) +
      `<rect x="${bx}" y="${by}" width="${bw}" height="${bh}" rx="${4 * s}" fill="${f}"/>`
    );
  }
  // Sun = core disc + N rays radiating from center, drawn pixel-chunky.
  function sun(cx, cy, r, rayLen, fill, rays) {
    const f = fill || 'currentColor';
    rays = rays || 8;
    let out = '';
    for (let i = 0; i < rays; i++) {
      const a = (i * 360) / rays;
      out += `<rect x="${cx - 1.6}" y="${cy - r - rayLen - 1.5}" width="3.2" height="${rayLen}" rx="1.4" fill="${f}" transform="rotate(${a} ${cx} ${cy})"/>`;
    }
    return out + `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${f}"/>`;
  }
  // Crescent moon = disc with a bg-colored bite. Icons always sit on the bg
  // role, so the cutout stays crisp and flat.
  function moon(cx, cy, r, fill) {
    const f = fill || 'currentColor';
    return `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${f}"/>` +
      `<circle cx="${cx + r * 0.55}" cy="${cy - r * 0.38}" r="${r * 0.92}" fill="var(--bg)"/>`;
  }
  // small vertical rounded rect "drop"
  function drop(x, y, h, fill) {
    return `<rect x="${x - 1.6}" y="${y}" width="3.2" height="${h}" rx="1.6" fill="${fill || 'currentColor'}"/>`;
  }
  // diamond flake
  function flake(x, y, s, fill) {
    return `<rect x="${x - s / 2}" y="${y - s / 2}" width="${s}" height="${s}" fill="${fill || 'currentColor'}" transform="rotate(45 ${x} ${y})"/>`;
  }

  // ---- glyph definitions ---------------------------------------------------
  // Each returns inner SVG markup for a 44×44 viewBox. `a` = accent override
  // color for the rain/precip bars (icons themselves stay monochrome for theme
  // safety, but precip-bearing glyphs tint their drops with the accent).
  const G = {
    'clear-day': () => sun(22, 22, 9, 8),
    'clear-night': () => moon(24, 22, 14),
    'partly-cloudy-day': () =>
      sun(15, 14, 6, 6) + cloud(25, 24, 0.92),
    'partly-cloudy-night': () =>
      moon(15, 14, 9) + cloud(25, 24, 0.92),
    'cloudy': () =>
      `<g opacity="0.55">${cloud(15, 15, 0.72)}</g>` + cloud(24, 25, 1),
    'fog': () =>
      cloud(22, 16, 0.9) +
      ['M11 32h22', 'M9 37h26', 'M13 42h18']
        .map((d, i) => `<path d="${d}" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" fill="none" opacity="${i === 1 ? 1 : 0.75}"/>`) 
        .join(''),
    'drizzle': (a) =>
      cloud(22, 16, 0.96) + drop(15, 33, 6, a) + drop(22, 35, 6, a) + drop(29, 33, 6, a),
    'rain': (a) =>
      cloud(22, 15, 0.98) + drop(14, 33, 9, a) + drop(22, 35, 9, a) + drop(30, 33, 9, a),
    'thunderstorm': (a) =>
      cloud(22, 14, 0.98) +
      `<polygon points="24,30 16,30 26,41 22,41 27,33 22,33 26,28" fill="${a || 'currentColor'}"/>` +
      drop(14, 33, 7, 'currentColor'),
    'snow': () =>
      cloud(22, 15, 0.98) + flake(15, 35, 5) + flake(23, 38, 5) + flake(31, 35, 5),
    'sleet': (a) =>
      cloud(22, 15, 0.98) + drop(15, 33, 8, a) + flake(24, 36, 5) + drop(31, 33, 8, a),
    'wind': () =>
      ['M8 17h20a4 4 0 1 0-4-4', 'M8 24h26a4 4 0 1 1-4 4', 'M8 31h16a4 4 0 1 0-4 4']
        .map((d) => `<path d="${d}" stroke="currentColor" stroke-width="3.4" stroke-linecap="round" fill="none"/>`) 
        .join(''),
  };

  const ORDER = [
    'clear-day', 'clear-night', 'partly-cloudy-day', 'partly-cloudy-night',
    'cloudy', 'fog', 'drizzle', 'rain', 'thunderstorm', 'snow', 'sleet', 'wind',
  ];

  // Public: build an <svg> string for a glyph at a given pixel size.
  function icon(name, size, accent) {
    const inner = (G[name] || G['cloudy'])(accent);
    size = size || 44;
    return `<svg class="wicon" width="${size}" height="${size}" viewBox="0 0 44 44" fill="none" xmlns="http://www.w3.org/2000/svg" aria-label="${name}">${inner}</svg>`;
  }

  window.PebbleIcons = { icon, ORDER, names: ORDER };
})();
