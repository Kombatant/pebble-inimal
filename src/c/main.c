// =============================================================================
// Pebble-inimal watchface for Pebble Time 2 (Emery, 200x228)
// =============================================================================
// New with 0.2: Settings! Set night mode and weather update interval
//
// Shows:
//   - Weather icons (drawn with primitives)
//   - Date row "18 Fri.  |  24°C" with decorative arrows
//   - Stats row: STEPS / Heart Rate / KM (Pebble Health)
//   - Battery bar at the bottom with battery percentage text on top
// =============================================================================

#include <pebble.h>
#include <ctype.h>

// ---------- Layout constants (Emery is 200 x 228) -----------------------------
// Vertical rhythm: each section sits a few px below the one above so the face
// never feels glued together.
//   ticks    : 0  .. 7
//   weather  : ~12 .. 44   (centered at WEATHER_ICON_CY=30)
//   date     : 50 .. 72    (frame 22, font 18)
//   divider  : 74
//   time     : 78 .. 136   (frame 58, font 42)
//   stats lbl: 140 .. 156  (pill 16 tall)
//   stats val: 160 .. 182  (frame 22, font 20)
//   battery %: 184 .. 206  (frame 22, font 18)
//   battery bar: 210 .. 216
//   ticks    : 220 .. 228
#define WEATHER_ICON_CY  30
#define DATE_ROW_Y       50
#define DIVIDER_Y        74
#define TIME_Y           78
#define STATS_LABEL_Y    140
#define STATS_VALUE_Y    160
#define BATTERY_TEXT_Y   184

// Battery bar geometry — a thin horizontal pill. Replaces the bottom arc.
#define BAR_Y            210
#define BAR_HEIGHT       6
#define BAR_WIDTH        160     // centered: x = (200-160)/2 = 20

// Larger mode layout, scaled to Emery's 200x228 display.
// One explicit grid keeps the mockup-like proportions from drifting.
#define LARGE_INSET             14
#define LARGE_CONTENT_LEFT      16
#define LARGE_CONTENT_RIGHT     184
#define LARGE_TOP_Y             10
#define LARGE_TOP_H             30
#define LARGE_DIVIDER_Y         44
#define LARGE_TIME_Y            40
#define LARGE_TIME_H            106
#define LARGE_TIME_X            06
#define LARGE_TIME_W            154
#define LARGE_STATUS_DIVIDER_X  158
#define LARGE_STATUS_DIVIDER_TOP 52
#define LARGE_STATUS_DIVIDER_BOTTOM 122
#define LARGE_STATUS_CENTER_X   176
#define LARGE_SECONDS_Y         60
#define LARGE_STATUS_ICON_Y     90
#define LARGE_STATS_Y           132
#define LARGE_STATS_H           44
#define LARGE_STAT_CENTER_Y     (LARGE_STATS_Y + LARGE_STATS_H / 2)
#define LARGE_CARD_W            82
#define LARGE_CARD_GAP          8
#define LARGE_LEFT_CARD_X       LARGE_INSET
#define LARGE_RIGHT_CARD_X      (LARGE_LEFT_CARD_X + LARGE_CARD_W + LARGE_CARD_GAP)
#define LARGE_BATTERY_Y         184
#define LARGE_BATTERY_BAR_Y     198
#define LARGE_BATTERY_BAR_H     8

// ---------- Globals -----------------------------------------------------------
static Window *s_main_window;
static Layer *s_canvas_layer;

static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_steps_label_layer;
static TextLayer *s_km_label_layer;
static TextLayer *s_steps_value_layer;
static TextLayer *s_hr_value_layer;
static TextLayer *s_dist_value_layer;
static TextLayer *s_temp_layer;
static TextLayer *s_seconds_layer;
static TextLayer *s_battery_value_layer;

// Custom fonts (Atkinson Hyperlegible, loaded from resources)
static GFont s_font_time;
static GFont s_font_time_large;
static GFont s_font_top;
static GFont s_font_label;
static GFont s_font_stat;
static GFont s_font_stat_large;
static GFont s_font_small;

// Bitmaps loaded from resources
static GBitmap *s_battery_full_bitmap;
static GBitmap *s_battery_charging_bitmap;
static GBitmap *s_battery_75_bitmap;
static GBitmap *s_battery_50_bitmap;
static GBitmap *s_battery_25_bitmap;
static GBitmap *s_bt_on_bitmap;
static GBitmap *s_bt_off_bitmap;
static GBitmap *s_steps_bitmap;

// Bluetooth connection state
static bool s_bt_connected = true;

// Buffers (must remain valid as long as TextLayer references them)
static char s_time_buffer[12];   // "HH:MM:SS" + null
static char s_date_buffer[32];
static char s_seconds_buffer[4];
static char s_temp_buffer[8];
static char s_steps_buffer[12];
static char s_hr_buffer[12];
static char s_dist_buffer[16];
static char s_battery_text_buffer[8];
static int s_last_hr_bpm = 0;

// Cached time components used by the unified time renderer.
// s_displayed_seconds is the value currently painted on the watch — it's
// updated by the second-tick handler while seconds are active, and kept
// frozen between active windows so the user sees the last value at flick-off.
static int s_displayed_hour    = 0;
static int s_displayed_min     = 0;
static int s_displayed_seconds = 0;

// On-demand seconds: when the user flicks the wrist (accel tap), update
// seconds for a short window roughly matching the backlight duration, then
// freeze the displayed value until the next minute change.
static bool s_seconds_active = false;
static AppTimer *s_seconds_timeout_timer = NULL;
static AppTimer *s_seconds_tick_timer = NULL;
static AppTimer *s_deferred_refresh_timer = NULL;
#define SECONDS_DURATION_MS 3500
#define SECONDS_POLL_MS 200

// State
static int s_battery_level = 100;
static bool s_battery_is_charging = false;
static int s_temp_c = 0;
static bool s_temp_known = false;
static int s_weather_code = -1;   // -1 = unknown, otherwise WMO code from Open-Meteo

// Configuration (mirrored on the phone via the settings page)
static bool s_night_mode_enabled  = false;   // default: OFF (user opts in)
static int  s_night_start_hour    = 0;       // 00:00 = midnight
static int  s_night_end_hour      = 6;       // 06:00 = 6 AM
static int  s_weather_interval_min = 30;

typedef enum {
    FaceModeMinimal = 0,
    FaceModeLarger  = 1
} FaceMode;
static FaceMode s_face_mode = FaceModeLarger;

// Tracking state for the night-idle gate and the weather refresh schedule
static time_t s_last_tap_time      = 0;
static time_t s_last_weather_fetch = 0;

// Persistent storage keys
#define PERSIST_KEY_TEMP             100
#define PERSIST_KEY_TEMP_KNOWN       101
#define PERSIST_KEY_WEATHER          102
#define PERSIST_KEY_NIGHT_MODE       103
#define PERSIST_KEY_WEATHER_INT      104
#define PERSIST_KEY_NIGHT_START      105
#define PERSIST_KEY_NIGHT_END        106
#define PERSIST_KEY_FACE_MODE        107
#define PERSIST_KEY_BAT_LAST_PCT     108
#define PERSIST_KEY_BAT_LAST_TS      109
#define PERSIST_KEY_BAT_EWMA_MILLI   110
#define PERSIST_KEY_BAT_EWMA_INIT    111

// Battery life estimator state
// EWMA stored as %/hour × 1000 (fixed-point) to avoid float in persist.
// Sample acceptance: dt_hours in [0.05, 12.0] and dpct > 0.
static uint8_t  s_bat_last_pct      = 0;
static time_t   s_bat_last_ts       = 0;
static int32_t  s_bat_ewma_milli    = 0;     // 0 == uninitialized
static bool     s_bat_ewma_init     = false;

// =============================================================================
// Drawing helpers
// =============================================================================

// --- Weather icons -----------------------------------------------------------
// Each icon is centered at (cx, cy) and fits roughly within a 40x32 bounding
// box. draw_weather_icon() picks one based on a WMO weather code.

