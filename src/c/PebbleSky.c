#include <pebble.h>
#include "../../shared/pebblesky_render.h"

// ============================================================================
// PebbleSky — weather watchface for the Pebble Time 2 (emery, 200x228, color).
// Layout from the Claude Design handoff (design/README.md). Weather data arrives
// from PebbleKit JS over AppMessage; the 12-glyph icon set + precip bars are
// drawn from the shared header (shared/pebblesky_render.h), also used by the
// companion app.
// ============================================================================

// ---- Theme (named color roles; default "Midnight"). Configurable via Clay. --
typedef struct {
  GColor background;     // #000000
  GColor text_primary;   // #FFFFFF
  GColor text_secondary; // #AAAAAA
  GColor accent;         // #00AAFF
  GColor warning;        // #FFFF00
} Theme;

static Theme s_theme;
static GColor s_precip_ramp[5];   // fixed 5-step intensity ramp (see psky_ramp)

// ---- User settings (Clay config page; persisted across launches) -----------
typedef struct {
  bool   celsius;   // false = Fahrenheit
  bool   clock24;   // true = 24h; false = 12h (wrap 01-12, no AM/PM)
  bool   date_dmy;  // false = M/D; true = D/M
  GColor accent;
  GColor warning;
} Settings;
static Settings s_cfg;

enum { PK_CELSIUS = 1, PK_CLOCK24, PK_DATEDMY, PK_ACCENT, PK_WARNING };

// Clay sends colors as a 24-bit RGB integer; quantize to the Pebble palette.
static GColor color_from_int(int32_t v) {
  return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}
// Convert a stored °F into the display unit (integer-rounded).
static int disp_temp(int f) {
  if (!s_cfg.celsius) return f;
  int n = (f - 32) * 5;
  return (n >= 0) ? (n + 4) / 9 : (n - 4) / 9;
}

// ---- Weather/face data -----------------------------------------------------
typedef struct {
  int   battery;        // 0-100
  int   temp_f;
  int   high_f;
  int   steps;
  int   icon;           // 0-11, matches the shared icon dispatcher
  bool  rain;
  bool  stale;
  char  warn[20];       // e.g. "CLEARING IN 35M"
  uint8_t precip[8];    // visible 2h window (0-4), recomputed each minute
} FaceData;

