# Handoff: Pebble Time 2 — Weather Watchface (+ companion app)

## Overview
A glanceable, themeable weather watchface for the **Pebble Time 2** (rectangular
**200 × 228 px** color display), plus a small companion app with three
flick-cycled detail screens. **Hyperlocal precipitation is the headline feature**:
the face always shows the next two hours of rain as an 8-bucket bar strip, and a
conditional warning line appears when rain is imminent.

The project is intended to be **open-sourced and published**, so colors are
exposed as named, user-configurable roles and everything is built from the
Pebble 64-color palette.

---

## About the design files
The files in this bundle (`Pebble Weather Watchface.html` + `pebble/*.js|css`)
are **design references created in HTML** — an interactive prototype showing the
intended look, layout, color system, icon set, and the two face states. They are
**not** production code to copy.

The actual product is a **native Pebble watchface written in C** against the
**Pebble SDK / PebbleOS**, with a **PebbleKit JS** component on the phone for
weather data and a **Clay** configuration page for settings. Recreate the designs
in that environment using its established patterns. Because the Pebble Time 2 /
PebbleOS SDK was recently revived (Core Devices, 2025), **verify exact SDK API
names and resource tooling against the current SDK docs** — the architecture and
specs below are framework-accurate, but treat specific function signatures as
guidance, not gospel.

Open `Pebble Weather Watchface.html` in a browser to see everything live. The
control bar at the top toggles clock format, units, date order, accent/warning
colors, fresh/stale data, and zoom — use it to inspect every state.

## Fidelity
**High-fidelity.** Final layout geometry, palette, type sizing, icon forms, and
both data states are all specified. Reproduce the 200 × 228 face pixel-accurately.

---

## The canvas & rendering rules (read first)
- **200 × 228 px, rectangular, origin top-left.** Design is authored 1:1 — do not
  upscale source art.
- **64-color palette only (GColor8 — 2 bits per channel; each channel ∈
  {00, 55, AA, FF}).** Every color below is already palette-legal.
- **Flat color fields only.** No gradients, soft shadows, glows, blurs, or 1px
  hairlines — the panel bands them badly and AA detail looks muddy.
- Always-on display: optimize for **glanceability + high contrast**; only mark
  layers dirty when their data actually changes.
- Side padding is **9 px** on the status bar, weather row, and bottom zone.

---

## Color roles (theming)
Expose these as **named roles**, not hardcoded colors. Default theme is "Midnight"
(dark). `accent` and `warning` are **user-configurable** in settings; the rest can
optionally be themed too.

| Role | Default | Used for |
|---|---|---|
| `background` | `#000000` | Face fill. Pure black = max contrast + lowest always-on power. |
| `text-primary` | `#FFFFFF` | Time, current temp, step count — the glanceable layer. |
| `text-secondary` | `#AAAAAA` | Date, battery, high temp, captions, axis lines. |
| `accent` | `#00AAFF` | "Now" precip marker, forecast range bar, icon precip-drop tint, probability text. **Configurable.** |
| `warning` | `#FFFF00` | Rain-incoming alert line + companion headline. **Configurable.** |

### Precip intensity ramp (fixed sub-palette — intentionally **decoupled** from `accent`)
Rain severity must read consistently regardless of the user's accent choice, so
the bars use their own fixed 5-step ramp:

| Level | Name | Color | Bar height (of max) |
|---|---|---|---|
| 0 | none | `#555555` | 0.14 (short stub / axis tick) |
| 1 | trace | `#0055AA` | 0.34 |
| 2 | light | `#00AAFF` | 0.55 |
| 3 | moderate | `#55AAFF` | 0.78 |
| 4 | heavy | `#AAFFFF` | 1.00 |

Structural line/axis color: `#555555`.

> **Configurable-accent suggestion for the palette picker:** accents
> `#00AAFF #00FFAA #55FF00 #FFAA00 #FF55AA #AA55FF`; warnings
> `#FFFF00 #FFAA00 #FF5500 #FF0055 #FF55AA`. All palette-legal.

---