static void draw_sun_only(GContext *ctx, int cx, int cy) {
    GColor sun = COLOR_FALLBACK(GColorYellow, GColorWhite);
    graphics_context_set_fill_color(ctx, sun);
    graphics_fill_circle(ctx, GPoint(cx, cy), 8);

    graphics_context_set_stroke_color(ctx, sun);
    graphics_context_set_stroke_width(ctx, 2);
    int ri = 11, ro = 16;
    int di = (ri * 707) / 1000;   // diagonal ≈ ri / sqrt(2)
    int doo = (ro * 707) / 1000;
    // Cardinals
    graphics_draw_line(ctx, GPoint(cx,      cy - ri), GPoint(cx,      cy - ro));
    graphics_draw_line(ctx, GPoint(cx,      cy + ri), GPoint(cx,      cy + ro));
    graphics_draw_line(ctx, GPoint(cx - ri, cy),      GPoint(cx - ro, cy));
    graphics_draw_line(ctx, GPoint(cx + ri, cy),      GPoint(cx + ro, cy));
    // Diagonals
    graphics_draw_line(ctx, GPoint(cx + di, cy + di), GPoint(cx + doo, cy + doo));
    graphics_draw_line(ctx, GPoint(cx - di, cy + di), GPoint(cx - doo, cy + doo));
    graphics_draw_line(ctx, GPoint(cx + di, cy - di), GPoint(cx + doo, cy - doo));
    graphics_draw_line(ctx, GPoint(cx - di, cy - di), GPoint(cx - doo, cy - doo));
    graphics_context_set_stroke_width(ctx, 1);
}

// Sun + cloud (mainly clear / partly cloudy)
static void draw_sun_cloud(GContext *ctx, int cx, int cy) {
    graphics_context_set_fill_color(ctx, COLOR_FALLBACK(GColorYellow, GColorWhite));
    graphics_fill_circle(ctx, GPoint(cx + 11, cy - 9), 9);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(cx - 14, cy + 4), 9);
    graphics_fill_circle(ctx, GPoint(cx,      cy - 1), 12);
    graphics_fill_circle(ctx, GPoint(cx + 13, cy + 5), 10);
    graphics_fill_rect  (ctx, GRect(cx - 19, cy + 3, 38, 9), 4, GCornersBottom);
}

// Plain cloud (overcast / fog)
static void draw_cloud_only(GContext *ctx, int cx, int cy, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx - 12, cy + 2), 9);
    graphics_fill_circle(ctx, GPoint(cx,      cy - 4), 12);
    graphics_fill_circle(ctx, GPoint(cx + 13, cy + 3), 10);
    graphics_fill_rect  (ctx, GRect(cx - 18, cy + 2, 38, 9), 4, GCornersBottom);
}

static void draw_cloud_only_small(GContext *ctx, int cx, int cy, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx - 9, cy + 2), 7);
    graphics_fill_circle(ctx, GPoint(cx,     cy - 3), 9);
    graphics_fill_circle(ctx, GPoint(cx + 10, cy + 2), 7);
    graphics_fill_rect(ctx, GRect(cx - 14, cy + 2, 29, 7), 3, GCornersBottom);
}

static void draw_rain(GContext *ctx, int cx, int cy) {
    draw_cloud_only(ctx, cx, cy - 4, GColorWhite);
    graphics_context_set_stroke_color(ctx, COLOR_FALLBACK(GColorVividCerulean, GColorWhite));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(cx - 9, cy + 11), GPoint(cx - 11, cy + 16));
    graphics_draw_line(ctx, GPoint(cx,     cy + 12), GPoint(cx -  2, cy + 17));
    graphics_draw_line(ctx, GPoint(cx + 9, cy + 11), GPoint(cx +  7, cy + 16));
    graphics_context_set_stroke_width(ctx, 1);
}

static void draw_snow(GContext *ctx, int cx, int cy) {
    draw_cloud_only(ctx, cx, cy - 4, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(cx - 9, cy + 14), 2);
    graphics_fill_circle(ctx, GPoint(cx,     cy + 16), 2);
    graphics_fill_circle(ctx, GPoint(cx + 9, cy + 14), 2);
}

static void draw_thunderstorm(GContext *ctx, int cx, int cy) {
    draw_cloud_only(ctx, cx, cy - 4, COLOR_FALLBACK(GColorLightGray, GColorWhite));
    GPathInfo bolt = {
        .num_points = 5,
        .points = (GPoint[]) {
            { (int16_t)(cx + 1), (int16_t)(cy + 7)  },
            { (int16_t)(cx - 5), (int16_t)(cy + 14) },
            { (int16_t)(cx),     (int16_t)(cy + 14) },
            { (int16_t)(cx - 4), (int16_t)(cy + 19) },
            { (int16_t)(cx + 5), (int16_t)(cy + 11) }
        }
    };
    GPath *p = gpath_create(&bolt);
    if (p) {
        graphics_context_set_fill_color(ctx, COLOR_FALLBACK(GColorYellow, GColorWhite));
        gpath_draw_filled(ctx, p);
        gpath_destroy(p);
    }
}

// WMO weather code dispatcher (codes from Open-Meteo).
// 0 clear · 1-3 partly cloudy/overcast · 45-48 fog · 51-67 drizzle/rain
// 71-77 snow · 80-82 rain showers · 85-86 snow showers · 95-99 thunderstorm
static void draw_weather_icon(GContext *ctx, int cx, int cy, int code) {
    if (code < 0)             { draw_sun_cloud(ctx, cx, cy); return; }
    if (code == 0)            { draw_sun_only(ctx, cx, cy); return; }
    if (code <= 2)            { draw_sun_cloud(ctx, cx, cy); return; }
    if (code == 3)            { draw_cloud_only(ctx, cx, cy, GColorWhite); return; }
    if (code >= 45 && code <= 48) {
        draw_cloud_only(ctx, cx, cy, COLOR_FALLBACK(GColorLightGray, GColorWhite)); return;
    }
    if (code >= 51 && code <= 67) { draw_rain(ctx, cx, cy); return; }
    if (code >= 71 && code <= 77) { draw_snow(ctx, cx, cy); return; }
    if (code >= 80 && code <= 82) { draw_rain(ctx, cx, cy); return; }
    if (code >= 85 && code <= 86) { draw_snow(ctx, cx, cy); return; }
    if (code >= 95)               { draw_thunderstorm(ctx, cx, cy); return; }
    // Anything else: fall back to partly cloudy
    draw_sun_cloud(ctx, cx, cy);
}

static void draw_sun_only_small(GContext *ctx, int cx, int cy) {
    GColor sun = COLOR_FALLBACK(GColorYellow, GColorWhite);
    graphics_context_set_fill_color(ctx, sun);
    graphics_fill_circle(ctx, GPoint(cx, cy), 6);

    graphics_context_set_stroke_color(ctx, sun);
    graphics_context_set_stroke_width(ctx, 2);
    int ri = 8, ro = 11;
    int di = (ri * 707) / 1000;
    int doo = (ro * 707) / 1000;
    graphics_draw_line(ctx, GPoint(cx,      cy - ri), GPoint(cx,      cy - ro));
    graphics_draw_line(ctx, GPoint(cx,      cy + ri), GPoint(cx,      cy + ro));
    graphics_draw_line(ctx, GPoint(cx - ri, cy),      GPoint(cx - ro, cy));
    graphics_draw_line(ctx, GPoint(cx + ri, cy),      GPoint(cx + ro, cy));
    graphics_draw_line(ctx, GPoint(cx + di, cy + di), GPoint(cx + doo, cy + doo));
    graphics_draw_line(ctx, GPoint(cx - di, cy + di), GPoint(cx - doo, cy + doo));
    graphics_draw_line(ctx, GPoint(cx + di, cy - di), GPoint(cx + doo, cy - doo));
    graphics_draw_line(ctx, GPoint(cx - di, cy - di), GPoint(cx - doo, cy - doo));
    graphics_context_set_stroke_width(ctx, 1);
}

