#include <pebble.h>
#include "../../../shared/pebblesky_render.h"

// ============================================================================
// PebbleSky Detail — companion app. Opened from the watchface via Quick Launch.
// Three screens cycled with UP/DOWN (BACK exits): 2-hour precip detail, 5-day
// forecast, and now-details. Fetches its own forecast (Open-Meteo) on launch.
// ============================================================================

#define W 200
#define H 228
#define PAD 9
#define BUF_N 16
#define WIN_N 8

// ---- theme ----
static GColor s_bg, s_pri, s_sec, s_acc, s_warn;
static GColor s_ramp[5];

// ---- settings (Clay; persisted; configured separately from the watchface) ----
static bool s_celsius = false;
enum { PK_CELSIUS = 1, PK_ACCENT, PK_WARNING };
static GColor color_from_int(int32_t v) { return GColorFromRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); }
static int disp_temp(int f) { if (!s_celsius) return f; int n = (f - 32) * 5; return (n >= 0) ? (n + 4) / 9 : (n - 4) / 9; }

// ---- received model ----
static int     s_temp = 72, s_high = 78, s_icon = 2, s_feels = 72, s_wind = 0, s_humid = 0;
static time_t  s_anchor = 0, s_last_update = 0;
static uint8_t s_precip[BUF_N]; static int s_precip_len = 0;
static uint8_t s_probs[5];      static int s_probs_len = 0;
static struct { uint8_t icon; int hi; int lo; } s_days[5]; static int s_days_len = 0;
static char    s_loc[24] = "";   // reverse-geocoded place name (GPS)
static bool    s_have = false;

static Window *s_window;
static Layer  *s_canvas;
static int     s_screen = 0;   // 0=precip 1=forecast 2=now

static GFont s_f54, s_f25, s_f18, s_f15, s_f12, s_f10;

static const char *WD[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char *COND[] = {"Clear","Clear","Partly Cloudy","Partly Cloudy","Cloudy",
                             "Fog","Drizzle","Rain","Storm","Snow","Sleet","Windy"};

// Build the visible 8-bucket window from the buffer, anchored to now.
static int window_levels(uint8_t out[WIN_N]) {
  time_t now = time(NULL);
  int idx = s_have ? (int)((now - s_anchor) / 900) : 0;
  if (idx < 0) idx = 0;
  for (int i = 0; i < WIN_N; i++) {
    int b = idx + i;
    out[i] = (s_have && b < s_precip_len) ? s_precip[b] : 0;
  }
  return idx;
}

// ----------------------------------------------------------------------------
// Screen 0: 2-hour precip detail
// ----------------------------------------------------------------------------
static void draw_precip_screen(GContext *ctx) {
  psky_text(ctx, "2-HOUR PRECIP", s_f12, GRect(PAD, 6, W - 2*PAD, 16), s_sec, GTextAlignmentLeft);

  uint8_t win[WIN_N];
  int idx = window_levels(win);

  // Headline (rain countdown / dry), like the face.
  static char head[20];
  time_t now = time(NULL);
  bool rain = false; head[0] = '\0';
  if (win[0] > 0) {
    int clear_b = -1;
    for (int i = 1; i < WIN_N; i++) if (win[i] == 0) { clear_b = idx + i; break; }
    rain = true;
    if (clear_b >= 0) { int m = (int)((s_anchor + (time_t)clear_b*900 - now)/60); if (m<1) m=1;
      snprintf(head, sizeof(head), "DRY IN %dM", m); }
    else snprintf(head, sizeof(head), "RAIN NEXT 2H");
  } else {
    for (int i = 1; i < WIN_N; i++) if (win[i] > 0) {
      int m = (int)((s_anchor + (time_t)(idx+i)*900 - now)/60); if (m<1) m=1;
      rain = true; snprintf(head, sizeof(head), "RAIN IN %dM", m); break; }
  }
  if (!s_have) snprintf(head, sizeof(head), "UPDATING");
  else if (!rain) snprintf(head, sizeof(head), "DRY NEXT 2H");
  psky_text(ctx, head, s_f15, GRect(PAD, 24, W - 2*PAD, 20),
            rain ? s_warn : s_sec, GTextAlignmentLeft);

  // Big bars on an axis.
  GRect bars = GRect(PAD, 52, W - 2*PAD, 84);
  psky_precip_bars(ctx, bars, win, WIN_N, s_ramp, s_acc, rain, 84);

  // Probability row (5 samples), larger for legibility.
  if (s_probs_len >= 5) {
    static char p[8];
    for (int i = 0; i < 5; i++) {
      snprintf(p, sizeof(p), "%d%%", s_probs[i]);
      psky_text(ctx, p, s_f12, GRect(PAD + i*(W-2*PAD)/5 - 8, 146, (W-2*PAD)/5 + 16, 18),
                s_acc, GTextAlignmentCenter);
    }
  }
  // Time axis labels.
  const char *lbl[5] = {"NOW","+30","+60","+90","+120"};
  for (int i = 0; i < 5; i++)
    psky_text(ctx, lbl[i], s_f12, GRect(PAD + i*(W-2*PAD)/5 - 8, 168, (W-2*PAD)/5 + 16, 18),
              s_sec, GTextAlignmentCenter);

  // Intensity scale: 5 ramp swatches (height/color of the bars already convey
  // intensity, so no per-swatch labels — just a low→high color key).
  int sw = 20, gap = 5, sx = (W - 5*sw - 4*gap) / 2;
  for (int i = 0; i < 5; i++) {
    graphics_context_set_fill_color(ctx, s_ramp[i]);
    graphics_fill_rect(ctx, GRect(sx + i*(sw+gap), 198, sw, 12), 2, GCornersAll);
  }
}

// ----------------------------------------------------------------------------
// Screen 1: 5-day forecast
// ----------------------------------------------------------------------------
static void draw_forecast_screen(GContext *ctx) {
  psky_text(ctx, "5-DAY FORECAST", s_f12, GRect(PAD, 6, W - 2*PAD, 16), s_sec, GTextAlignmentLeft);
  if (s_days_len < 1) {
    psky_text(ctx, "...", s_f15, GRect(PAD, 90, W-2*PAD, 24), s_sec, GTextAlignmentCenter);
    return;
  }

  int wmin = 200, wmax = -200;
  for (int i = 0; i < s_days_len; i++) {
    if (s_days[i].lo < wmin) wmin = s_days[i].lo;
    if (s_days[i].hi > wmax) wmax = s_days[i].hi;
  }
  int span = (wmax - wmin); if (span < 1) span = 1;
  time_t now = time(NULL);
  int today_wd = localtime(&now)->tm_wday;

  int y = 30, rh = 36;
  for (int i = 0; i < s_days_len; i++) {
    psky_text(ctx, WD[(today_wd + i) % 7], s_f15, GRect(PAD, y + 7, 40, 20), s_pri, GTextAlignmentLeft);
    psky_icon(ctx, 52, y + 5, 24, s_days[i].icon, s_pri, s_sec, s_acc, s_bg);
    int tx = 78, tw = 46;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(tx, y + 14, tw, 4), 2, GCornersAll);
    int x0 = tx + (s_days[i].lo - wmin) * tw / span;
    int x1 = tx + (s_days[i].hi - wmin) * tw / span;
    int fw = x1 - x0; if (fw < 3) fw = 3;
    graphics_context_set_fill_color(ctx, s_acc);
    graphics_fill_rect(ctx, GRect(x0, y + 14, fw, 4), 2, GCornersAll);
    static char t[6];
    snprintf(t, sizeof(t), "%d°", disp_temp(s_days[i].hi));
    psky_text(ctx, t, s_f15, GRect(128, y + 7, 30, 20), s_pri, GTextAlignmentRight);
    snprintf(t, sizeof(t), "%d°", disp_temp(s_days[i].lo));
    psky_text(ctx, t, s_f15, GRect(160, y + 7, W - PAD - 160, 20), s_sec, GTextAlignmentRight);
    y += rh;
  }
}