static FaceData s_data = {
  .battery = 82, .temp_f = 72, .high_f = 78, .steps = 8432,
  .icon = 2, .rain = false, .stale = false, .warn = "",
  .precip = {0, 0, 1, 0, 0, 1, 0, 0},
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

static void draw_text(GContext *ctx, const char *s, GFont f, GRect box,
                      GColor color, GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Battery: shell (16x10, 2px border) + inner fill ∝ %, + nub, + "NN%".
static void draw_battery(GContext *ctx) {
  const int shell_w = 16, shell_h = 10;
  const int sx = 138, sy = (STATUS_H - shell_h) / 2;
  graphics_context_set_stroke_color(ctx, s_theme.text_secondary);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_rect(ctx, GRect(sx, sy, shell_w, shell_h));
  graphics_context_set_fill_color(ctx, s_theme.text_secondary);
  graphics_fill_rect(ctx, GRect(sx + shell_w, sy + 3, 2, 4), 0, GCornerNone);
  int fw = (13 * s_data.battery) / 100; if (fw < 1) fw = 1;
  graphics_fill_rect(ctx, GRect(sx + 2, sy + 2, fw, shell_h - 4), 0, GCornerNone);
  static char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", s_data.battery);
  draw_text(ctx, pct, s_f_14, GRect(sx + shell_w + 5, 2, W - (sx + shell_w + 5) - PAD, 20),
            s_theme.text_secondary, GTextAlignmentLeft);
}

// Footprint glyph (~16px) for the STEPS caption.
static void draw_footprint(GContext *ctx, int x, int y, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(x + 5, y + 8), 5);
  graphics_fill_circle(ctx, GPoint(x + 10, y + 3), 2);
  graphics_fill_circle(ctx, GPoint(x + 12, y + 6), 2);
}

// ---------------------------------------------------------------------------
// Root render
// ---------------------------------------------------------------------------
static void canvas_update(Layer *layer, GContext *ctx) {
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
  psky_icon(ctx, PAD, WROW_Y, 44, s_data.icon,
            c_primary, s_theme.text_secondary, s_theme.accent, s_theme.background);
  static char temp[8], high[10];
  snprintf(temp, sizeof(temp), "%d°", disp_temp(s_data.temp_f));
  snprintf(high, sizeof(high), "H %d°", disp_temp(s_data.high_f));
  draw_text(ctx, temp, s_f_temp, GRect(PAD + 52, WROW_Y + 2, 95, 42),
            c_primary, GTextAlignmentLeft);
  draw_text(ctx, high, s_f_18, GRect(W - PAD - 70, WROW_Y + 14, 70, 24),
            s_theme.text_secondary, GTextAlignmentRight);
  if (s_data.stale) {
    graphics_context_set_fill_color(ctx, s_theme.text_secondary);
    graphics_fill_circle(ctx, GPoint(PAD + 40, WROW_Y + 4), 4);
  }

  // --- Bottom zone: steps (left col) | precip (right col) ---
  draw_footprint(ctx, PAD, 169, s_theme.text_secondary);
  draw_text(ctx, "STEPS", s_f_cap, GRect(PAD + 20, 171, 70, 14),
            s_theme.text_secondary, GTextAlignmentLeft);
  static char steps[12];
  if (s_data.steps >= 1000)
    snprintf(steps, sizeof(steps), "%d,%03d", s_data.steps / 1000, s_data.steps % 1000);
  else
    snprintf(steps, sizeof(steps), "%d", s_data.steps);
  draw_text(ctx, steps, s_f_steps, GRect(PAD, 188, 95, 34),
            s_theme.text_primary, GTextAlignmentLeft);

  const int rcx = 101, rcw = W - 101 - PAD; // 90
  if (s_data.rain)
    draw_text(ctx, s_data.warn, s_f_14, GRect(rcx, 168, rcw, 18),
              s_theme.warning, GTextAlignmentLeft);
  else
    draw_text(ctx, "No rain · 2h", s_f_14, GRect(rcx, 169, rcw, 18),
              s_theme.text_secondary, GTextAlignmentLeft);
  psky_precip_bars(ctx, GRect(rcx, 192, rcw, 26), s_data.precip, 8,
                   s_precip_ramp, s_theme.accent, s_data.rain, 26);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static void update_time(struct tm *t) {
  strftime(s_time_buf, sizeof(s_time_buf), s_cfg.clock24 ? "%H:%M" : "%I:%M", t);
  if (s_cfg.date_dmy)
    snprintf(s_date_buf, sizeof(s_date_buf), "%s %d/%d", WD[t->tm_wday], t->tm_mday, t->tm_mon + 1);
  else
    snprintf(s_date_buf, sizeof(s_date_buf), "%s %d/%d", WD[t->tm_wday], t->tm_mon + 1, t->tm_mday);
}

// Slide the 2h window over the 4h buffer and recompute rain state + warning,
// anchored to the *current* clock so the countdown stays accurate between fetches.
static void recompute_weather(void) {
  if (!s_wx_have) return;
  time_t now = time(NULL);
  int idx = (int)((now - s_wx_anchor) / 900);
  if (idx < 0) idx = 0;

  for (int i = 0; i < WIN_N; i++) {
    int b = idx + i;
    s_data.precip[i] = (b < s_wx_buf_len) ? s_wx_buf[b] : 0;
  }

  s_data.rain = false;
  s_data.warn[0] = '\0';
  if (s_data.precip[0] > 0) {
    int clear_b = -1;
    for (int i = 1; i < WIN_N; i++) if (s_data.precip[i] == 0) { clear_b = idx + i; break; }
    s_data.rain = true;
    if (clear_b >= 0) {
      int m = (int)((s_wx_anchor + (time_t)clear_b * 900 - now) / 60); if (m < 1) m = 1;
      snprintf(s_data.warn, sizeof(s_data.warn), "DRY IN %dM", m);
    } else {
      snprintf(s_data.warn, sizeof(s_data.warn), "RAIN NEXT 2H");
    }
  } else {
    for (int i = 1; i < WIN_N; i++) {
      if (s_data.precip[i] > 0) {
        int m = (int)((s_wx_anchor + (time_t)(idx + i) * 900 - now) / 60); if (m < 1) m = 1;
        s_data.rain = true;
        snprintf(s_data.warn, sizeof(s_data.warn), "RAIN IN %dM", m);
        break;
      }
    }
  }

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
  if (ok & HealthServiceAccessibilityMaskAvailable)
    s_data.steps = (int)health_service_sum_today(HealthMetricStepCount);
}
static void health_handler(HealthEventType event, void *context) {
  if (event != HealthEventSleepUpdate) {
    refresh_steps();
    if (s_canvas) layer_mark_dirty(s_canvas);
  }
}
#endif

// ---- Weather + settings via AppMessage -------------------------------------
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  bool cfg_changed = false;
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_CELSIUS))) {
    s_cfg.celsius = (t->value->int32 != 0); persist_write_bool(PK_CELSIUS, s_cfg.celsius); cfg_changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_CLOCK24))) {
    s_cfg.clock24 = (t->value->int32 != 0); persist_write_bool(PK_CLOCK24, s_cfg.clock24); cfg_changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_DATEDMY))) {
    s_cfg.date_dmy = (t->value->int32 != 0); persist_write_bool(PK_DATEDMY, s_cfg.date_dmy); cfg_changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_ACCENT))) {
    s_cfg.accent = color_from_int(t->value->int32); s_theme.accent = s_cfg.accent;
    persist_write_int(PK_ACCENT, s_cfg.accent.argb); cfg_changed = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_WARNING))) {
    s_cfg.warning = color_from_int(t->value->int32); s_theme.warning = s_cfg.warning;
    persist_write_int(PK_WARNING, s_cfg.warning.argb); cfg_changed = true; }

  bool wx = false;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_TEMP)))   s_data.temp_f = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_HIGH)))   s_data.high_f = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ICON)))   s_data.icon   = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ANCHOR))) { s_wx_anchor = (time_t)t->value->int32; wx = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_PRECIP))) {
    int n = t->length; if (n > BUF_N) n = BUF_N;
    for (int i = 0; i < n; i++) s_wx_buf[i] = t->value->data[i];
    s_wx_buf_len = n; wx = true;
  }
  if (wx) { s_wx_have = true; s_wx_last_update = time(NULL); recompute_weather(); }

  if (cfg_changed) { time_t now = time(NULL); update_time(localtime(&now)); }
  if (cfg_changed || wx) { if (s_canvas) layer_mark_dirty(s_canvas); }
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

static void load_settings(void) {
  s_cfg.celsius  = persist_exists(PK_CELSIUS)  ? persist_read_bool(PK_CELSIUS)  : false;
  s_cfg.clock24  = persist_exists(PK_CLOCK24)  ? persist_read_bool(PK_CLOCK24)  : clock_is_24h_style();
  s_cfg.date_dmy = persist_exists(PK_DATEDMY)  ? persist_read_bool(PK_DATEDMY)  : false;
  s_cfg.accent   = persist_exists(PK_ACCENT)
      ? (GColor){.argb = (uint8_t)persist_read_int(PK_ACCENT)}  : GColorVividCerulean;
  s_cfg.warning  = persist_exists(PK_WARNING)
      ? (GColor){.argb = (uint8_t)persist_read_int(PK_WARNING)} : GColorYellow;
  s_theme.accent  = s_cfg.accent;
  s_theme.warning = s_cfg.warning;
}

static void prv_init(void) {
  s_theme = (Theme){
    .background     = GColorBlack,
    .text_primary   = GColorWhite,
    .text_secondary = GColorLightGray,
    .accent         = GColorVividCerulean,
    .warning        = GColorYellow,
  };
  psky_ramp(s_precip_ramp);
  load_settings();

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