static void draw_sun_cloud_small(GContext *ctx, int cx, int cy) {
    graphics_context_set_fill_color(ctx, COLOR_FALLBACK(GColorYellow, GColorWhite));
    graphics_fill_circle(ctx, GPoint(cx + 8, cy - 7), 6);

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(cx - 10, cy + 3), 7);
    graphics_fill_circle(ctx, GPoint(cx,       cy - 1), 9);
    graphics_fill_circle(ctx, GPoint(cx + 10,  cy + 3), 7);
    graphics_fill_rect  (ctx, GRect(cx - 14, cy + 2, 29, 7), 3, GCornersBottom);
}

static void draw_rain_small(GContext *ctx, int cx, int cy) {
    draw_cloud_only_small(ctx, cx, cy - 3, GColorWhite);
    graphics_context_set_stroke_color(ctx, COLOR_FALLBACK(GColorVividCerulean, GColorWhite));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(cx - 7, cy + 9),  GPoint(cx - 9, cy + 13));
    graphics_draw_line(ctx, GPoint(cx,     cy + 10), GPoint(cx - 2, cy + 14));
    graphics_draw_line(ctx, GPoint(cx + 7, cy + 9),  GPoint(cx + 5, cy + 13));
    graphics_context_set_stroke_width(ctx, 1);
}

static void draw_snow_small(GContext *ctx, int cx, int cy) {
    draw_cloud_only_small(ctx, cx, cy - 3, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(cx - 7, cy + 11), 2);
    graphics_fill_circle(ctx, GPoint(cx,     cy + 13), 2);
    graphics_fill_circle(ctx, GPoint(cx + 7, cy + 11), 2);
}

static void draw_thunderstorm_small(GContext *ctx, int cx, int cy) {
    draw_cloud_only_small(ctx, cx, cy - 3, COLOR_FALLBACK(GColorLightGray, GColorWhite));
    GPathInfo bolt = {
        .num_points = 5,
        .points = (GPoint[]) {
            { (int16_t)(cx + 1), (int16_t)(cy + 6)  },
            { (int16_t)(cx - 4), (int16_t)(cy + 11) },
            { (int16_t)(cx),     (int16_t)(cy + 11) },
            { (int16_t)(cx - 3), (int16_t)(cy + 15) },
            { (int16_t)(cx + 4), (int16_t)(cy + 9)  }
        }
    };
    GPath *p = gpath_create(&bolt);
    if (p) {
        graphics_context_set_fill_color(ctx, COLOR_FALLBACK(GColorYellow, GColorWhite));
        gpath_draw_filled(ctx, p);
        gpath_destroy(p);
    }
}

static void draw_weather_icon_small(GContext *ctx, int cx, int cy, int code) {
    if (code < 0)             { draw_sun_cloud_small(ctx, cx, cy); return; }
    if (code == 0)            { draw_sun_only_small(ctx, cx, cy); return; }
    if (code <= 2)            { draw_sun_cloud_small(ctx, cx, cy); return; }
    if (code == 3)            { draw_cloud_only_small(ctx, cx, cy, GColorWhite); return; }
    if (code >= 45 && code <= 48) {
        draw_cloud_only_small(ctx, cx, cy, COLOR_FALLBACK(GColorLightGray, GColorWhite)); return;
    }
    if (code >= 51 && code <= 67) { draw_rain_small(ctx, cx, cy); return; }
    if (code >= 71 && code <= 77) { draw_snow_small(ctx, cx, cy); return; }
    if (code >= 80 && code <= 82) { draw_rain_small(ctx, cx, cy); return; }
    if (code >= 85 && code <= 86) { draw_snow_small(ctx, cx, cy); return; }
    if (code >= 95)               { draw_thunderstorm_small(ctx, cx, cy); return; }
    draw_sun_cloud_small(ctx, cx, cy);
}

static void draw_heart(GContext *ctx, int cx, int cy, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx - 4, cy - 2), 4);
    graphics_fill_circle(ctx, GPoint(cx + 4, cy - 2), 4);
    GPathInfo info = {
        .num_points = 3,
        .points = (GPoint[]) {
            { (int16_t)(cx - 8), (int16_t)(cy - 1) },
            { (int16_t)(cx + 8), (int16_t)(cy - 1) },
            { (int16_t)(cx),     (int16_t)(cy + 8) }
        }
    };
    GPath *p = gpath_create(&info);
    if (p) { gpath_draw_filled(ctx, p); gpath_destroy(p); }
}

// Heart icon sized to match 24x24 step icon bbox.
// Lobes radius 6, centers at (cx±6, cy-4); triangle tip at cy+14.
static void draw_heart_large(GContext *ctx, int cx, int cy, GColor color) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_circle(ctx, GPoint(cx - 6, cy - 4), 6);
    graphics_fill_circle(ctx, GPoint(cx + 6, cy - 4), 6);
    GPathInfo info = {
        .num_points = 3,
        .points = (GPoint[]) {
            { (int16_t)(cx - 12), (int16_t)(cy - 2) },
            { (int16_t)(cx + 12), (int16_t)(cy - 2) },
            { (int16_t)cx,        (int16_t)(cy + 14) }
        }
    };
    GPath *p = gpath_create(&info);
    if (p) { gpath_draw_filled(ctx, p); gpath_destroy(p); }
}

static void draw_arrow(GContext *ctx, int x, int y, bool point_left) {
    int dx = point_left ? -6 : 6;
    GPathInfo info = {
        .num_points = 3,
        .points = (GPoint[]) {
            { (int16_t)x,       (int16_t)(y - 4) },
            { (int16_t)x,       (int16_t)(y + 4) },
            { (int16_t)(x + dx),(int16_t)y }
        }
    };
    GPath *p = gpath_create(&info);
    if (p) { gpath_draw_filled(ctx, p); gpath_destroy(p); }
}

// Draw 60 tick marks around the rectangular perimeter, every 5th tick longer
// & brighter to mark hour positions.
static void draw_tick_marks(GContext *ctx, int W, int H) {
    int perim = 2 * (W + H);
    int half_top = W / 2;

    for (int i = 0; i < 60; i++) {
        int p = (perim * i) / 60;
        bool is_hour = (i % 5 == 0);
        int len     = is_hour ? 7 : 3;
        GColor color = is_hour ? GColorWhite : GColorDarkGray;

        graphics_context_set_stroke_color(ctx, color);
        graphics_context_set_stroke_width(ctx, is_hour ? 2 : 1);

        int x0, y0, x1, y1;

        if (p < half_top) {
            // top edge, right half — tick points down
            x0 = half_top + p; y0 = 0;
            x1 = x0;           y1 = len;
        } else if (p < half_top + H) {
            // right edge — tick points left
            x0 = W - 1;       y0 = p - half_top;
            x1 = W - 1 - len; y1 = y0;
        } else if (p < half_top + H + W) {
            // bottom edge — tick points up
            x0 = W - 1 - (p - half_top - H); y0 = H - 1;
            x1 = x0;                          y1 = H - 1 - len;
        } else if (p < half_top + H + W + H) {
            // left edge — tick points right
            x0 = 0;   y0 = H - 1 - (p - half_top - H - W);
            x1 = len; y1 = y0;
        } else {
            // top edge, left half — tick points down
            x0 = p - (half_top + H + W + H); y0 = 0;
            x1 = x0;                          y1 = len;
        }
        graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x1, y1));
    }

    // Reset stroke width so other drawing isn't affected
    graphics_context_set_stroke_width(ctx, 1);
}

static GBitmap *get_battery_bitmap(void) {
    if (s_battery_is_charging && s_battery_charging_bitmap) {
        return s_battery_charging_bitmap;
    }
    if (s_battery_level > 75 && s_battery_full_bitmap) {
        return s_battery_full_bitmap;
    }
    if (s_battery_level > 50 && s_battery_75_bitmap) {
        return s_battery_75_bitmap;
    }
    if (s_battery_level > 25 && s_battery_50_bitmap) {
        return s_battery_50_bitmap;
    }
    return s_battery_25_bitmap;
}

