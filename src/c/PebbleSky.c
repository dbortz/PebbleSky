#include <pebble.h>

// ============================================================================
// PebbleSky — weather watchface for the Pebble Time 2 (emery, 200x228, color).
//
// First implementation pass: full layout drawn from the Claude Design handoff
// (see design/README.md). Uses system fonts and demo weather/steps/battery data
// so we can validate layout fidelity in the emulator. Next passes wire real
// services (HealthService, BatteryStateService), AppMessage/PebbleKit JS weather,
// the gpath weather-icon set, custom TTF fonts at exact sizes, and Clay theming.
// ============================================================================

// ---- Theme (named color roles; default "Midnight"). Configurable later. ----
typedef struct {
  GColor background;     // #000000
  GColor text_primary;   // #FFFFFF
  GColor text_secondary; // #AAAAAA
  GColor accent;         // #00AAFF
  GColor warning;        // #FFFF00
} Theme;

static Theme s_theme;

// Fixed 5-step precip intensity ramp (decoupled from accent on purpose).
static GColor s_precip_ramp[5];                 // none..heavy
static const int s_precip_h_num[5] = {14, 34, 55, 78, 100}; // % of max bar height

// ---- Demo data (TODO: replace with real services + AppMessage) -------------
typedef struct {
  int   battery;        // 0-100
  int   temp_f;
  int   high_f;
  int   steps;
  int   icon;           // 0-11, matches the icon dispatcher / icons.js ORDER
  bool  rain;
  bool  stale;
  char  warn[20];       // e.g. "CLEARING IN 35M"
  uint8_t precip[8];    // visible 2h window (0-4), recomputed each minute
} FaceData;

static FaceData s_data = {
  .battery = 82,
  .temp_f  = 72,
  .high_f  = 78,
  .steps   = 8432,
  .icon    = 2,         // partly-cloudy-day (demo until weather arrives)
  .rain    = false,
  .stale   = false,
  .warn    = "",
  .precip  = {0, 0, 1, 0, 0, 1, 0, 0},
};

// Rolling 4h precip buffer + anchor. The phone over-fetches; the watch slides a
// 2h (8-bucket) window over this and recomputes the now-marker + warning every
// minute against the real clock, so labels stay accurate between fetches.
#define BUF_N 16
#define WIN_N 8
static uint8_t s_wx_buf[BUF_N];
static int     s_wx_buf_len = 0;
static time_t  s_wx_anchor = 0;       // epoch of s_wx_buf[0]
static time_t  s_wx_last_update = 0;  // when the last AppMessage arrived
static bool    s_wx_have = false;     // received at least one forecast

static Window *s_window;
static Layer  *s_canvas;
static char    s_time_buf[8];
static char    s_date_buf[16];

// Custom Archivo bitmap fonts (loaded in window_load).
static GFont s_f_time;  // ExtraBold 66 — time
static GFont s_f_temp;  // ExtraBold 36 — current temp
static GFont s_f_steps; // ExtraBold 25 — step count
static GFont s_f_18;    // Bold 18 — high temp
static GFont s_f_14;    // Bold 14 — date, battery, status line
static GFont s_f_cap;   // Bold 10 — captions (STEPS)

static const char *WD[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

// ---- Layout constants (px), from design/README.md --------------------------
#define PAD        9
#define W          200
#define H          228
#define STATUS_H   26
#define TIME_Y     26
#define TIME_H     82
#define WROW_Y     108
#define WROW_H     46
#define BOT_Y      154

// ---------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------
static void draw_text(GContext *ctx, const char *s, GFont f, GRect box,
                      GColor color, GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Chunky cloud = 3 overlapping discs + a rounded base bar (icons.js recipe),
// authored on a 44 grid, drawn at icon origin (ox,oy) with scale s.
static void draw_cloud(GContext *ctx, int ox, int oy, GPoint c, float s, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  int cx = ox + c.x, cy = oy + c.y;
  graphics_fill_circle(ctx, GPoint(cx - (int)(8*s), cy + (int)(1*s)), (int)(8*s));
  graphics_fill_circle(ctx, GPoint(cx + (int)(7*s), cy + (int)(1*s)), (int)(9*s));
  graphics_fill_circle(ctx, GPoint(cx - (int)(1*s), cy - (int)(5*s)), (int)(8*s));
  GRect base = GRect(cx - (int)(14*s), cy, (int)(28*s), (int)(9*s));
  graphics_fill_rect(ctx, base, (int)(4*s), GCornersBottom);
}

// Sun = filled disc + N radiating rays.
static void draw_sun(GContext *ctx, int ox, int oy, GPoint c, int r, int ray, GColor fill) {
  int cx = ox + c.x, cy = oy + c.y;
  graphics_context_set_stroke_color(ctx, fill);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int dx0 = (cos_lookup(a) * (r + 2))  / TRIG_MAX_RATIO;
    int dy0 = (sin_lookup(a) * (r + 2))  / TRIG_MAX_RATIO;
    int dx1 = (cos_lookup(a) * (r + 2 + ray)) / TRIG_MAX_RATIO;
    int dy1 = (sin_lookup(a) * (r + 2 + ray)) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(cx+dx0, cy+dy0), GPoint(cx+dx1, cy+dy1));
  }
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
}

