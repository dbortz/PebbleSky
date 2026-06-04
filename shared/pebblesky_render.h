#pragma once
#include <pebble.h>

// ============================================================================
// PebbleSky shared rendering — the 12-glyph weather icon set and the precip
// intensity bars, parameterized by color so both the watchface and the
// companion app draw them identically. Header-only (static inline) so each
// project can #include it without build-system wiring.
//
// TODO: the watchface (src/c/PebbleSky.c) still has its own inline copy of these
// helpers; converge it onto this header in a later refactor.
// ============================================================================

// Fixed 5-step precip intensity ramp: bar height as % of max, by level 0..4.
static const int PSKY_PRECIP_H[5] = {14, 34, 55, 78, 100};

// Build the fixed 5-step precip color ramp (palette-exact). Decoupled from the
// user's accent so rain severity always reads the same.
static inline void psky_ramp(GColor out[5]) {
  out[0] = GColorDarkGray;                  // #555555 none
  out[1] = GColorFromRGB(0x00, 0x55, 0xAA); // #0055AA trace
  out[2] = GColorVividCerulean;             // #00AAFF light
  out[3] = GColorFromRGB(0x55, 0xAA, 0xFF); // #55AAFF moderate
  out[4] = GColorFromRGB(0xAA, 0xFF, 0xFF); // #AAFFFF heavy
}

static inline void psky_text(GContext *ctx, const char *s, GFont f, GRect box,
                             GColor color, GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// ---- icon primitives (authored on a 44 grid, drawn at origin ox,oy) --------
static inline void psky_cloud(GContext *ctx, int ox, int oy, GPoint c, float s, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  int cx = ox + c.x, cy = oy + c.y;
  graphics_fill_circle(ctx, GPoint(cx - (int)(8*s), cy + (int)(1*s)), (int)(8*s));
  graphics_fill_circle(ctx, GPoint(cx + (int)(7*s), cy + (int)(1*s)), (int)(9*s));
  graphics_fill_circle(ctx, GPoint(cx - (int)(1*s), cy - (int)(5*s)), (int)(8*s));
  graphics_fill_rect(ctx, GRect(cx - (int)(14*s), cy, (int)(28*s), (int)(9*s)), (int)(4*s), GCornersBottom);
}

static inline void psky_sun(GContext *ctx, int ox, int oy, GPoint c, int r, int ray, GColor fill) {
  int cx = ox + c.x, cy = oy + c.y;
  graphics_context_set_stroke_color(ctx, fill);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int dx0 = (cos_lookup(a) * (r + 2)) / TRIG_MAX_RATIO;
    int dy0 = (sin_lookup(a) * (r + 2)) / TRIG_MAX_RATIO;
    int dx1 = (cos_lookup(a) * (r + 2 + ray)) / TRIG_MAX_RATIO;
    int dy1 = (sin_lookup(a) * (r + 2 + ray)) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(cx+dx0, cy+dy0), GPoint(cx+dx1, cy+dy1));
  }
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
}

static inline void psky_moon(GContext *ctx, int ox, int oy, GPoint c, int r, GColor fill, GColor bg) {
  int cx = ox + c.x, cy = oy + c.y;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, GPoint(cx + (r*55)/100, cy - (r*38)/100), (r*92)/100);
}

static inline void psky_drop(GContext *ctx, int ox, int oy, int x, int y, int h, GColor fill) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(ox + x - 1, oy + y, 3, h), 1, GCornersAll);
}

static inline void psky_hlines(GContext *ctx, int ox, int oy, const int *yy,
                               const int *x0, const int *x1, int n, GColor fill) {
  graphics_context_set_stroke_color(ctx, fill);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < n; i++)
    graphics_draw_line(ctx, GPoint(ox + x0[i], oy + yy[i]), GPoint(ox + x1[i], oy + yy[i]));
}