// ----------------------------------------------------------------------------
// Screen 2: now details
// ----------------------------------------------------------------------------
static void draw_now_screen(GContext *ctx) {
  psky_text(ctx, s_loc[0] ? s_loc : "NOW", s_f15, GRect(PAD, 4, W - 2*PAD, 20),
            s_sec, GTextAlignmentLeft);
  psky_icon(ctx, PAD, 28, 44, s_have ? s_icon : 4, s_pri, s_sec, s_acc, s_bg);
  static char t[8];
  if (s_have) snprintf(t, sizeof(t), "%d°", disp_temp(s_temp));
  else snprintf(t, sizeof(t), "--°");
  psky_text(ctx, t, s_f54, GRect(PAD + 52, 24, W - PAD - (PAD+52), 56), s_pri, GTextAlignmentLeft);
  psky_text(ctx, s_have ? COND[(s_icon >= 0 && s_icon < 12) ? s_icon : 4] : "Updating", s_f18,
            GRect(PAD, 86, W - 2*PAD, 22), s_pri, GTextAlignmentLeft);

  const char *k[4] = {"FEELS", "WIND", "HUMID", "HIGH"};
  static char v[4][8];
  if (s_have) {
    snprintf(v[0], 8, "%d°", disp_temp(s_feels));
    snprintf(v[1], 8, "%d", s_wind);
    snprintf(v[2], 8, "%d%%", s_humid);
    snprintf(v[3], 8, "%d°", disp_temp(s_high));
  } else {
    for (int i = 0; i < 4; i++) snprintf(v[i], 8, "--");
  }
  for (int i = 0; i < 4; i++) {
    int gx = PAD + (i % 2) * ((W - 2*PAD) / 2);
    int gy = 120 + (i / 2) * 48;
    int gw = (W - 2*PAD) / 2 - 6;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(gx, gy, gw, 1), 0, GCornerNone);
    psky_text(ctx, k[i], s_f10, GRect(gx, gy + 4, gw, 14), s_sec, GTextAlignmentLeft);
    psky_text(ctx, v[i], s_f15, GRect(gx, gy + 18, gw, 22), s_pri, GTextAlignmentLeft);
  }
  static char up[20];
  if (s_last_update) {
    char hm[6]; strftime(hm, sizeof(hm), "%H:%M", localtime(&s_last_update));
    snprintf(up, sizeof(up), "UPDATED %s", hm);
  } else snprintf(up, sizeof(up), "UPDATING");
  psky_text(ctx, up, s_f10, GRect(PAD, 212, W - 2*PAD, 14), s_sec, GTextAlignmentLeft);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_bg);
  graphics_fill_rect(ctx, GRect(0, 0, W, H), 0, GCornerNone);
  switch (s_screen) {
    case 0: draw_precip_screen(ctx); break;
    case 1: draw_forecast_screen(ctx); break;
    default: draw_now_screen(ctx); break;
  }
  for (int i = 0; i < 3; i++) {
    graphics_context_set_fill_color(ctx, i == s_screen ? s_pri : GColorDarkGray);
    graphics_fill_circle(ctx, GPoint(W - 5, 100 + i * 12), 2);
  }
}