// =============================================================================
// Canvas update procedure
// =============================================================================
static void draw_larger_canvas(GContext *ctx, int W, int H) {
    GColor dim = GColorDarkGray;
    GColor red = COLOR_FALLBACK(GColorMelon, GColorWhite);
    GColor green = COLOR_FALLBACK(GColorScreaminGreen, GColorWhite);

    draw_tick_marks(ctx, W, H);
    draw_weather_icon_small(ctx, 25, 24, s_weather_code);

    graphics_context_set_stroke_color(ctx, dim);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(LARGE_CONTENT_LEFT, LARGE_DIVIDER_Y),
                       GPoint(LARGE_CONTENT_RIGHT, LARGE_DIVIDER_Y));

    graphics_context_set_stroke_color(ctx, dim);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(LARGE_STATUS_DIVIDER_X, LARGE_STATUS_DIVIDER_TOP),
                       GPoint(LARGE_STATUS_DIVIDER_X, LARGE_STATUS_DIVIDER_BOTTOM));

    graphics_draw_round_rect(ctx, GRect(LARGE_LEFT_CARD_X, LARGE_STATS_Y,
                                        LARGE_CARD_W, LARGE_STATS_H), 6);
    graphics_draw_round_rect(ctx, GRect(LARGE_RIGHT_CARD_X, LARGE_STATS_Y,
                                        LARGE_CARD_W, LARGE_STATS_H), 6);
    graphics_context_set_stroke_width(ctx, 1);

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    if (s_steps_bitmap) {
        graphics_draw_bitmap_in_rect(ctx, s_steps_bitmap,
                                     GRect(LARGE_LEFT_CARD_X + 3, LARGE_STAT_CENTER_Y - 12, 24, 24));
    }

    draw_heart_large(ctx, LARGE_RIGHT_CARD_X + 20, LARGE_STAT_CENTER_Y, red);

    GBitmap *bt = s_bt_connected ? s_bt_on_bitmap : s_bt_off_bitmap;
    if (bt) {
        graphics_draw_bitmap_in_rect(ctx, bt,
                                     GRect(LARGE_STATUS_CENTER_X - 12, LARGE_STATUS_ICON_Y, 24, 24));
    }

    GBitmap *battery = get_battery_bitmap();
    if (battery) {
        graphics_draw_bitmap_in_rect(ctx, battery,
                                     GRect(LARGE_LEFT_CARD_X + 8,
                                           LARGE_BATTERY_BAR_Y + LARGE_BATTERY_BAR_H / 2 - 12,
                                           24, 24));
    }

    const int bar_x = LARGE_RIGHT_CARD_X;
    const int bar_w = LARGE_CARD_W;
    const int bar_h = LARGE_BATTERY_BAR_H;
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(bar_x, LARGE_BATTERY_BAR_Y, bar_w, bar_h),
                       bar_h / 2, GCornersAll);

    int fill_w = (bar_w * s_battery_level) / 100;
    if (s_battery_level > 0 && fill_w < bar_h) fill_w = bar_h;
    if (fill_w > 0) {
        graphics_context_set_fill_color(ctx,
            s_battery_level <= 20 ? red : green);
        graphics_fill_rect(ctx, GRect(bar_x, LARGE_BATTERY_BAR_Y, fill_w, bar_h),
                           bar_h / 2, GCornersAll);
    }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    const int W = bounds.size.w;
    const int H = bounds.size.h;

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    if (s_face_mode == FaceModeLarger) {
        draw_larger_canvas(ctx, W, H);
        return;
    }

    // 0. Tick marks around the perimeter (drawn first, others paint on top)
    draw_tick_marks(ctx, W, H);

    // 1. Weather icon at the top (chosen from the WMO code)
    draw_weather_icon(ctx, W / 2, WEATHER_ICON_CY, s_weather_code);

    // 2. Decorative arrows around the date row
    graphics_context_set_fill_color(ctx, GColorWhite);
    draw_arrow(ctx, 15,     DATE_ROW_Y + 10, true);
    draw_arrow(ctx, W - 15, DATE_ROW_Y + 10, false);

    // 3. Horizontal divider under the date row
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_line(ctx, GPoint(20, DIVIDER_Y), GPoint(W - 20, DIVIDER_Y));

    // 4. Pill backgrounds behind STEPS / KM labels (60-wide columns centered at x=40 and x=160)
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(10,     STATS_LABEL_Y, 60, 16), 8, GCornersAll);
    graphics_fill_rect(ctx, GRect(W - 70, STATS_LABEL_Y, 60, 16), 8, GCornersAll);

    // 5. Heart icon between the two pills
    draw_heart(ctx, W / 2, STATS_LABEL_Y + 8, GColorWhite);

    // 6. Battery bar: a thin centered horizontal pill. Track in dark gray,
    //    fill in white (or red if low) from the left, proportional to %.
    int bar_x = (W - BAR_WIDTH) / 2;
    int corner_radius = BAR_HEIGHT / 2;

    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(bar_x, BAR_Y, BAR_WIDTH, BAR_HEIGHT),
                       corner_radius, GCornersAll);

    if (s_battery_level > 0) {
        int fill_w = (BAR_WIDTH * s_battery_level) / 100;
        if (fill_w < BAR_HEIGHT) fill_w = BAR_HEIGHT;   // keep the pill cap visible
        GColor fill_color = (s_battery_level <= 20)
            ? COLOR_FALLBACK(GColorRed, GColorWhite)
            : GColorWhite;
        graphics_context_set_fill_color(ctx, fill_color);
        graphics_fill_rect(ctx, GRect(bar_x, BAR_Y, fill_w, BAR_HEIGHT),
                           corner_radius, GCornersAll);
    }

    // 7. Bottom info row:
    //    - Battery (bolt icon + percentage) left-aligned
    //    - Bluetooth status icon right-aligned
    {
        const int icon_w = 24;
        const int icon_h = 24;
        const int gap    = 6;
        const int margin = 20;
        const int icon_y = BATTERY_TEXT_Y + 11 - icon_h / 2;

        graphics_context_set_compositing_mode(ctx, GCompOpSet);

        // Battery icon (left)
        GBitmap *battery = get_battery_bitmap();
        if (battery) {
            graphics_draw_bitmap_in_rect(ctx, battery,
                GRect(margin, icon_y, icon_w, icon_h));
        }

        // Battery percentage text right of icon
        if (s_battery_text_buffer[0]) {
            GSize text_size = graphics_text_layout_get_content_size(
                s_battery_text_buffer,
                s_font_label,
                GRect(0, 0, W, 22),
                GTextOverflowModeWordWrap,
                GTextAlignmentLeft);

            graphics_context_set_text_color(ctx,
                COLOR_FALLBACK(GColorYellow, GColorWhite));
            graphics_draw_text(ctx,
                s_battery_text_buffer,
                s_font_label,
                GRect(margin + icon_w + gap, BATTERY_TEXT_Y,
                      text_size.w + 4, 22),
                GTextOverflowModeWordWrap,
                GTextAlignmentLeft,
                NULL);
        }

        // Bluetooth icon (right)
        GBitmap *bt = s_bt_connected ? s_bt_on_bitmap : s_bt_off_bitmap;
        if (bt) {
            graphics_draw_bitmap_in_rect(ctx, bt,
                GRect(W - margin - icon_w, icon_y, icon_w, icon_h));
        }
    }
}

// =============================================================================
// Update routines
// =============================================================================
// Renders the time string from cached components. Called whenever any of
// hour/minute/second changes (or when seconds need to freeze in place).
static void render_time(void) {
    if (s_face_mode == FaceModeLarger) {
        snprintf(s_time_buffer, sizeof(s_time_buffer), "%02d:%02d",
                 s_displayed_hour, s_displayed_min);
        if (s_seconds_active) {
            snprintf(s_seconds_buffer, sizeof(s_seconds_buffer), "%02d",
                     s_displayed_seconds);
        } else {
            s_seconds_buffer[0] = '\0';
        }
        if (s_seconds_layer) {
            text_layer_set_text(s_seconds_layer, s_seconds_buffer);
        }
    } else {
        snprintf(s_time_buffer, sizeof(s_time_buffer), "%02d:%02d:%02d",
                 s_displayed_hour, s_displayed_min, s_displayed_seconds);
    }
    text_layer_set_text(s_time_layer, s_time_buffer);
}

static void update_time_from_tm(struct tm *now, bool update_seconds) {
    s_displayed_hour = now->tm_hour;
    s_displayed_min  = now->tm_min;
    if (update_seconds) {
        s_displayed_seconds = now->tm_sec;
    }
    render_time();
}