// Dispatch one of the 12 glyphs at a target pixel `size` (authored on a 44 grid,
// scaled). pri=main, sec=dim(back cloud), acc=precip, bg=moon bite.
static inline void psky_icon(GContext *ctx, int ox, int oy, int size, int icon,
                             GColor pri, GColor sec, GColor acc, GColor bg) {
#define Z(v) ((v) * size / 44)
  float fs = (float)size / 44.0f;
  int dot = Z(2); if (dot < 1) dot = 1;
  switch (icon) {
    case 0: psky_sun(ctx, ox, oy, GPoint(Z(22), Z(22)), Z(9), Z(8), pri); break;
    case 1: psky_moon(ctx, ox, oy, GPoint(Z(24), Z(22)), Z(14), pri, bg); break;
    case 2: psky_sun(ctx, ox, oy, GPoint(Z(15), Z(14)), Z(6), Z(6), pri);
            psky_cloud(ctx, ox, oy, GPoint(Z(25), Z(24)), 0.92f*fs, pri); break;
    case 3: psky_moon(ctx, ox, oy, GPoint(Z(15), Z(14)), Z(9), pri, bg);
            psky_cloud(ctx, ox, oy, GPoint(Z(25), Z(24)), 0.92f*fs, pri); break;
    case 4: psky_cloud(ctx, ox, oy, GPoint(Z(15), Z(15)), 0.72f*fs, sec);
            psky_cloud(ctx, ox, oy, GPoint(Z(24), Z(25)), 1.0f*fs, pri); break;
    case 5: { psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(16)), 0.9f*fs, pri);
              int yy[3]={Z(32),Z(37),Z(42)}, a[3]={Z(11),Z(9),Z(13)}, b[3]={Z(33),Z(35),Z(31)};
              psky_hlines(ctx, ox, oy, yy, a, b, 3, pri); } break;
    case 6: psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(16)), 0.96f*fs, pri);
            psky_drop(ctx, ox, oy, Z(15), Z(33), Z(6), acc);
            psky_drop(ctx, ox, oy, Z(22), Z(35), Z(6), acc);
            psky_drop(ctx, ox, oy, Z(29), Z(33), Z(6), acc); break;
    case 7: psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(15)), 0.98f*fs, pri);
            psky_drop(ctx, ox, oy, Z(14), Z(33), Z(9), acc);
            psky_drop(ctx, ox, oy, Z(22), Z(35), Z(9), acc);
            psky_drop(ctx, ox, oy, Z(30), Z(33), Z(9), acc); break;
    case 8: { psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(14)), 0.98f*fs, pri);
              psky_drop(ctx, ox, oy, Z(14), Z(33), Z(7), pri);
              graphics_context_set_stroke_color(ctx, acc);
              graphics_context_set_stroke_width(ctx, size >= 36 ? 3 : 2);
              graphics_draw_line(ctx, GPoint(ox+Z(27), oy+Z(29)), GPoint(ox+Z(22), oy+Z(35)));
              graphics_draw_line(ctx, GPoint(ox+Z(22), oy+Z(35)), GPoint(ox+Z(27), oy+Z(35)));
              graphics_draw_line(ctx, GPoint(ox+Z(27), oy+Z(35)), GPoint(ox+Z(22), oy+Z(42))); } break;
    case 9: psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(15)), 0.98f*fs, pri);
            graphics_context_set_fill_color(ctx, pri);
            graphics_fill_circle(ctx, GPoint(ox+Z(15), oy+Z(35)), dot);
            graphics_fill_circle(ctx, GPoint(ox+Z(23), oy+Z(38)), dot);
            graphics_fill_circle(ctx, GPoint(ox+Z(31), oy+Z(35)), dot); break;
    case 10: psky_cloud(ctx, ox, oy, GPoint(Z(22), Z(15)), 0.98f*fs, pri);
             psky_drop(ctx, ox, oy, Z(15), Z(33), Z(8), acc);
             graphics_context_set_fill_color(ctx, pri);
             graphics_fill_circle(ctx, GPoint(ox+Z(24), oy+Z(37)), dot);
             psky_drop(ctx, ox, oy, Z(31), Z(33), Z(8), acc); break;
    case 11: { int yy[3]={Z(17),Z(24),Z(31)}, a[3]={Z(8),Z(8),Z(8)}, b[3]={Z(28),Z(34),Z(24)};
               psky_hlines(ctx, ox, oy, yy, a, b, 3, pri); } break;
    default: psky_cloud(ctx, ox, oy, GPoint(Z(24), Z(22)), 1.0f*fs, pri); break;
  }
#undef Z
}

// Precip bars sitting on a 2px axis. levels[n]=0..4; ramp[5] colors; accent caps
// the "now" bar when mark_now.
static inline void psky_precip_bars(GContext *ctx, GRect area, const uint8_t *levels, int n,
                                    const GColor *ramp, GColor accent, bool mark_now, int max_h) {
  int gap = 3;
  int bw = (area.size.w - gap * (n - 1)) / n;
  int base_y = area.origin.y + area.size.h;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(area.origin.x, base_y, area.size.w, 2), 0, GCornerNone);
  for (int i = 0; i < n; i++) {
    int lv = levels[i]; if (lv > 4) lv = 4;
    int h = (PSKY_PRECIP_H[lv] * max_h) / 100; if (h < 2) h = 2;
    int x = area.origin.x + i * (bw + gap);
    graphics_context_set_fill_color(ctx, ramp[lv]);
    graphics_fill_rect(ctx, GRect(x, base_y - h, bw, h), 0, GCornerNone);
    if (mark_now && i == 0) {
      graphics_context_set_fill_color(ctx, accent);
      graphics_fill_rect(ctx, GRect(x, base_y - h, bw, 3), 0, GCornerNone);
    }
  }
}