## Typography
- **One typeface: a clean bold grotesque.** The prototype uses **Archivo**
  (weights 700/800). On device, bundle a grotesque as font resources rasterized at
  the fixed sizes below (Pebble compiles bitmap fonts from TTF per size). Archivo,
  or the PebbleOS system bold (Raster Gothic / "Bitham"-class) if you want a pure
  on-platform look.
- Numerals are **tabular** (time, temps, steps). Time uses ~ -2px tracking at 66px.
- Minimum readable size on the face is ~14px bold; avoid anything thinner.

| Element | Size / weight |
|---|---|
| Time | 66 px / 800 |
| Current temp | 36 px / 800 |
| Step count | 25 px / 800 |
| High temp, condition | 18 px / 700 |
| Date, battery %, status line | 14–15 px / 700–800 |
| Captions (STEPS, axis labels) | 10 px / 700, ~1.2px tracking, uppercase |

---

## MAIN FACE — layout & element spec
Vertical zones down the 228 px height (x-padding 9 px unless noted):

```
┌──────────────────────────────────┐ y0
│ WED 6/4                 ▮▮▮ 82%   │  STATUS BAR        y0–26   h26
├──────────────────────────────────┤
│                                   │
│            10:42                  │  TIME (centered)   y26–108 h82
│                                   │
├──────────────────────────────────┤
│  ⛅  72°            H 78°          │  WEATHER ROW       y108–154 h46
├──────────────────────────────────┤
│  👟 STEPS      No rain · 2h        │  BOTTOM ZONE       y154–228 h74
│  8,432         ▁▁▂▁▁▂▁▁           │  (two columns)
└──────────────────────────────────┘ y228
```

### Status bar — `y0–26`
- **Date**, left, x9. `text-secondary`, 15px/700. Format `WED 6/4`. Supports M/D
  and D/M order (setting). Weekday is a 3-letter uppercase abbrev.
- **Battery**, right, x≈191. Shell 16×10, 2px border in `text-secondary`, inner
  fill width ∝ %, plus a 2px nub. `NN%` label 14px/700 `text-secondary`.

### Time — `y26–108`, centered
- 66px/800, `text-primary`, tabular, ~ -2px tracking.
- **12h is identical to 24h formatting** (leading zero kept) — hours simply wrap
  **01–12** instead of going to 13–23. **No AM/PM is shown.** (e.g. 13:05 → `01:05`,
  00:30 → `12:30`.) 24h shows `00`–`23`.

### Weather row — `y108–154`, x9, row gap 9
- **Condition icon** 44×44, left. Monochrome `text-primary`; precip-bearing glyphs
  (rain/drizzle/sleet/thunder) tint their drops/bolt with `accent`.
- **Current temp**, 36px/800 `text-primary`. `72°` / supports °C/°F (setting) and
  the range `-12°…105°`.
- **Today's high**, right-aligned (margin-left:auto). `H 78°`, 18px/700
  `text-secondary`.

### Bottom zone — `y154–228`, flex row, `align-items:flex-end`,
`justify-content:space-between`, gap 14, x-padding 9, bottom-padding 9.
Two columns:

**Left — steps block** (no goal meter):
- Caption row: 14px footprint glyph + `STEPS` (10px/700, 1.2px tracking,
  `text-secondary`).
- Count below: `8,432`, 25px/800 `text-primary`, tabular, thousands-separated.

**Right — precip block** (flex:1, column, gap 8):
- **Status line** (one line):
  - *Rain-incoming state:* `RAIN IN 20M` (also e.g. `CLEARING IN 35M`), 15px/800,
    **`warning`** color.
  - *Dry state:* `No rain · 2h`, 14px/700 `text-secondary`.
- **Precip strip:** 8 bars, flex-filling the column width, gap 3, max bar height
  ~26px, sitting on a 2px `#555555` baseline axis. Bars colored/sized by the
  intensity ramp above. Each bar = a 15-minute bucket; 8 buckets = next 2 hours.
  In the rain state the **first ("now") bar** gets a 3px `accent` cap to mark
  the current bucket.