static void update_time_and_date_from_tm(struct tm *now, bool update_seconds) {
    update_time_from_tm(now, update_seconds);

    char day_buf[3], wday_buf[6];
    strftime(day_buf,  sizeof(day_buf),  "%d", now);
    strftime(wday_buf, sizeof(wday_buf), "%a", now);

    if (s_face_mode == FaceModeLarger) {
        for (int i = 0; wday_buf[i]; i++) {
            wday_buf[i] = (char)toupper((unsigned char)wday_buf[i]);
        }
        snprintf(s_date_buffer, sizeof(s_date_buffer), "%s %s", wday_buf, day_buf);
        snprintf(s_temp_buffer, sizeof(s_temp_buffer),
                 s_temp_known ? "%d\u00B0C" : "--\u00B0C", s_temp_c);
        if (s_temp_layer) {
            text_layer_set_text(s_temp_layer, s_temp_buffer);
        }
    } else {
        if (s_temp_known) {
            snprintf(s_date_buffer, sizeof(s_date_buffer),
                     "%s %s.  |  %d\u00B0C", day_buf, wday_buf, s_temp_c);
        } else {
            snprintf(s_date_buffer, sizeof(s_date_buffer),
                     "%s %s.  |  --\u00B0C", day_buf, wday_buf);
        }
    }
    text_layer_set_text(s_date_layer, s_date_buffer);
}

static void update_time_and_date() {
    time_t now_t = time(NULL);
    struct tm *now = localtime(&now_t);

    update_time_and_date_from_tm(now, false);
}

static void update_health_data() {
#if defined(PBL_HEALTH)
    time_t start = time_start_of_today();
    time_t end   = time(NULL);

    // Steps today
    HealthMetric m_steps = HealthMetricStepCount;
    if (health_service_metric_accessible(m_steps, start, end)
        & HealthServiceAccessibilityMaskAvailable) {
        int steps = (int)health_service_sum_today(m_steps);
        snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%d", steps);
    } else {
        snprintf(s_steps_buffer, sizeof(s_steps_buffer), "--");
    }
#ifdef DEBUG_LAYOUT
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "29999");
#endif
    text_layer_set_text(s_steps_value_layer, s_steps_buffer);

    // Distance today (meters → km, 2 decimals)
    HealthMetric m_dist = HealthMetricWalkedDistanceMeters;
    if (health_service_metric_accessible(m_dist, start, end)
        & HealthServiceAccessibilityMaskAvailable) {
        int dist_m = (int)health_service_sum_today(m_dist);
        int km     = dist_m / 1000;
        int km_dec = (dist_m % 1000) / 10;
        snprintf(s_dist_buffer, sizeof(s_dist_buffer),
                 "%d.%02d", km, km_dec);
    } else {
        snprintf(s_dist_buffer, sizeof(s_dist_buffer), "--");
    }
    text_layer_set_text(s_dist_value_layer, s_dist_buffer);

    // Heart rate (most recent reading)
    HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (bpm > 0) {
        s_last_hr_bpm = (int)bpm;
        snprintf(s_hr_buffer, sizeof(s_hr_buffer), "%d", s_last_hr_bpm);
    } else {
        bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
        if (bpm > 0) {
            s_last_hr_bpm = (int)bpm;
            snprintf(s_hr_buffer, sizeof(s_hr_buffer), "%d", s_last_hr_bpm);
        } else if (s_last_hr_bpm > 0) {
            snprintf(s_hr_buffer, sizeof(s_hr_buffer), "%d", s_last_hr_bpm);
        } else {
            snprintf(s_hr_buffer, sizeof(s_hr_buffer), "--");
        }
    }
#ifdef DEBUG_LAYOUT
    snprintf(s_hr_buffer, sizeof(s_hr_buffer), "199");
#endif
    text_layer_set_text(s_hr_value_layer, s_hr_buffer);
#else
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "--");
    snprintf(s_hr_buffer,    sizeof(s_hr_buffer),    "--");
    snprintf(s_dist_buffer,  sizeof(s_dist_buffer),  "--");
    text_layer_set_text(s_steps_value_layer, s_steps_buffer);
    text_layer_set_text(s_hr_value_layer,    s_hr_buffer);
    text_layer_set_text(s_dist_value_layer,  s_dist_buffer);
#endif
}

// =============================================================================
// Battery service
// =============================================================================
static GColor get_large_battery_color(void) {
    return s_battery_level <= 20
        ? COLOR_FALLBACK(GColorMelon, GColorWhite)
        : COLOR_FALLBACK(GColorScreaminGreen, GColorWhite);
}