// ---- navigation ----
static void next_click(ClickRecognizerRef r, void *c) { s_screen = (s_screen + 1) % 3; layer_mark_dirty(s_canvas); }
static void prev_click(ClickRecognizerRef r, void *c) { s_screen = (s_screen + 2) % 3; layer_mark_dirty(s_canvas); }
static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_DOWN, next_click);
  window_single_click_subscribe(BUTTON_ID_UP, prev_click);
}

// ---- AppMessage ----
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  // --- Settings (Clay) ---
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_CELSIUS))) {
    s_celsius = (t->value->int32 != 0); persist_write_bool(PK_CELSIUS, s_celsius); }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_ACCENT))) {
    s_acc = color_from_int(t->value->int32); persist_write_int(PK_ACCENT, s_acc.argb); }
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_WARNING))) {
    s_warn = color_from_int(t->value->int32); persist_write_int(PK_WARNING, s_warn.argb); }

  // --- Weather ---
  bool wx = false;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_TEMP)))   s_temp  = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_HIGH)))   s_high  = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ICON)))   s_icon  = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_FEELS)))  s_feels = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_WIND)))   s_wind  = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_HUMID)))  s_humid = t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_LOC))) {
    strncpy(s_loc, t->value->cstring, sizeof(s_loc) - 1); s_loc[sizeof(s_loc) - 1] = '\0';
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_ANCHOR))) { s_anchor = (time_t)t->value->int32; wx = true; }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_PRECIP))) {
    int n = t->length; if (n > BUF_N) n = BUF_N;
    for (int i = 0; i < n; i++) s_precip[i] = t->value->data[i];
    s_precip_len = n; wx = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_PROBS))) {
    int n = t->length; if (n > 5) n = 5;
    for (int i = 0; i < n; i++) s_probs[i] = t->value->data[i];
    s_probs_len = n;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_DAILY))) {
    int n = t->length / 3; if (n > 5) n = 5;
    for (int i = 0; i < n; i++) {
      s_days[i].icon = t->value->data[i*3];
      s_days[i].hi   = (int)t->value->data[i*3 + 1] - 50;
      s_days[i].lo   = (int)t->value->data[i*3 + 2] - 50;
    }
    s_days_len = n;
  }
  if (wx) { s_have = true; s_last_update = time(NULL); }
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  s_f54 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_XB_54));
  s_f25 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_XB_25));
  s_f18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_18));
  s_f15 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_15));
  s_f12 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_12));
  s_f10 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_B_10));
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}
static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  fonts_unload_custom_font(s_f54); fonts_unload_custom_font(s_f25);
  fonts_unload_custom_font(s_f18); fonts_unload_custom_font(s_f15);
  fonts_unload_custom_font(s_f12); fonts_unload_custom_font(s_f10);
}

static void load_settings(void) {
  s_celsius = persist_exists(PK_CELSIUS) ? persist_read_bool(PK_CELSIUS) : false;
  s_acc  = persist_exists(PK_ACCENT)  ? (GColor){.argb = (uint8_t)persist_read_int(PK_ACCENT)}  : GColorVividCerulean;
  s_warn = persist_exists(PK_WARNING) ? (GColor){.argb = (uint8_t)persist_read_int(PK_WARNING)} : GColorYellow;
}

static void init(void) {
  s_bg = GColorBlack; s_pri = GColorWhite; s_sec = GColorLightGray;
  s_acc = GColorVividCerulean; s_warn = GColorYellow;
  psky_ramp(s_ramp);
  load_settings();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}
static void deinit(void) { window_destroy(s_window); }

int main(void) { init(); app_event_loop(); deinit(); }