### Two required states
Ship/handle both:
- **(A) Dry / no alert** — calm status line, flat low precip strip, no warning.
- **(B) Rain incoming** — `warning`-colored alert line, rising precip bars, now-marker.

### Stale-data indicator
When the companion hasn't pushed fresh weather recently: dim the **weather row** to
~42% opacity and draw a 7px `text-secondary` dot at the top-right of the icon.
(On device, "dim" = step down to a darker palette entry, e.g. white→`#AAAAAA`,
since there's no real alpha — or simply render the weather glyph/text in
`text-secondary` and add the dot.)

---

## WEATHER ICON SET (12 glyphs, two sizes: 44×44 and 24×24)
Solid, flat, chunky forms drawn on a 44 grid. Monochrome in `text-primary`;
precip glyphs tint drops/bolt with `accent`. The 24px version is the same artwork
scaled for the 5-day rows. Build them either as programmatic vector fills
(`gpath`/graphics primitives — crisp at both sizes) or as pre-rasterized flat
PNG `GBitmap`s per size.

| Name | Composition |
|---|---|
| `clear-day` | Sun: filled disc + 8 radiating rounded rays. |
| `clear-night` | Crescent: filled disc with a `background`-colored disc "bite" offset to the upper-right. |
| `partly-cloudy-day` | Small sun upper-left + cloud lower-right. |
| `partly-cloudy-night` | Small crescent upper-left + cloud. |
| `cloudy` | Two overlapping clouds (back one dimmed). |
| `fog` | Cloud + 3 horizontal rounded lines below. |
| `drizzle` | Cloud + 3 short accent drops. |
| `rain` | Cloud + 3 longer accent drops. |
| `thunderstorm` | Cloud + accent lightning-bolt polygon + one drop. |
| `snow` | Cloud + 3 diamond flakes. |
| `sleet` | Cloud + drops + a flake (mix). |
| `wind` | 3 horizontal rounded lines with curled ends. |

Cloud primitive = three overlapping circles + a rounded base bar, all the same
fill (overlap merges visually). Sun = disc + N rays. Crescent = disc minus an
offset background-colored disc.

---

## COMPANION APP — three screens (each 200 × 228)
Opened from the face via **long-press Quick Launch**; the user **wrist-flicks** to
cycle screens. Same canvas, palette, and type as the face.

### 1 · 2-hour precip detail
- Header `2-HOUR PRECIP` (12px/800, `text-secondary`, 1.6px tracking).
- Headline in `warning`, one line: e.g. `RAIN IN ~20 MIN` (or `CLEARING IN ~35 MIN`).
- Big bar chart: 8 flex bars, height ~84, on a 2px `#555555` axis, colored by the
  same 5-level ramp.
- Probability row beneath, in `accent`: 5 values (e.g. `10% 35% 70% 90% 80%`).
- Time-axis labels: `NOW +30 +60 +90 +120` (`text-secondary`, 11px/700).
- Legend: 5 swatches + labels `NONE TRACE LIGHT MOD HEAVY` (9.5px/700).

### 2 · 5-day forecast (key screen)
Header `5-DAY FORECAST`, then 5 rows, each (height ~34, gap 8):
- Weekday, 36px col, 15px/800 `text-primary`.
- 24px condition icon.
- Hi/lo **range bar** (flex): track `#555555`; filled range in `accent`,
  positioned by normalizing each day's lo→hi against the week min/max.
- High temp, 15px/800 `text-primary`; Low temp, 15px/700 `text-secondary`.

### 3 · Now details (optional)
Header `NOW`; 44px icon + 54px/800 current temp; condition text (18px/700, e.g.
`Partly Cloudy`); a 2×2 grid with top-border separators: `FEELS`, `WIND`, `HUMID`,
`HIGH` (10–11px caption `text-secondary` + 15px/800 value); footer
`UPDATED 10:38` (10px/700 `text-secondary`, 1px tracking).

---

## Data model
The face/app consumes:

```
time:    { h: 0–23, m: 0–59 }                 // per-minute
date:    { weekday: 0–6, month, day }         // per-day
battery: 0–100                                 // on event
steps:   integer                               // on event
weather: {                                     // on companion push (~10–30 min)
  tempF, highF,
  icon: <one of the 12 names>,
  precip: [l0..l7]  // 8 levels, each 0–4, 15-min buckets = next 2h
  rain: bool,       // drives state A vs B
  warnText: "RAIN IN 20M" | "CLEARING IN 35M" | null,
  stale: bool
}
companion: {
  probs: [p0..p4],                 // precip-detail probabilities, %
  days:  [{ weekday, icon, hiT, loT } × 5],
  now:   { feelsF, condition, wind, humidPct, updated }
}
```

### Static vs dynamic (redraw budget)
- **Per-minute:** time.
- **Per-day:** date.
- **On event:** battery, steps.
- **On companion push (~10–30 min):** temp, high, icon, precip bars, warning line,
  stale flag.

Only invalidate the affected layer(s) on change to preserve always-on power.

---

## Settings (Clay config page → persist storage)
Expose, persist (`persist_write_*`), and apply on launch:
- `accent` color (palette swatches) → `accent` role.
- `warning` color (palette swatches) → `warning` role.
- Clock format: 24h / 12h (12h = wrap-to-12, leading zero, no AM/PM).
- Units: °F / °C.
- Date order: M/D / D/M.
- (Optional) `background` / text roles if you choose to theme them too.

---

## Suggested Pebble implementation notes
- **Language/SDK:** C on PebbleOS, target the Time 2 (200×228 color, GColor8).
- **Rendering:** a root `Window` with a `Layer` update proc (or a few sublayers:
  status, time, weather, bottom). Draw flat fills with `graphics_context_set_fill_color`
  / `graphics_fill_rect`; bars are plain rects; the battery shell is a stroked +
  filled rect; the precip axis is a 2px `graphics_fill_rect`.
- **Colors:** map each role to a `GColor` built from the palette hex (use
  `GColorFromHEX`-style 8-bit values). Keep them in a `theme` struct read from
  persist.
- **Fonts:** add the grotesque as `.ttf` font **resources** at the sizes in the
  Typography table (Pebble bitmap-compiles per size). Use `fonts_load_custom_font`.
- **Icons:** either draw via `gpath_create` + `gpath_draw_filled` (vector, crisp
  at 44 & 24, easy to recolor per role), or ship flat PNGs as `GBitmap` resources.
  Vector is recommended given the simple geometric forms and the accent tinting.
- **Weather data:** a **PebbleKit JS** phone component fetches from a weather API
  that provides a **minutely/nowcast precip** series (to fill the 8 buckets) plus
  current/high/condition and a 5-day forecast; send to the watch via **AppMessage**.
- **Wrist-flick cycling:** `accel_tap_service_subscribe` (or the companion is a
  separate watchapp launched via Quick Launch that pages on flick/tap).
- **Glanceability:** keep the always-on face legible at the dimmest backlight;
  avoid per-second animation on the face.

---

## Design decisions worth preserving
- Precip ramp is **decoupled from `accent`** so rain severity always reads the
  same regardless of the user's accent color.
- **12h = 24h layout** (leading zero) wrapping 01–12, **no AM/PM** — keeps the big
  centered time visually identical in both modes and avoids a clipped suffix.
- **One grotesque typeface**, committed (no multi-font system).
- Bottom zone is a **two-column split** (steps | precip+status) with **no step
  goal meter** — chosen to relieve crowding and give the precip timeline real width.
- Precip is the star: it gets the wider bottom-right column and the bars flex to
  fill it.

---

## Files in this bundle
- `Pebble Weather Watchface.html` — the interactive design reference (open this).
- `pebble/studio.css` — all screen + studio styles (the watch screen rules are the
  source of truth for geometry, sizes, and colors).
- `pebble/icons.js` — the 12 weather glyphs (vector construction reference).
- `pebble/watchface.js` — render logic for the face + 3 companion screens
  (layout/data reference).
- `pebble/app.js` — demo data, the live control/settings wiring, color-role +
  precip-ramp definitions, and the element spec table.
```