// =============================================================================
// Bluetooth / connection service
// =============================================================================
static void connection_callback(bool connected) {
    if (connected != s_bt_connected) {
        vibes_short_pulse();
    }
    s_bt_connected = connected;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// Update EWMA discharge rate from a fresh battery reading.
// Smoothing factor alpha = 0.2 (numerator 1, denom 5).
static void battery_estimator_update(BatteryChargeState state) {
    time_t now = time(NULL);

    if (state.is_charging) {
        // Freeze EWMA across a charge cycle. Reset only the dt anchor so the
        // first post-charge sample doesn't span the charging window.
        s_bat_last_pct = state.charge_percent;
        s_bat_last_ts  = now;
        return;
    }

    if (s_bat_last_ts == 0) {
        // First reading after install: just anchor.
        s_bat_last_pct = state.charge_percent;
        s_bat_last_ts  = now;
        return;
    }

    int32_t dt_sec = (int32_t)(now - s_bat_last_ts);
    int32_t dpct   = (int32_t)s_bat_last_pct - (int32_t)state.charge_percent;

    // Reject: noise floor (<3 min), no drop yet, or stale gap (>12 h).
    if (dt_sec < 180 || dpct <= 0 || dt_sec > 12 * 3600) {
        if (dpct < 0) {
            // Battery rose without is_charging set (firmware glitch). Re-anchor.
            s_bat_last_pct = state.charge_percent;
            s_bat_last_ts  = now;
        }
        return;
    }

    // rate_milli = dpct * 1000 * 3600 / dt_sec  (avoids float)
    int32_t rate_milli = (dpct * 3600 * 1000) / dt_sec;

    if (!s_bat_ewma_init) {
        s_bat_ewma_milli = rate_milli;
        s_bat_ewma_init  = true;
    } else {
        // EWMA: new = alpha*rate + (1-alpha)*old, alpha = 1/5
        s_bat_ewma_milli = (rate_milli + 4 * s_bat_ewma_milli) / 5;
    }

    s_bat_last_pct = state.charge_percent;
    s_bat_last_ts  = now;

    APP_LOG(APP_LOG_LEVEL_DEBUG,
            "bat est: dpct=%d dt=%ds rate=%ld ewma=%ld",
            (int)dpct, (int)dt_sec, (long)rate_milli, (long)s_bat_ewma_milli);
}

// Format current battery-life estimate into buf.
// Produces "charging", "—", "Xd Yh", or "Yh Zm".
static void battery_estimator_format(char *buf, size_t n) {
    if (s_battery_is_charging) {
        snprintf(buf, n, "charging");
        return;
    }
    if (!s_bat_ewma_init || s_bat_ewma_milli <= 0) {
        snprintf(buf, n, "—");
        return;
    }
    // total_minutes = pct * 60 * 1000 / ewma_milli
    int32_t total_min = ((int32_t)s_battery_level * 60 * 1000) / s_bat_ewma_milli;
    if (total_min < 0) total_min = 0;

    int days  = total_min / (60 * 24);
    int hours = (total_min / 60) % 24;
    int mins  = total_min % 60;

    if (days > 0) {
        snprintf(buf, n, "%dd %dh", days, hours);
    } else {
        snprintf(buf, n, "%dh %dm", hours, mins);
    }
}

static void battery_callback(BatteryChargeState state) {
    battery_estimator_update(state);
    s_battery_level = state.charge_percent;
    s_battery_is_charging = state.is_charging;
    snprintf(s_battery_text_buffer, sizeof(s_battery_text_buffer),
             "%d%%", s_battery_level);
    if (s_battery_value_layer) {
        text_layer_set_text(s_battery_value_layer, s_battery_text_buffer);
        text_layer_set_text_color(s_battery_value_layer, get_large_battery_color());
    }
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// =============================================================================
// Tick service
// =============================================================================

// Night-idle = setting on AND current hour falls inside the user's night
// window AND no accel taps for 20 minutes. Window can wrap past midnight
// (e.g. 22:00–06:00 means 22..23 OR 0..5).
static bool hour_in_night_window(int hour, int start, int end) {
    if (start == end)  return false;          // empty window
    if (start <  end)  return hour >= start && hour < end;
    return hour >= start || hour < end;       // wrap-around
}

static bool is_night_idle(struct tm *now) {
    if (!s_night_mode_enabled) return false;
    if (!hour_in_night_window(now->tm_hour, s_night_start_hour, s_night_end_hour)) {
        return false;
    }
    time_t current = time(NULL);
    if ((current - s_last_tap_time) < 20 * 60) return false;
    return true;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    if (units_changed & MINUTE_UNIT) {
        // Skip everything for 4 of every 5 minutes when night-idle
        if (is_night_idle(tick_time) && (tick_time->tm_min % 5 != 0)) {
            return;
        }

        s_displayed_seconds = 0;
        update_time_and_date();
        update_health_data();

        // Time-based weather refresh: respects user-configured interval and
        // works correctly for intervals longer than an hour (where the old
        // "minute % N == 0" check would never fire).
        time_t now = time(NULL);
        if (now - s_last_weather_fetch >= s_weather_interval_min * 60) {
            DictionaryIterator *iter;
            if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
                dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
                if (app_message_outbox_send() == APP_MSG_OK) {
                    s_last_weather_fetch = now;
                }
            }
        }
    }

    // On-demand seconds are driven by a short app timer while the watch is
    // awake. Keeping the system tick service on MINUTE_UNIT avoids churn in
    // tick subscriptions during accel wake.
}

// =============================================================================
// On-demand seconds: react to wrist-flicks like the backlight does
// =============================================================================
static void seconds_tick_handler(void *context);

static void refresh_active_seconds(void) {
    time_t now_t = time(NULL);
    struct tm *now = localtime(&now_t);
    if (now->tm_hour != s_displayed_hour ||
        now->tm_min  != s_displayed_min  ||
        now->tm_sec  != s_displayed_seconds) {
        update_time_from_tm(now, true);
    }
}

static void schedule_seconds_tick(void) {
    if (s_seconds_active && !s_seconds_tick_timer) {
        s_seconds_tick_timer = app_timer_register(
            SECONDS_POLL_MS, seconds_tick_handler, NULL);
    }
}

static void seconds_tick_handler(void *context) {
    s_seconds_tick_timer = NULL;
    if (!s_seconds_active) {
        return;
    }

    refresh_active_seconds();
    schedule_seconds_tick();
}

static void seconds_timeout_handler(void *context) {
    // Backlight is presumed off now. Stop per-second updates, but leave the
    // last displayed seconds value frozen on screen until the next minute.
    s_seconds_active = false;
    s_seconds_timeout_timer = NULL;
    if (s_seconds_tick_timer) {
        app_timer_cancel(s_seconds_tick_timer);
        s_seconds_tick_timer = NULL;
    }
    if (s_face_mode == FaceModeLarger) {
        render_time();
    }
}

static void deferred_refresh_handler(void *context) {
    s_deferred_refresh_timer = NULL;
    update_health_data();
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    time_t now_t = time(NULL);
    struct tm *now = localtime(&now_t);

    // Record this tap so the night-idle gate knows the wrist just moved.
    s_last_tap_time = now_t;

    // If the watchface was in night-idle, the displayed HH:MM could be up to
    // 5 minutes stale. Refresh immediately so the user sees the correct time
    // the moment they look at the watch.
    if (!s_seconds_active) {
        s_seconds_active = true;
    }

    // Show fresh seconds on every tap. Multiple accel taps can arrive while
    // seconds are already active; repainting with cached seconds would make
    // the display appear to skip a second on the next tick.
    update_time_and_date_from_tm(now, true);

    // Reset the auto-shutoff timer on every tap (gives extended viewing if
    // the user keeps moving their wrist).
    if (s_seconds_timeout_timer) {
        app_timer_reschedule(s_seconds_timeout_timer, SECONDS_DURATION_MS);
    } else {
        s_seconds_timeout_timer = app_timer_register(
            SECONDS_DURATION_MS, seconds_timeout_handler, NULL);
    }
    schedule_seconds_tick();

    // Health queries can be slow enough to delay tick delivery on some
    // watches. Keep the active seconds window focused on time rendering, then
    // refresh stats after seconds freeze again.
    if (s_deferred_refresh_timer) {
        app_timer_reschedule(s_deferred_refresh_timer, SECONDS_DURATION_MS + 250);
    } else {
        s_deferred_refresh_timer = app_timer_register(
            SECONDS_DURATION_MS + 250, deferred_refresh_handler, NULL);
    }
}

// =============================================================================
// AppMessage (weather from PebbleKit JS)
// =============================================================================
static void apply_face_mode_layout(GRect bounds);

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    Tuple *t = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
    if (t) {
        s_temp_c = (int)t->value->int32;
        s_temp_known = true;
        update_time_and_date();   // re-renders date row with new temp
    }
    Tuple *wc = dict_find(iterator, MESSAGE_KEY_WEATHER_CODE);
    if (wc) {
        s_weather_code = (int)wc->value->int32;
        if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    }

    // Settings from the configuration page
    Tuple *night = dict_find(iterator, MESSAGE_KEY_NIGHT_MODE_ENABLED);
    if (night) {
        s_night_mode_enabled = (night->value->int32 != 0);
        persist_write_bool(PERSIST_KEY_NIGHT_MODE, s_night_mode_enabled);
        APP_LOG(APP_LOG_LEVEL_INFO, "Night mode: %d", s_night_mode_enabled);
    }
    Tuple *nstart = dict_find(iterator, MESSAGE_KEY_NIGHT_START_HOUR);
    if (nstart) {
        int v = (int)nstart->value->int32;
        if (v >= 0 && v <= 23) {
            s_night_start_hour = v;
            persist_write_int(PERSIST_KEY_NIGHT_START, s_night_start_hour);
            APP_LOG(APP_LOG_LEVEL_INFO, "Night start: %d:00", v);
        }
    }
    Tuple *nend = dict_find(iterator, MESSAGE_KEY_NIGHT_END_HOUR);
    if (nend) {
        int v = (int)nend->value->int32;
        if (v >= 0 && v <= 23) {
            s_night_end_hour = v;
            persist_write_int(PERSIST_KEY_NIGHT_END, s_night_end_hour);
            APP_LOG(APP_LOG_LEVEL_INFO, "Night end: %d:00", v);
        }
    }
    Tuple *interval = dict_find(iterator, MESSAGE_KEY_WEATHER_INTERVAL);
    if (interval) {
        int v = (int)interval->value->int32;
        if (v >= 5 && v <= 720) {            // sanity bounds
            s_weather_interval_min = v;
            persist_write_int(PERSIST_KEY_WEATHER_INT, s_weather_interval_min);
            APP_LOG(APP_LOG_LEVEL_INFO, "Weather interval: %d min", v);
        }
    }
    Tuple *mode = dict_find(iterator, MESSAGE_KEY_FACE_MODE);
    if (mode) {
        int v = (int)mode->value->int32;
        if (v == FaceModeMinimal || v == FaceModeLarger) {
            s_face_mode = (FaceMode)v;
            persist_write_int(PERSIST_KEY_FACE_MODE, (int)s_face_mode);
            if (s_main_window) {
                Layer *root = window_get_root_layer(s_main_window);
                apply_face_mode_layout(layer_get_bounds(root));
                update_time_and_date();
                update_health_data();
            }
            APP_LOG(APP_LOG_LEVEL_INFO, "Face mode: %d", v);
        }
    }

    Tuple *bat_req = dict_find(iterator, MESSAGE_KEY_REQUEST_BATTERY_INFO);
    if (bat_req) {
        char est[24];
        battery_estimator_format(est, sizeof(est));
        DictionaryIterator *out;
        if (app_message_outbox_begin(&out) == APP_MSG_OK) {
            dict_write_cstring(out, MESSAGE_KEY_BATTERY_ESTIMATE, est);
            dict_write_int32  (out, MESSAGE_KEY_BATTERY_RATE_MILLI, s_bat_ewma_milli);
            app_message_outbox_send();
        }
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
}

static void outbox_sent_callback(DictionaryIterator *it, void *ctx) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox sent");
}