// Crescent moon = disc with a background-colored bite (offset upper-right).
static void draw_moon(GContext *ctx, int ox, int oy, GPoint c, int r, GColor fill) {
  int cx = ox + c.x, cy = oy + c.y;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
  graphics_context_set_fill_color(ctx, s_theme.background);
  graphics_fill_circle(ctx, GPoint(cx + (r*55)/100, cy - (r*38)/100), (r*92)/100);
}

// Vertical rounded "drop".
static void draw_drop(GContext *ctx, int ox, int oy, int x, int y, int h, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(ox + x - 1, oy + y, 3, h), 1, GCornersAll);
}

// N horizontal rounded lines (fog / wind).
static void draw_hlines(GContext *ctx, int ox, int oy, const int *yy,
                        const int *x0, const int *x1, int n, GColor fill) {
  graphics_context_set_stroke_color(ctx, fill);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < n; i++)
    graphics_draw_line(ctx, GPoint(ox + x0[i], oy + yy[i]), GPoint(ox + x1[i], oy + yy[i]));
}

// Dispatch one of the 12 weather glyphs (44 grid). Precip drops/bolt use accent.
static void draw_weather_icon(GContext *ctx, int ox, int oy, int icon) {
  GColor pri = s_data.stale ? s_theme.text_secondary : s_theme.text_primary;
  GColor acc = s_theme.accent;
  switch (icon) {
    case 0: draw_sun(ctx, ox, oy, GPoint(22, 22), 9, 8, pri); break;
    case 1: draw_moon(ctx, ox, oy, GPoint(24, 22), 14, pri); break;
    case 2: draw_sun(ctx, ox, oy, GPoint(15, 14), 6, 6, pri);
            draw_cloud(ctx, ox, oy, GPoint(25, 24), 0.92f, pri); break;
    case 3: draw_moon(ctx, ox, oy, GPoint(15, 14), 9, pri);
            draw_cloud(ctx, ox, oy, GPoint(25, 24), 0.92f, pri); break;
    case 4: draw_cloud(ctx, ox, oy, GPoint(15, 15), 0.72f, s_theme.text_secondary);
            draw_cloud(ctx, ox, oy, GPoint(24, 25), 1.0f, pri); break;
    case 5: { draw_cloud(ctx, ox, oy, GPoint(22, 16), 0.9f, pri);
              int yy[3]={32,37,42}, a[3]={11,9,13}, b[3]={33,35,31};
              draw_hlines(ctx, ox, oy, yy, a, b, 3, pri); } break;
    case 6: draw_cloud(ctx, ox, oy, GPoint(22, 16), 0.96f, pri);
            draw_drop(ctx, ox, oy, 15, 33, 6, acc);
            draw_drop(ctx, ox, oy, 22, 35, 6, acc);
            draw_drop(ctx, ox, oy, 29, 33, 6, acc); break;
    case 7: draw_cloud(ctx, ox, oy, GPoint(22, 15), 0.98f, pri);
            draw_drop(ctx, ox, oy, 14, 33, 9, acc);
            draw_drop(ctx, ox, oy, 22, 35, 9, acc);
            draw_drop(ctx, ox, oy, 30, 33, 9, acc); break;
    case 8: { draw_cloud(ctx, ox, oy, GPoint(22, 14), 0.98f, pri);
              draw_drop(ctx, ox, oy, 14, 33, 7, pri);
              graphics_context_set_stroke_color(ctx, acc);
              graphics_context_set_stroke_width(ctx, 3);
              graphics_draw_line(ctx, GPoint(ox+27, oy+29), GPoint(ox+22, oy+35));
              graphics_draw_line(ctx, GPoint(ox+22, oy+35), GPoint(ox+27, oy+35));
              graphics_draw_line(ctx, GPoint(ox+27, oy+35), GPoint(ox+22, oy+42)); } break;
    case 9: draw_cloud(ctx, ox, oy, GPoint(22, 15), 0.98f, pri);
            graphics_context_set_fill_color(ctx, pri);
            graphics_fill_circle(ctx, GPoint(ox+15, oy+35), 2);
            graphics_fill_circle(ctx, GPoint(ox+23, oy+38), 2);
            graphics_fill_circle(ctx, GPoint(ox+31, oy+35), 2); break;
    case 10: draw_cloud(ctx, ox, oy, GPoint(22, 15), 0.98f, pri);
             draw_drop(ctx, ox, oy, 15, 33, 8, acc);
             graphics_context_set_fill_color(ctx, pri);
             graphics_fill_circle(ctx, GPoint(ox+24, oy+37), 2);
             draw_drop(ctx, ox, oy, 31, 33, 8, acc); break;
    case 11: { int yy[3]={17,24,31}, a[3]={8,8,8}, b[3]={28,34,24};
               draw_hlines(ctx, ox, oy, yy, a, b, 3, pri); } break;
    default: draw_cloud(ctx, ox, oy, GPoint(24, 22), 1.0f, pri); break;
  }
}

