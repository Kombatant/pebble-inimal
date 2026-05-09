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

// Custom fonts (Atkinson Hyperlegible, loaded from resources)
static GFont s_font_time;
static GFont s_font_label;
static GFont s_font_stat;
static GFont s_font_small;

// Bitmaps loaded from resources
static GBitmap *s_bolt_bitmap;
static GBitmap *s_bt_on_bitmap;
static GBitmap *s_bt_off_bitmap;

// Bluetooth connection state
static bool s_bt_connected = true;

// Buffers (must remain valid as long as TextLayer references them)
static char s_time_buffer[12];   // "HH:MM:SS" + null
static char s_date_buffer[32];
static char s_steps_buffer[12];
static char s_hr_buffer[12];
static char s_dist_buffer[16];
static char s_battery_text_buffer[8];

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
static AppTimer *s_seconds_timer = NULL;
#define SECONDS_DURATION_MS 3500

// State
static int s_battery_level = 100;
static int s_temp_c = 0;
static bool s_temp_known = false;
static int s_weather_code = -1;   // -1 = unknown, otherwise WMO code from Open-Meteo

// Configuration (mirrored on the phone via the settings page)
static bool s_night_mode_enabled  = false;   // default: OFF (user opts in)
static int  s_night_start_hour    = 0;       // 00:00 = midnight
static int  s_night_end_hour      = 6;       // 06:00 = 6 AM
static int  s_weather_interval_min = 30;

// Tracking state for the night-idle gate and the weather refresh schedule
static time_t s_last_tap_time      = 0;
static time_t s_last_weather_fetch = 0;

// Persistent storage keys
#define PERSIST_KEY_TEMP         100
#define PERSIST_KEY_TEMP_KNOWN   101
#define PERSIST_KEY_WEATHER      102
#define PERSIST_KEY_NIGHT_MODE   103
#define PERSIST_KEY_WEATHER_INT  104
#define PERSIST_KEY_NIGHT_START  105
#define PERSIST_KEY_NIGHT_END    106

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

// =============================================================================
// Canvas update procedure
// =============================================================================
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    const int W = bounds.size.w;
    const int H = bounds.size.h;

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
        if (s_bolt_bitmap) {
            graphics_draw_bitmap_in_rect(ctx, s_bolt_bitmap,
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
    snprintf(s_time_buffer, sizeof(s_time_buffer), "%02d:%02d:%02d",
             s_displayed_hour, s_displayed_min, s_displayed_seconds);
    text_layer_set_text(s_time_layer, s_time_buffer);
}

static void update_time_and_date() {
    time_t now_t = time(NULL);
    struct tm *now = localtime(&now_t);

    s_displayed_hour = now->tm_hour;
    s_displayed_min  = now->tm_min;
    render_time();

    char day_buf[3], wday_buf[6];
    strftime(day_buf,  sizeof(day_buf),  "%d", now);
    strftime(wday_buf, sizeof(wday_buf), "%a", now);
    if (s_temp_known) {
        snprintf(s_date_buffer, sizeof(s_date_buffer),
                 "%s %s.  |  %d\u00B0C", day_buf, wday_buf, s_temp_c);
    } else {
        snprintf(s_date_buffer, sizeof(s_date_buffer),
                 "%s %s.  |  --\u00B0C", day_buf, wday_buf);
    }
    text_layer_set_text(s_date_layer, s_date_buffer);
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
        snprintf(s_hr_buffer, sizeof(s_hr_buffer), "%d", (int)bpm);
    } else {
        snprintf(s_hr_buffer, sizeof(s_hr_buffer), "--");
    }
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

static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    snprintf(s_battery_text_buffer, sizeof(s_battery_text_buffer),
             "%d%%", s_battery_level);
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

    // Per-second updates only fire while we're subscribed to SECOND_UNIT
    // (accel-tap path). When we drop back to MINUTE_UNIT the displayed value
    // freezes because render_time() isn't called again until the next minute.
    if (units_changed & SECOND_UNIT) {
        s_displayed_seconds = tick_time->tm_sec;
        render_time();
    }
}

// =============================================================================
// On-demand seconds: react to wrist-flicks like the backlight does
// =============================================================================
static void seconds_timeout_handler(void *context) {
    // Backlight is presumed off now. Stop per-second updates, but leave the
    // last displayed seconds value frozen on screen until the next minute.
    s_seconds_active = false;
    s_seconds_timer  = NULL;
    tick_timer_service_unsubscribe();
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    // Record this tap so the night-idle gate knows the wrist just moved.
    s_last_tap_time = time(NULL);

    // If the watchface was in night-idle, the displayed HH:MM (and stats)
    // could be up to 5 minutes stale. Refresh immediately so the user sees
    // the correct time the moment they look at the watch.
    update_time_and_date();
    update_health_data();

    if (!s_seconds_active) {
        s_seconds_active = true;
        tick_timer_service_unsubscribe();
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);

        // Show the current seconds immediately so the user doesn't have to
        // wait up to a second for the first update.
        time_t now_t = time(NULL);
        struct tm *now = localtime(&now_t);
        s_displayed_seconds = now->tm_sec;
        render_time();
    }

    // Reset the auto-shutoff timer on every tap (gives extended viewing if
    // the user keeps moving their wrist).
    if (s_seconds_timer) {
        app_timer_reschedule(s_seconds_timer, SECONDS_DURATION_MS);
    } else {
        s_seconds_timer = app_timer_register(SECONDS_DURATION_MS,
                                             seconds_timeout_handler, NULL);
    }
}

// =============================================================================
// AppMessage (weather from PebbleKit JS)
// =============================================================================
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
static void main_window_load(Window *window) {
    Layer *root  = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    const int W  = bounds.size.w;

    // 0. Load custom fonts (Atkinson Hyperlegible) from resources
    s_font_time  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_56));
    s_font_label = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_18));
    s_font_stat  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TEXT_18));
    s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LABEL_14));

    // Load bitmap resources
    s_bolt_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_BOLT);
    s_bt_on_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_ON);
    s_bt_off_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH_OFF);

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

    // Battery percentage is now drawn directly in canvas_update_proc as part
    // of the centered (icon + text) unit. No TextLayer needed.

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
    layer_destroy(s_canvas_layer);

    fonts_unload_custom_font(s_font_time);
    fonts_unload_custom_font(s_font_label);
    fonts_unload_custom_font(s_font_stat);
    fonts_unload_custom_font(s_font_small);

    if (s_bolt_bitmap)   gbitmap_destroy(s_bolt_bitmap);
    if (s_bt_on_bitmap)  gbitmap_destroy(s_bt_on_bitmap);
    if (s_bt_off_bitmap) gbitmap_destroy(s_bt_off_bitmap);
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
    if (s_seconds_timer) app_timer_cancel(s_seconds_timer);
    accel_tap_service_unsubscribe();
    connection_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