// =============================================================================
// Window load / unload
// =============================================================================
static void set_text_layer_hidden(TextLayer *text_layer, bool hidden) {
    if (text_layer) {
        layer_set_hidden(text_layer_get_layer(text_layer), hidden);
    }
}

static void apply_face_mode_layout(GRect bounds) {
    const int W = bounds.size.w;
    const bool large = (s_face_mode == FaceModeLarger);
    GColor red = COLOR_FALLBACK(GColorMelon, GColorWhite);

    layer_set_frame(text_layer_get_layer(s_date_layer),
                    large ? GRect(104, LARGE_TOP_Y, 80, LARGE_TOP_H)
                          : GRect(0, DATE_ROW_Y, W, 22));
    text_layer_set_font(s_date_layer, large ? s_font_top : s_font_label);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_text_alignment(s_date_layer,
                                  large ? GTextAlignmentRight : GTextAlignmentCenter);

    layer_set_frame(text_layer_get_layer(s_time_layer),
                    large ? GRect(LARGE_TIME_X, LARGE_TIME_Y, LARGE_TIME_W, LARGE_TIME_H)
                          : GRect(0, TIME_Y, W, 58));
    text_layer_set_font(s_time_layer, large ? s_font_time_large : s_font_time);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

    set_text_layer_hidden(s_steps_label_layer, large);
    set_text_layer_hidden(s_km_label_layer, large);
    set_text_layer_hidden(s_dist_value_layer, large);
    set_text_layer_hidden(s_temp_layer, !large);
    set_text_layer_hidden(s_seconds_layer, !large);
    set_text_layer_hidden(s_battery_value_layer, !large);

    layer_set_frame(text_layer_get_layer(s_steps_label_layer),
                    GRect(10, STATS_LABEL_Y - 1, 60, 18));
    layer_set_frame(text_layer_get_layer(s_km_label_layer),
                    GRect(W - 70, STATS_LABEL_Y - 1, 60, 18));
    text_layer_set_font(s_steps_label_layer, s_font_small);
    text_layer_set_font(s_km_label_layer, s_font_small);

    layer_set_frame(text_layer_get_layer(s_steps_value_layer),
                    large ? GRect(42, LARGE_STATS_Y + 8, 50, 32)
                          : GRect(10, STATS_VALUE_Y, 60, 22));
    text_layer_set_font(s_steps_value_layer, large ? s_font_stat_large : s_font_stat);
    text_layer_set_text_color(s_steps_value_layer, GColorWhite);
    text_layer_set_text_alignment(s_steps_value_layer,
                                  large ? GTextAlignmentLeft : GTextAlignmentCenter);

    layer_set_frame(text_layer_get_layer(s_hr_value_layer),
                    large ? GRect(146, LARGE_STATS_Y + 8, 38, 32)
                          : GRect(0, STATS_VALUE_Y, W, 22));
    text_layer_set_font(s_hr_value_layer, large ? s_font_stat_large : s_font_stat);
    text_layer_set_text_color(s_hr_value_layer, large ? red : GColorWhite);
    text_layer_set_text_alignment(s_hr_value_layer,
                                  large ? GTextAlignmentLeft : GTextAlignmentCenter);

    layer_set_frame(text_layer_get_layer(s_dist_value_layer),
                    GRect(W - 70, STATS_VALUE_Y, 60, 22));
    text_layer_set_font(s_dist_value_layer, s_font_stat);
    text_layer_set_text_color(s_dist_value_layer, GColorWhite);
    text_layer_set_text_alignment(s_dist_value_layer, GTextAlignmentCenter);

    layer_set_frame(text_layer_get_layer(s_temp_layer),
                    GRect(48, LARGE_TOP_Y, 56, LARGE_TOP_H));
    text_layer_set_font(s_temp_layer, s_font_top);
    text_layer_set_text_color(s_temp_layer, GColorWhite);
    text_layer_set_text_alignment(s_temp_layer, GTextAlignmentLeft);

    layer_set_frame(text_layer_get_layer(s_seconds_layer),
                    GRect(LARGE_STATUS_CENTER_X - 16, LARGE_SECONDS_Y, 32, 28));
    text_layer_set_font(s_seconds_layer, s_font_label);
    text_layer_set_text_color(s_seconds_layer, GColorWhite);
    text_layer_set_text_alignment(s_seconds_layer, GTextAlignmentCenter);

    layer_set_frame(text_layer_get_layer(s_battery_value_layer),
                    GRect(50, LARGE_BATTERY_Y + 4, 50, 32));
    text_layer_set_font(s_battery_value_layer, s_font_stat_large);
    text_layer_set_text_color(s_battery_value_layer, get_large_battery_color());
    text_layer_set_text_alignment(s_battery_value_layer, GTextAlignmentLeft);

    if (s_canvas_layer) {
        layer_mark_dirty(s_canvas_layer);
    }
}

static void main_window_load(Window *window) {
    Layer *root  = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    const int W  = bounds.size.w;

    // 0. Load custom fonts (Atkinson Hyperlegible) from resources
    s_font_time  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_56));
    s_font_time_large = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_72));
    s_font_top = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TOP_20));
    s_font_label = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_18));
    s_font_stat  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_18));
    s_font_stat_large = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_STAT_22));
    s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_14));

    // Load bitmap resources
    s_battery_full_bitmap     = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_FULL);
    s_battery_charging_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CHARGING);
    s_battery_75_bitmap       = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_75);
    s_battery_50_bitmap       = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_50);
    s_battery_25_bitmap       = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_25);
    s_bt_on_bitmap            = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_ON);
    s_bt_off_bitmap           = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_OFF);
    s_steps_bitmap            = gbitmap_create_with_resource(RESOURCE_ID_STEPS);

    // 1. Custom canvas covering the whole screen
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);

    // 2. Date row (centered)
    s_date_layer = text_layer_create(GRect(0, DATE_ROW_Y, W, 22));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, s_font_label);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    // 3. Big time — full-width frame so HH:MM:SS centers on the display
    s_time_layer = text_layer_create(GRect(0, TIME_Y, W, 58));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_font_time);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));

    // 5. Stats label pills: STEPS / KM (frames match pill backgrounds)
    s_steps_label_layer = text_layer_create(GRect(10, STATS_LABEL_Y - 1, 60, 18));
    text_layer_set_background_color(s_steps_label_layer, GColorClear);
    text_layer_set_text_color(s_steps_label_layer, GColorWhite);
    text_layer_set_font(s_steps_label_layer, s_font_small);
    text_layer_set_text_alignment(s_steps_label_layer, GTextAlignmentCenter);
    text_layer_set_text(s_steps_label_layer, "STEPS");
    layer_add_child(root, text_layer_get_layer(s_steps_label_layer));

    s_km_label_layer = text_layer_create(GRect(W - 70, STATS_LABEL_Y - 1, 60, 18));
    text_layer_set_background_color(s_km_label_layer, GColorClear);
    text_layer_set_text_color(s_km_label_layer, GColorWhite);
    text_layer_set_font(s_km_label_layer, s_font_small);
    text_layer_set_text_alignment(s_km_label_layer, GTextAlignmentCenter);
    text_layer_set_text(s_km_label_layer, "KM");
    layer_add_child(root, text_layer_get_layer(s_km_label_layer));

    // 6. Stats values: 60-wide frames centered at x=40, x=100, x=160 to align with pills/heart
    s_steps_value_layer = text_layer_create(GRect(10, STATS_VALUE_Y, 60, 22));
    text_layer_set_background_color(s_steps_value_layer, GColorClear);
    text_layer_set_text_color(s_steps_value_layer, GColorWhite);
    text_layer_set_font(s_steps_value_layer, s_font_stat);
    text_layer_set_text_alignment(s_steps_value_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_steps_value_layer));

    // HR: full-width frame (same as battery text below it) so center alignment
    //     produces the exact same x-coordinate. The result is guaranteed to be
    //     in the same column as the heart icon (drawn at W/2) and the battery
    //     percentage text.
    s_hr_value_layer = text_layer_create(GRect(0, STATS_VALUE_Y, W, 22));
    text_layer_set_background_color(s_hr_value_layer, GColorClear);
    text_layer_set_text_color(s_hr_value_layer, GColorWhite);
    text_layer_set_font(s_hr_value_layer, s_font_stat);
    text_layer_set_text_alignment(s_hr_value_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_hr_value_layer));

    s_dist_value_layer = text_layer_create(GRect(W - 70, STATS_VALUE_Y, 60, 22));
    text_layer_set_background_color(s_dist_value_layer, GColorClear);
    text_layer_set_text_color(s_dist_value_layer, GColorWhite);
    text_layer_set_font(s_dist_value_layer, s_font_stat);
    text_layer_set_text_alignment(s_dist_value_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_dist_value_layer));

    s_temp_layer = text_layer_create(GRect(0, 0, 1, 1));
    text_layer_set_background_color(s_temp_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_temp_layer));

    s_seconds_layer = text_layer_create(GRect(0, 0, 1, 1));
    text_layer_set_background_color(s_seconds_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_seconds_layer));

    s_battery_value_layer = text_layer_create(GRect(0, 0, 1, 1));
    text_layer_set_background_color(s_battery_value_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_battery_value_layer));

    // Apply the selected mode once all shared and large-mode layers exist.
    apply_face_mode_layout(bounds);

    // Draw initial state
    update_time_and_date();
    update_health_data();
    battery_callback(battery_state_service_peek());
}