// Battery: shell (16x10, 2px border) + inner fill ∝ %, + nub, + "NN%".
static void draw_battery(GContext *ctx) {
  const int shell_w = 16, shell_h = 10;
  const int sx = 138, sy = (STATUS_H - shell_h) / 2; // ~8
  graphics_context_set_stroke_color(ctx, s_theme.text_secondary);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, GRect(sx, sy, shell_w, shell_h));
  // nub
  graphics_context_set_fill_color(ctx, s_theme.text_secondary);
  graphics_fill_rect(ctx, GRect(sx + shell_w, sy + 3, 2, 4), 0, GCornerNone);
  // inner fill
  int fw = (13 * s_data.battery) / 100;
  if (fw < 1) fw = 1;
  graphics_fill_rect(ctx, GRect(sx + 2, sy + 2, fw, shell_h - 4), 0, GCornerNone);
  // percent
  static char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", s_data.battery);
  draw_text(ctx, pct, s_f_14,
            GRect(sx + shell_w + 5, 2, W - (sx + shell_w + 5) - PAD, 20),
            s_theme.text_secondary, GTextAlignmentLeft);
}

// Footprint glyph (~16px) approximation for the STEPS caption.
static void draw_footprint(GContext *ctx, int x, int y, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(x + 5, y + 8), 5);   // heel/ball
  graphics_fill_circle(ctx, GPoint(x + 10, y + 3), 2);  // toes
  graphics_fill_circle(ctx, GPoint(x + 12, y + 6), 2);
}

// 8 precip bars on a 2px baseline, heights/colors by the ramp.
static void draw_precip_strip(GContext *ctx, GRect area, bool mark_now) {
  const int n = 8, gap = 3, max_h = 26;
  int bw = (area.size.w - gap * (n - 1)) / n;
  int base_y = area.origin.y + area.size.h;           // baseline (bottom)
  // axis
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(area.origin.x, base_y, area.size.w, 2), 0, GCornerNone);
  for (int i = 0; i < n; i++) {
    int lv = s_data.precip[i]; if (lv > 4) lv = 4;
    int h = (s_precip_h_num[lv] * max_h) / 100; if (h < 2) h = 2;
    int x = area.origin.x + i * (bw + gap);
    graphics_context_set_fill_color(ctx, s_precip_ramp[lv]);
    graphics_fill_rect(ctx, GRect(x, base_y - h, bw, h), 0, GCornerNone);
    if (mark_now && i == 0) { // 3px accent cap on the "now" bar
      graphics_context_set_fill_color(ctx, s_theme.accent);
      graphics_fill_rect(ctx, GRect(x, base_y - h, bw, 3), 0, GCornerNone);
    }
  }
}

