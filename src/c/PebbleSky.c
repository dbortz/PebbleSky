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
  bool  rain;
  bool  stale;
  char  warn[16];       // e.g. "RAIN IN 20M"
  uint8_t precip[8];    // 0-4 per 15-min bucket
} FaceData;

static FaceData s_data = {
  .battery = 82,
  .temp_f  = 72,
  .high_f  = 78,
  .steps   = 8432,
  .rain    = false,
  .stale   = false,
  .warn    = "",
  .precip  = {0, 0, 1, 0, 0, 1, 0, 0},
};

static Window *s_window;
static Layer  *s_canvas;
static char    s_time_buf[8];
static char    s_date_buf[16];

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

// Demo weather glyph: partly-cloudy-day (small sun upper-left + cloud).
static void draw_weather_icon(GContext *ctx, int ox, int oy) {
  draw_sun(ctx, ox, oy, GPoint(15, 14), 6, 6, s_theme.text_primary);
  draw_cloud(ctx, ox, oy, GPoint(25, 24), 0.92f, s_theme.text_primary);
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
  draw_text(ctx, pct, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
            GRect(sx + shell_w + 5, -1, W - (sx + shell_w + 5) - PAD, 20),
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
  GFont f_time = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont f_temp = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  GFont f_18   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_steps= fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f_14   = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont f_14r  = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Background
  graphics_context_set_fill_color(ctx, s_theme.background);
  graphics_fill_rect(ctx, GRect(0, 0, W, H), 0, GCornerNone);

  // --- Status bar: date (left), battery (right) ---
  draw_text(ctx, s_date_buf, f_14, GRect(PAD, 2, 120, 20),
            s_theme.text_secondary, GTextAlignmentLeft);
  draw_battery(ctx);

  // --- Time (centered) ---
  draw_text(ctx, s_time_buf, f_time, GRect(0, TIME_Y + 12, W, TIME_H),
            s_theme.text_primary, GTextAlignmentCenter);

  // --- Weather row: icon, current temp, high (right) ---
  bool dim = s_data.stale;
  GColor c_primary = dim ? s_theme.text_secondary : s_theme.text_primary;
  draw_weather_icon(ctx, PAD, WROW_Y);
  static char temp[8], high[10];
  snprintf(temp, sizeof(temp), "%d°", s_data.temp_f);
  snprintf(high, sizeof(high), "H %d°", s_data.high_f);
  draw_text(ctx, temp, f_temp, GRect(PAD + 44 + 8, WROW_Y + 4, 90, 40),
            c_primary, GTextAlignmentLeft);
  draw_text(ctx, high, f_18, GRect(W - 9 - 70, WROW_Y + 12, 70, 24),
            s_theme.text_secondary, GTextAlignmentRight);
  if (s_data.stale) { // stale dot at top-right of icon
    graphics_context_set_fill_color(ctx, s_theme.text_secondary);
    graphics_fill_circle(ctx, GPoint(PAD + 40, WROW_Y + 4), 4);
  }

  // --- Bottom zone: steps (left col) | precip (right col) ---
  // Steps caption + count, bottom-aligned.
  draw_footprint(ctx, PAD, 168, s_theme.text_secondary);
  draw_text(ctx, "STEPS", f_14, GRect(PAD + 22, 166, 70, 18),
            s_theme.text_secondary, GTextAlignmentLeft);
  static char steps[12];
  snprintf(steps, sizeof(steps), "%d", s_data.steps);
  // simple thousands separator
  if (s_data.steps >= 1000) {
    snprintf(steps, sizeof(steps), "%d,%03d", s_data.steps / 1000, s_data.steps % 1000);
  }
  draw_text(ctx, steps, f_steps, GRect(PAD, 188, 95, 32),
            s_theme.text_primary, GTextAlignmentLeft);

  // Precip status line + strip, right column.
  const int rcx = 101, rcw = W - 101 - PAD; // 90
  if (s_data.rain) {
    draw_text(ctx, s_data.warn, f_14, GRect(rcx, 166, rcw, 18),
              s_theme.warning, GTextAlignmentLeft);
  } else {
    draw_text(ctx, "No rain · 2h", f_14r, GRect(rcx, 167, rcw, 18),
              s_theme.text_secondary, GTextAlignmentLeft);
  }
  draw_precip_strip(ctx, GRect(rcx, 190, rcw, 26), s_data.rain);
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

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static void prv_window_load(Window *window) {
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
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
