/* Watchface + companion screen renderers. Pure functions that emit HTML for a
   200×228 Pebble screen. All color comes from CSS custom properties (the named
   roles), so a single theme/role change restyles everything. */
(function () {
  const I = window.PebbleIcons;

  // ---------- formatting helpers -------------------------------------------
  function fmtTime(h24, m, t) {
    const mm = String(m).padStart(2, '0');
    let h = h24;
    if (t.format12) { h = h24 % 12; if (h === 0) h = 12; } // wrap 1–12, keep leading zero
    return { main: `${String(h).padStart(2, '0')}:${mm}`, ap: '' };
  }
  function fmtTemp(f, t) {
    const v = t.unitsF ? f : Math.round((f - 32) * 5 / 9);
    return `${v}°`;
  }
  const WD = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'];
  function fmtDate(wd, mo, day, t) {
    const d = t.dateMD ? `${mo}/${day}` : `${day}/${mo}`;
    return `${WD[wd]} ${d}`;
  }

  // ---------- shared atoms --------------------------------------------------
  function battery(pct) {
    const w = Math.max(2, Math.round(13 * pct / 100));
    return `<span class="batt"><span class="batt-shell"><span class="batt-fill" style="width:${w}px"></span></span><span class="batt-nub"></span><span class="batt-pct">${pct}%</span></span>`;
  }

  // 8 precip bars. levels: array of 0..4. `big` scales the strip for companion.
  function precipBars(levels, opts) {
    opts = opts || {};
    const heights = [0.14, 0.34, 0.55, 0.78, 1]; // by level
    const maxH = opts.maxH || 22;
    const bars = levels.map((lv, i) => {
      const h = Math.max(2, Math.round(heights[lv] * maxH));
      const cls = lv === 0 ? 'p0' : 'p' + lv;
      const now = opts.markNow && i === 0 ? ' bar-now' : '';
      return `<span class="pbar${now}"><span class="pbar-fill ${cls}" style="height:${h}px"></span></span>`;
    }).join('');
    return `<span class="pstrip" style="--barmax:${maxH}px">${bars}</span>`;
  }

  // ---------- MAIN FACE -----------------------------------------------------
  // opts: {h,m, wd,mo,day, battery, tempF, highF, icon, steps, goal,
  //        precip:[8], rain:bool, warnText, stale:bool}
  function renderFace(o, t) {
    const tm = fmtTime(o.h, o.m, t);
    const stale = o.stale ? ' is-stale' : '';
    const statusLine = o.rain
      ? `<div class="warn-line">${o.warnText}</div>`
      : `<div class="calm-line">No rain · 2h</div>`;
    const bars = precipBars(o.precip, { markNow: o.rain, maxH: 26 });

    return `<div class="face">
      <div class="statusbar">
        <span class="date">${fmtDate(o.wd, o.mo, o.day, t)}</span>
        ${battery(o.battery)}
      </div>
      <div class="timezone">
        <span class="time">${tm.main}</span>
      </div>
      <div class="weatherrow${stale}">
        <span class="wicon-box">${I.icon(o.icon, 44, 'var(--accent)')}${o.stale ? '<span class="stale-dot"></span>' : ''}</span>
        <span class="temp-now">${fmtTemp(o.tempF, t)}</span>
        <span class="temp-high">H ${fmtTemp(o.highF, t)}</span>
      </div>
      <div class="bottomzone">
        <div class="stepblock">
          <span class="step-cap">${footprint()}STEPS</span>
          <span class="step-count">${o.steps.toLocaleString()}</span>
        </div>
        <div class="precipblock">
          ${statusLine}
          ${bars}
        </div>
      </div>
    </div>`;
  }

  function footprint() {
    return `<svg width="18" height="18" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <ellipse cx="9" cy="11" rx="5" ry="7" fill="currentColor"/>
      <circle cx="16.5" cy="8" r="2.4" fill="currentColor"/>
      <circle cx="19.5" cy="12" r="2" fill="currentColor"/>
      <ellipse cx="9" cy="20.5" rx="3.4" ry="2.4" fill="currentColor"/>
    </svg>`;
  }

  // ---------- COMPANION 1 — 2-HOUR PRECIP DETAIL ---------------------------
  function renderPrecipDetail(o, t) {
    const labels = ['NOW', '+30', '+60', '+90', '+120'];
    const probs = o.probs;
    const big = o.precip.map((lv, i) => {
      const heights = [0.1, 0.34, 0.55, 0.78, 1];
      const h = Math.max(3, Math.round(heights[lv] * 80));
      return `<span class="dbar"><span class="dbar-fill p${lv}" style="height:${h}px"></span></span>`;
    }).join('');
    const legend = ['NONE', 'TRACE', 'LIGHT', 'MOD', 'HEAVY']
      .map((nm, i) => `<span class="leg"><span class="leg-sw p${i}"></span>${nm}</span>`).join('');
    return `<div class="cscreen">
      <div class="cs-head">2-HOUR PRECIP</div>
      <div class="cs-headline">${o.headline}</div>
      <div class="ddetail">
        <div class="dbars">${big}</div>
        <div class="dprob">${probs.map(p => `<span>${p}%</span>`).join('')}</div>
        <div class="dlabels">${labels.map(l => `<span>${l}</span>`).join('')}</div>
      </div>
      <div class="legendrow">${legend}</div>
    </div>`;
  }

  // ---------- COMPANION 2 — 5-DAY FORECAST ---------------------------------
  function renderForecast(o, t) {
    const rows = o.days.map(d => `
      <div class="fcast-row">
        <span class="fc-day">${d.wd}</span>
        <span class="fc-ico">${I.icon(d.icon, 26, 'var(--accent)')}</span>
        <span class="fc-bar"><span class="fc-track"></span><span class="fc-range" style="left:${d.lo}%;right:${100 - d.hi}%"></span></span>
        <span class="fc-hi">${fmtTemp(d.hiT, t)}</span>
        <span class="fc-lo">${fmtTemp(d.loT, t)}</span>
      </div>`).join('');
    return `<div class="cscreen">
      <div class="cs-head">5-DAY FORECAST</div>
      <div class="fcast">${rows}</div>
    </div>`;
  }

  // ---------- COMPANION 3 — NOW DETAILS ------------------------------------
  function renderNow(o, t) {
    return `<div class="cscreen now">
      <div class="cs-head">NOW</div>
      <div class="now-main">
        <span class="now-ico">${I.icon(o.icon, 44, 'var(--accent)')}</span>
        <span class="now-temp">${fmtTemp(o.tempF, t)}</span>
      </div>
      <div class="now-cond">${o.cond}</div>
      <div class="now-grid">
        <div class="ng"><span class="ng-k">FEELS</span><span class="ng-v">${fmtTemp(o.feelsF, t)}</span></div>
        <div class="ng"><span class="ng-k">WIND</span><span class="ng-v">${o.wind}</span></div>
        <div class="ng"><span class="ng-k">HUMID</span><span class="ng-v">${o.humid}%</span></div>
        <div class="ng"><span class="ng-k">HIGH</span><span class="ng-v">${fmtTemp(o.highF, t)}</span></div>
      </div>
      <div class="now-updated">UPDATED ${o.updated}</div>
    </div>`;
  }

  window.PebbleFace = { renderFace, renderPrecipDetail, renderForecast, renderNow, precipBars };
})();