// ---------------------------------------------------------------------------
// Root render
// ---------------------------------------------------------------------------
static void canvas_update(Layer *layer, GContext *ctx) {
  // Background
  graphics_context_set_fill_color(ctx, s_theme.background);
  graphics_fill_rect(ctx, GRect(0, 0, W, H), 0, GCornerNone);

  // --- Status bar: date (left), battery (right) ---
  draw_text(ctx, s_date_buf, s_f_14, GRect(PAD, 4, 120, 20),
            s_theme.text_secondary, GTextAlignmentLeft);
  draw_battery(ctx);

  // --- Time (centered) ---
  draw_text(ctx, s_time_buf, s_f_time, GRect(0, TIME_Y - 2, W, TIME_H),
            s_theme.text_primary, GTextAlignmentCenter);

  // --- Weather row: icon, current temp, high (right) ---
  GColor c_primary = s_data.stale ? s_theme.text_secondary : s_theme.text_primary;
  draw_weather_icon(ctx, PAD, WROW_Y, s_data.icon);
  static char temp[8], high[10];
  snprintf(temp, sizeof(temp), "%d°", s_data.temp_f);
  snprintf(high, sizeof(high), "H %d°", s_data.high_f);
  draw_text(ctx, temp, s_f_temp, GRect(PAD + 52, WROW_Y + 2, 95, 42),
            c_primary, GTextAlignmentLeft);
  draw_text(ctx, high, s_f_18, GRect(W - PAD - 70, WROW_Y + 14, 70, 24),
            s_theme.text_secondary, GTextAlignmentRight);
  if (s_data.stale) { // stale dot at top-right of icon
    graphics_context_set_fill_color(ctx, s_theme.text_secondary);
    graphics_fill_circle(ctx, GPoint(PAD + 40, WROW_Y + 4), 4);
  }

  // --- Bottom zone: steps (left col) | precip (right col) ---
  // Steps caption + count, bottom-aligned.
  draw_footprint(ctx, PAD, 169, s_theme.text_secondary);
  draw_text(ctx, "STEPS", s_f_cap, GRect(PAD + 20, 171, 70, 14),
            s_theme.text_secondary, GTextAlignmentLeft);
  static char steps[12];
  if (s_data.steps >= 1000) {
    snprintf(steps, sizeof(steps), "%d,%03d", s_data.steps / 1000, s_data.steps % 1000);
  } else {
    snprintf(steps, sizeof(steps), "%d", s_data.steps);
  }
  draw_text(ctx, steps, s_f_steps, GRect(PAD, 188, 95, 34),
            s_theme.text_primary, GTextAlignmentLeft);

  // Precip status line + strip, right column.
  const int rcx = 101, rcw = W - 101 - PAD; // 90
  if (s_data.rain) {
    draw_text(ctx, s_data.warn, s_f_14, GRect(rcx, 168, rcw, 18),
              s_theme.warning, GTextAlignmentLeft);
  } else {
    draw_text(ctx, "No rain · 2h", s_f_14, GRect(rcx, 169, rcw, 18),
              s_theme.text_secondary, GTextAlignmentLeft);
  }
  draw_precip_strip(ctx, GRect(rcx, 192, rcw, 26), s_data.rain);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static void update_time(struct tm *t) {
  strftime(s_time_buf, sizeof(s_time_buf),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  snprintf(s_date_buf, sizeof(s_date_buf), "%s %d/%d",
           WD[t->tm_wday], t->tm_mon + 1, t->tm_mday);
}

// Slide the 2h window over the 4h buffer and recompute rain state + warning,
// anchored to the *current* clock so the countdown stays accurate between fetches.
static void recompute_weather(void) {
  if (!s_wx_have) return;  // keep demo data until the first real forecast
  time_t now = time(NULL);
  int idx = (int)((now - s_wx_anchor) / 900);  // 900s = 15 min
  if (idx < 0) idx = 0;

  for (int i = 0; i < WIN_N; i++) {
    int b = idx + i;
    s_data.precip[i] = (b < s_wx_buf_len) ? s_wx_buf[b] : 0;
  }

  s_data.rain = false;
  s_data.warn[0] = '\0';
  if (s_data.precip[0] > 0) {
    // Raining now: count down to the first dry bucket.
    int clear_b = -1;
    for (int i = 1; i < WIN_N; i++) if (s_data.precip[i] == 0) { clear_b = idx + i; break; }
    s_data.rain = true;
    if (clear_b >= 0) {
      int m = (int)((s_wx_anchor + (time_t)clear_b * 900 - now) / 60);
      if (m < 1) m = 1;
      snprintf(s_data.warn, sizeof(s_data.warn), "CLEARING IN %dM", m);
    } else {
      snprintf(s_data.warn, sizeof(s_data.warn), "RAIN NEXT 2H");
    }
  } else {
    // Dry now: count down to the first wet bucket, if any in the window.
    for (int i = 1; i < WIN_N; i++) {
      if (s_data.precip[i] > 0) {
        int m = (int)((s_wx_anchor + (time_t)(idx + i) * 900 - now) / 60);
        if (m < 1) m = 1;
        s_data.rain = true;
        snprintf(s_data.warn, sizeof(s_data.warn), "RAIN IN %dM", m);
        break;
      }
    }
  }

  // Stale: no fresh push in 60 min, or the buffer no longer covers "now".
  bool old = (now - s_wx_last_update) > 60 * 60;
  bool exhausted = (idx >= s_wx_buf_len);
  s_data.stale = old || exhausted;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  recompute_weather();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---- Battery + Health (real sensors) --------------------------------------
static void battery_handler(BatteryChargeState state) {
  s_data.battery = state.charge_percent;
  if (s_canvas) layer_mark_dirty(s_canvas);
}

#if defined(PBL_HEALTH)
static void refresh_steps(void) {
  HealthServiceAccessibilityMask ok = health_service_metric_accessible(
      HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (ok & HealthServiceAccessibilityMaskAvailable) {
    s_data.steps = (int)health_service_sum_today(HealthMetricStepCount);
  }
}
static void health_handler(HealthEventType event, void *context) {
  if (event != HealthEventSleepUpdate) {
    refresh_steps();
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
}
#endif

// ---- Weather via AppMessage (PebbleKit JS pushes the canonical model) -------
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_TEMP)))   s_data.temp_f = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_HIGH)))   s_data.high_f = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ICON)))   s_data.icon   = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ANCHOR))) s_wx_anchor   = (time_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_PRECIP))) {
    int n = t->length; if (n > BUF_N) n = BUF_N;
    for (int i = 0; i < n; i++) s_wx_buf[i] = t->value->data[i];
    s_wx_buf_len = n;
  }
  s_wx_have = true;
  s_wx_last_update = time(NULL);
  recompute_weather();  // derive the visible window + warning from the new buffer
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static void prv_window_load(Window *window) {
  s_f_time  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_XB_66));
  s_f_temp  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_XB_36));
  s_f_steps = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_XB_25));
  s_f_18    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_18));
  s_f_14    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_14));
  s_f_cap   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_10));

  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  time_t now = time(NULL);
  update_time(localtime(&now));
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  fonts_unload_custom_font(s_f_time);
  fonts_unload_custom_font(s_f_temp);
  fonts_unload_custom_font(s_f_steps);
  fonts_unload_custom_font(s_f_18);
  fonts_unload_custom_font(s_f_14);
  fonts_unload_custom_font(s_f_cap);
}

static void prv_init(void) {
  s_theme = (Theme){
    .background     = GColorBlack,
    .text_primary   = GColorWhite,
    .text_secondary = GColorLightGray,
    .accent         = GColorVividCerulean,
    .warning        = GColorYellow,
  };
  s_precip_ramp[0] = GColorDarkGray;                    // #555555 none
  s_precip_ramp[1] = GColorFromRGB(0x00, 0x55, 0xAA);   // #0055AA trace
  s_precip_ramp[2] = GColorVividCerulean;               // #00AAFF light
  s_precip_ramp[3] = GColorFromRGB(0x55, 0xAA, 0xFF);   // #55AAFF moderate
  s_precip_ramp[4] = GColorFromRGB(0xAA, 0xFF, 0xFF);   // #AAFFFF heavy

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  battery_handler(battery_state_service_peek());
  battery_state_service_subscribe(battery_handler);
#if defined(PBL_HEALTH)
  refresh_steps();
  health_service_events_subscribe(health_handler, NULL);
#endif

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