static void main_window_unload(Window *window) {
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_steps_label_layer);
    text_layer_destroy(s_km_label_layer);
    text_layer_destroy(s_steps_value_layer);
    text_layer_destroy(s_hr_value_layer);
    text_layer_destroy(s_dist_value_layer);
    text_layer_destroy(s_temp_layer);
    text_layer_destroy(s_seconds_layer);
    text_layer_destroy(s_battery_value_layer);
    layer_destroy(s_canvas_layer);

    fonts_unload_custom_font(s_font_time);
    fonts_unload_custom_font(s_font_time_large);
    fonts_unload_custom_font(s_font_top);
    fonts_unload_custom_font(s_font_label);
    fonts_unload_custom_font(s_font_stat);
    fonts_unload_custom_font(s_font_stat_large);
    fonts_unload_custom_font(s_font_small);

    if (s_battery_full_bitmap)     gbitmap_destroy(s_battery_full_bitmap);
    if (s_battery_charging_bitmap) gbitmap_destroy(s_battery_charging_bitmap);
    if (s_battery_75_bitmap)       gbitmap_destroy(s_battery_75_bitmap);
    if (s_battery_50_bitmap)       gbitmap_destroy(s_battery_50_bitmap);
    if (s_battery_25_bitmap)       gbitmap_destroy(s_battery_25_bitmap);
    if (s_bt_on_bitmap)            gbitmap_destroy(s_bt_on_bitmap);
    if (s_bt_off_bitmap)           gbitmap_destroy(s_bt_off_bitmap);
    if (s_steps_bitmap)            gbitmap_destroy(s_steps_bitmap);
}

// =============================================================================
// init / deinit / main
// =============================================================================
static void init() {
    // Restore last-known weather from persistent storage so the icon and
    // temperature aren't blank between launches.
    if (persist_exists(PERSIST_KEY_TEMP)) {
        s_temp_c     = persist_read_int(PERSIST_KEY_TEMP);
        s_temp_known = persist_read_bool(PERSIST_KEY_TEMP_KNOWN);
    }
    if (persist_exists(PERSIST_KEY_WEATHER)) {
        s_weather_code = persist_read_int(PERSIST_KEY_WEATHER);
    }

    // Restore configuration (set via the settings page)
    if (persist_exists(PERSIST_KEY_NIGHT_MODE)) {
        s_night_mode_enabled = persist_read_bool(PERSIST_KEY_NIGHT_MODE);
    }
    if (persist_exists(PERSIST_KEY_NIGHT_START)) {
        s_night_start_hour = persist_read_int(PERSIST_KEY_NIGHT_START);
    }
    if (persist_exists(PERSIST_KEY_NIGHT_END)) {
        s_night_end_hour = persist_read_int(PERSIST_KEY_NIGHT_END);
    }
    if (persist_exists(PERSIST_KEY_WEATHER_INT)) {
        s_weather_interval_min = persist_read_int(PERSIST_KEY_WEATHER_INT);
    }
    if (persist_exists(PERSIST_KEY_FACE_MODE)) {
        int mode = persist_read_int(PERSIST_KEY_FACE_MODE);
        if (mode == FaceModeMinimal || mode == FaceModeLarger) {
            s_face_mode = (FaceMode)mode;
        }
    }

    // Restore battery estimator state across launches.
    if (persist_exists(PERSIST_KEY_BAT_EWMA_INIT)) {
        s_bat_ewma_init = persist_read_bool(PERSIST_KEY_BAT_EWMA_INIT);
    }
    if (persist_exists(PERSIST_KEY_BAT_EWMA_MILLI)) {
        s_bat_ewma_milli = persist_read_int(PERSIST_KEY_BAT_EWMA_MILLI);
        if (s_bat_ewma_milli <= 0) s_bat_ewma_init = false;
    }
    if (persist_exists(PERSIST_KEY_BAT_LAST_PCT)) {
        s_bat_last_pct = (uint8_t)persist_read_int(PERSIST_KEY_BAT_LAST_PCT);
    }
    if (persist_exists(PERSIST_KEY_BAT_LAST_TS)) {
        s_bat_last_ts = (time_t)persist_read_int(PERSIST_KEY_BAT_LAST_TS);
    }

    // Treat startup as a recent "tap" so we don't immediately enter night-idle
    // (e.g., during a watch reboot at 3am).
    s_last_tap_time = time(NULL);

    s_main_window = window_create();
    window_set_background_color(s_main_window, GColorBlack);
    window_set_window_handlers(s_main_window, (WindowHandlers){
        .load   = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    accel_tap_service_subscribe(accel_tap_handler);
    s_bt_connected = connection_service_peek_pebble_app_connection();
    connection_service_subscribe((ConnectionHandlers){
        .pebble_app_connection_handler = connection_callback
    });

    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);
    app_message_open(128, 128);
}

static void deinit() {
    persist_write_int (PERSIST_KEY_TEMP,        s_temp_c);
    persist_write_bool(PERSIST_KEY_TEMP_KNOWN,  s_temp_known);
    persist_write_int (PERSIST_KEY_WEATHER,     s_weather_code);
    persist_write_bool(PERSIST_KEY_NIGHT_MODE,  s_night_mode_enabled);
    persist_write_int (PERSIST_KEY_NIGHT_START, s_night_start_hour);
    persist_write_int (PERSIST_KEY_NIGHT_END,   s_night_end_hour);
    persist_write_int (PERSIST_KEY_WEATHER_INT, s_weather_interval_min);
    persist_write_int (PERSIST_KEY_FACE_MODE,   (int)s_face_mode);
    persist_write_int (PERSIST_KEY_BAT_LAST_PCT,   (int)s_bat_last_pct);
    persist_write_int (PERSIST_KEY_BAT_LAST_TS,    (int)s_bat_last_ts);
    persist_write_int (PERSIST_KEY_BAT_EWMA_MILLI, (int)s_bat_ewma_milli);
    persist_write_bool(PERSIST_KEY_BAT_EWMA_INIT,  s_bat_ewma_init);
    if (s_seconds_timeout_timer) app_timer_cancel(s_seconds_timeout_timer);
    if (s_seconds_tick_timer) app_timer_cancel(s_seconds_tick_timer);
    if (s_deferred_refresh_timer) app_timer_cancel(s_deferred_refresh_timer);
    accel_tap_service_unsubscribe();
    connection_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
