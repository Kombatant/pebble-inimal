// =============================================================================
// PebbleKit JS — runs on the phone, fetches weather from Open-Meteo,
// and sends the temperature back to the watch via AppMessage.
// =============================================================================

var APP_VERSION = (function () {
    try { return require('./version').version; }
    catch (e) { return '0.0.0'; }
})();

var xhrRequest = function (url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function () { callback(this.responseText); };
    xhr.onerror = function () { console.log('XHR error'); };
    xhr.open(type, url);
    xhr.send();
};

function locationSuccess(pos) {
    var url = 'https://api.open-meteo.com/v1/forecast' +
              '?latitude='  + pos.coords.latitude +
              '&longitude=' + pos.coords.longitude +
              '&current=temperature_2m,weather_code';

    xhrRequest(url, 'GET', function (responseText) {
        try {
            var json = JSON.parse(responseText);
            var temperature  = Math.round(json.current.temperature_2m);
            var weather_code = json.current.weather_code;
            var dictionary = {
                'TEMPERATURE':  temperature,
                'WEATHER_CODE': weather_code
            };

            Pebble.sendAppMessage(dictionary,
                function ()  {
                    localStorage.setItem('lastWeatherFetch', String(Date.now()));
                    console.log('Weather sent: ' + temperature + 'C, code ' + weather_code);
                },
                function (e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
            );
        } catch (e) {
            console.log('Weather parse error: ' + e);
        }
    });
}

function locationError(err) {
    console.log('Location error: ' + err.message);
}

function getWeather() {
    navigator.geolocation.getCurrentPosition(
        locationSuccess,
        locationError,
        { timeout: 15000, maximumAge: 600000 }
    );
}

Pebble.addEventListener('ready', function () {
    console.log('PebbleKit JS ready');
    // The JS environment restarts often (phone app relaunch, BT reconnect);
    // an unconditional fetch here would push off-schedule weather updates
    // that wake the watch. Fetch only if the last successful send is older
    // than the configured refresh interval. Watch-driven REQUEST_WEATHER
    // messages always fetch.
    var intervalMin = parseInt(localStorage.getItem('weather') || '30', 10);
    var lastFetch = parseInt(localStorage.getItem('lastWeatherFetch') || '0', 10);
    if (Date.now() - lastFetch >= intervalMin * 60 * 1000) {
        getWeather();
    }
});

Pebble.addEventListener('appmessage', function (e) {
    if (e.payload.REQUEST_WEATHER) {
        getWeather();
    }
    if (typeof e.payload.BATTERY_ESTIMATE !== 'undefined') {
        localStorage.setItem('batteryEstimate', String(e.payload.BATTERY_ESTIMATE));
        localStorage.setItem('batterySinceCharge', String(e.payload.BATTERY_SINCE_CHARGE || ''));
        localStorage.setItem('batteryRateMilli', String(e.payload.BATTERY_RATE_MILLI || 0));
        localStorage.setItem('batteryUpdatedAt', String(Date.now()));
        console.log('Battery info: ' + e.payload.BATTERY_ESTIMATE +
                    ', since charge ' + e.payload.BATTERY_SINCE_CHARGE +
                    ' (' + e.payload.BATTERY_RATE_MILLI + ' m%/h)');
        // Fresh values arrived — if the config page is waiting on them, open
        // it now so the user sees current data instead of the stale cache.
        openConfigWhenReady();
    }
});

// Config-page open is deferred until fresh battery info arrives (or a short
// timeout elapses), so the page never shows stale battery stats. These guard
// against opening twice or leaving a stale timer behind.
var configOpenPending = false;
var configOpenTimer = null;

function openConfigWhenReady() {
    if (!configOpenPending) { return; }
    configOpenPending = false;
    if (configOpenTimer !== null) {
        clearTimeout(configOpenTimer);
        configOpenTimer = null;
    }
    Pebble.openURL(buildConfigUrl());
}

// =============================================================================
// Configuration page
// =============================================================================
// The HTML page is embedded as a string and served via a data: URL — no
// external hosting required. When the user taps Save, the page redirects to
// pebblejs://close#<json> which fires 'webviewclosed' below.

var CONFIG_HTML =
'<!DOCTYPE html><html><head>' +
'<meta name="viewport" content="width=device-width,initial-scale=1">' +
'<title>Pebble-inimal Settings</title><style>' +
'body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;' +
'padding:16px;max-width:420px;margin:0 auto;background:#f0f0f3;color:#222}' +
'h1{font-size:1.4em;margin:8px 0 16px}' +
'.field{background:#fff;padding:16px;border-radius:10px;margin-bottom:14px;' +
'box-shadow:0 1px 2px rgba(0,0,0,.04)}' +
'label{font-weight:600;display:block;margin-bottom:4px}' +
'.desc{color:#888;font-size:.85em;margin-top:6px}' +
'select,input[type=time]{width:100%;padding:10px;font-size:16px;' +
'border:1px solid #ccc;border-radius:6px;background:#fff;' +
'-webkit-appearance:none;appearance:none}' +
'.toggle{display:flex;align-items:center;gap:12px}' +
'.toggle input{width:22px;height:22px}' +
'.toggle label{margin:0}' +
'.row{display:flex;gap:12px;margin-top:12px}' +
'.row > div{flex:1}' +
'.row label{font-weight:500;font-size:.9em;margin-bottom:6px}' +
'.metric{display:flex;align-items:baseline;justify-content:space-between;' +
'gap:12px;margin-top:8px}' +
'.metric .name{color:#555;font-size:.9em}' +
'.metric .value{font-size:1em;color:#222;font-weight:600;white-space:nowrap}' +
'#nightOptions{margin-top:14px;padding-top:14px;border-top:1px solid #eee}' +
'.hidden{display:none}' +
'button{width:100%;padding:14px;font-size:16px;background:#007aff;' +
'color:#fff;border:0;border-radius:8px;margin-top:8px}' +
'</style></head><body>' +
'<h1>Pebble-inimal Watchface</h1>' +

'<div class="field">' +
'<label for="faceMode">Watchface design</label>' +
'<select id="faceMode">' +
'<option value="0" __FM0__>Minimal mode</option>' +
'<option value="1" __FM1__>Larger mode</option>' +
'</select></div>' +

'<div class="field __BACKLIGHT_FIELD_CLASS__" id="backlightField">' +
'<label for="backlight">Backlight colour</label>' +
'<select id="backlight">' +
'<option value="0" __BL0__>System Default</option>' +
'<option value="1" __BL1__>White</option>' +
'<option value="2" __BL2__>YInMn Blue</option>' +
'<option value="3" __BL3__>Red</option>' +
'<option value="4" __BL4__>Amber</option>' +
'<option value="5" __BL5__>Yellow</option>' +
'<option value="6" __BL6__>Green</option>' +
'</select>' +
'<div class="desc">Tints the backlight LED. Available on Pebble Time 2 only.</div>' +
'</div>' +

'<div class="field">' +
'<div class="toggle">' +
'<input type="checkbox" id="nightMode" __NIGHT_CHECKED__>' +
'<label for="nightMode">Night idle mode</label></div>' +
'<div class="desc">After 20 minutes without wrist movement during your night ' +
'window, the watchface slows display and health updates and pauses weather refreshes. ' +
'Shaking your wrist wakes it back up immediately.</div>' +
'<div id="nightOptions" class="__NIGHT_OPTS_CLASS__">' +
'<div class="row">' +
'<div><label for="nightStart">Start</label>' +
'<input type="time" id="nightStart" value="__NIGHT_START__" step="3600"></div>' +
'<div><label for="nightEnd">End</label>' +
'<input type="time" id="nightEnd" value="__NIGHT_END__" step="3600"></div>' +
'</div>' +
'<label for="nightUpdate">Update cadence</label>' +
'<select id="nightUpdate">' +
'<option value="3"  __NU3__>Every 3 minutes</option>' +
'<option value="5"  __NU5__>Every 5 minutes</option>' +
'<option value="10" __NU10__>Every 10 minutes</option>' +
'<option value="15" __NU15__>Every 15 minutes</option>' +
'</select>' +
'</div></div>' +

'<div class="field">' +
'<label for="weather">Weather refresh</label>' +
'<select id="weather">' +
'<option value="15"  __W15__>Every 15 minutes</option>' +
'<option value="30"  __W30__>Every 30 minutes</option>' +
'<option value="60"  __W60__>Every hour</option>' +
'<option value="120" __W120__>Every 2 hours</option>' +
'<option value="360" __W360__>Every 6 hours</option>' +
'</select></div>' +

'<div class="field">' +
'<label>Battery Stats</label>' +
'<div class="metric"><span class="name">Time since last charge</span>' +
'<span class="value">__BAT_SINCE__</span></div>' +
'<div class="metric"><span class="name">Time remaining</span>' +
'<span class="value">__BAT_EST__</span></div>' +
'<div class="metric"><span class="name">Next charge</span>' +
'<span class="value">__BAT_NEXT__</span></div>' +
'<div class="metric"><span class="name">Discharge rate per hour</span>' +
'<span class="value">__BAT_RATE__</span></div>' +
'<div class="metric"><span class="name">Discharge rate per day</span>' +
'<span class="value">__BAT_RATE_DAY__</span></div>' +
'<div class="desc">Estimate is learned from your usage; allow a day of wear before it stabilizes.</div>' +
'</div>' +

'<div class="field">' +
'<label>About</label>' +
'<div class="desc" style="font-size:1em;color:#222;margin-top:4px">Pebble-inimal v__APP_VERSION__</div>' +
'</div>' +

'<button onclick="save()">Save</button>' +

'<script>' +
'var t=document.getElementById("nightMode");' +
'var opts=document.getElementById("nightOptions");' +
'function sync(){opts.className=t.checked?"":"hidden";}' +
't.addEventListener("change",sync);' +

'function hourFromTime(v){return parseInt((v||"0").split(":")[0],10)||0;}' +
'function save(){' +
'var d={NIGHT_MODE_ENABLED:t.checked?1:0,' +
'NIGHT_START_HOUR:hourFromTime(document.getElementById("nightStart").value),' +
'NIGHT_END_HOUR:hourFromTime(document.getElementById("nightEnd").value),' +
'NIGHT_UPDATE_INTERVAL:parseInt(document.getElementById("nightUpdate").value,10),' +
'FACE_MODE:parseInt(document.getElementById("faceMode").value,10),' +
'WEATHER_INTERVAL:parseInt(document.getElementById("weather").value,10)};' +
'var bf=document.getElementById("backlightField");' +
'if(bf&&bf.className.indexOf("hidden")<0){' +
'd.BACKLIGHT_COLOR=parseInt(document.getElementById("backlight").value,10);}' +
'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(d));}' +
'</script></body></html>';

function getStoredSettings() {
    return {
        nightMode:  localStorage.getItem('nightMode')  === '1',  // default false
        nightStart: parseInt(localStorage.getItem('nightStart') || '0', 10),
        nightEnd:   parseInt(localStorage.getItem('nightEnd')   || '6', 10),
        nightUpdate: validNightUpdate(parseInt(localStorage.getItem('nightUpdate') || '5', 10)),
        weather:    parseInt(localStorage.getItem('weather')    || '30', 10),
        faceMode:   parseInt(localStorage.getItem('faceMode')   || '1', 10),
        backlight:  parseInt(localStorage.getItem('backlight')  || '0', 10)
    };
}

// Backlight colour is an Emery-only (Pebble Time 2) feature — RGB backlight.
function isEmery() {
    try {
        var info = Pebble.getActiveWatchInfo && Pebble.getActiveWatchInfo();
        return !!(info && info.platform === 'emery');
    } catch (e) {
        return false;
    }
}

function pad2(n) { return (n < 10 ? '0' : '') + n; }

function validNightUpdate(n) {
    return (n === 3 || n === 5 || n === 10 || n === 15) ? n : 5;
}

// Parse a "Xd Yh" or "Yh Zm" duration string into total hours.
// Returns null if the string is not a parseable estimate.
function parseDurationHours(s) {
    if (!s) return null;
    var d = s.match(/(\d+)d/);
    var h = s.match(/(\d+)h/);
    var m = s.match(/(\d+)m/);
    if (!d && !h && !m) return null;
    return (d ? parseInt(d[1], 10) * 24 : 0) +
           (h ? parseInt(h[1], 10) : 0) +
           (m ? parseInt(m[1], 10) / 60 : 0);
}

function getBatteryDisplay() {
    var est = localStorage.getItem('batteryEstimate');
    var since = localStorage.getItem('batterySinceCharge');
    var rateMilli = parseInt(localStorage.getItem('batteryRateMilli') || '0', 10);
    var rateStr, rateDayStr;
    if (!rateMilli || rateMilli <= 0) {
        rateStr = '—';
        rateDayStr = '—';
    } else {
        rateStr = (rateMilli / 1000).toFixed(2) + '%';
        rateDayStr = (rateMilli * 24 / 1000).toFixed(1) + '%';
    }

    // Next-charge date: today + remaining hours from the estimate string.
    var nextStr = '—';
    var hours = parseDurationHours(est);
    if (hours !== null) {
        var when = new Date(Date.now() + hours * 3600 * 1000);
        nextStr = when.getDate() + '/' + (when.getMonth() + 1);
    }

    if (!est || est === '') est = '—';
    if (!since || since === '') since = '—';
    return {
        est: est, since: since, rate: rateStr,
        rateDay: rateDayStr, next: nextStr
    };
}

function buildConfigUrl() {
    var s = getStoredSettings();
    var b = getBatteryDisplay();
    var html = CONFIG_HTML
        .replace('__NIGHT_CHECKED__',    s.nightMode ? 'checked' : '')
        .replace('__NIGHT_OPTS_CLASS__', s.nightMode ? '' : 'hidden')
        .replace('__NIGHT_START__',      pad2(s.nightStart) + ':00')
        .replace('__NIGHT_END__',        pad2(s.nightEnd)   + ':00')
        .replace('__NU3__',  s.nightUpdate === 3  ? 'selected' : '')
        .replace('__NU5__',  s.nightUpdate === 5  ? 'selected' : '')
        .replace('__NU10__', s.nightUpdate === 10 ? 'selected' : '')
        .replace('__NU15__', s.nightUpdate === 15 ? 'selected' : '')
        .replace('__FM0__',  s.faceMode === 0 ? 'selected' : '')
        .replace('__FM1__',  s.faceMode === 1 ? 'selected' : '')
        .replace('__W15__',  s.weather === 15  ? 'selected' : '')
        .replace('__W30__',  s.weather === 30  ? 'selected' : '')
        .replace('__W60__',  s.weather === 60  ? 'selected' : '')
        .replace('__W120__', s.weather === 120 ? 'selected' : '')
        .replace('__W360__', s.weather === 360 ? 'selected' : '')
        .replace('__BAT_SINCE__', b.since)
        .replace('__BAT_EST__',  b.est)
        .replace('__BAT_NEXT__', b.next)
        .replace('__BAT_RATE__', b.rate)
        .replace('__BAT_RATE_DAY__', b.rateDay)
        .replace('__BACKLIGHT_FIELD_CLASS__', isEmery() ? '' : 'hidden')
        .replace('__BL0__', s.backlight === 0 ? 'selected' : '')
        .replace('__BL1__', s.backlight === 1 ? 'selected' : '')
        .replace('__BL2__', s.backlight === 2 ? 'selected' : '')
        .replace('__BL3__', s.backlight === 3 ? 'selected' : '')
        .replace('__BL4__', s.backlight === 4 ? 'selected' : '')
        .replace('__BL5__', s.backlight === 5 ? 'selected' : '')
        .replace('__BL6__', s.backlight === 6 ? 'selected' : '')
        .replace('__APP_VERSION__', APP_VERSION);
    return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

Pebble.addEventListener('showConfiguration', function () {
    // Ask the watch for a fresh estimate, then defer opening the page until it
    // arrives via 'appmessage' (see openConfigWhenReady). A timeout fallback
    // opens with cached values if the watch is slow or disconnected, so the
    // page always opens promptly.
    configOpenPending = true;
    if (configOpenTimer !== null) { clearTimeout(configOpenTimer); }
    configOpenTimer = setTimeout(function () {
        configOpenTimer = null;
        openConfigWhenReady();
    }, 1500);

    Pebble.sendAppMessage({ 'REQUEST_BATTERY_INFO': 1 },
        function ()  { console.log('Battery info requested'); },
        function (e) {
            console.log('Battery info request failed: ' + JSON.stringify(e));
            // Watch unreachable — open immediately with cached values.
            openConfigWhenReady();
        }
    );
});

Pebble.addEventListener('webviewclosed', function (e) {
    if (!e || !e.response) { return; }
    try {
        var settings = JSON.parse(decodeURIComponent(e.response));
        // Cache locally so the page pre-fills correctly on next open
        localStorage.setItem('nightMode',  settings.NIGHT_MODE_ENABLED ? '1' : '0');
        localStorage.setItem('nightStart', String(settings.NIGHT_START_HOUR));
        localStorage.setItem('nightEnd',   String(settings.NIGHT_END_HOUR));
        localStorage.setItem('nightUpdate', String(settings.NIGHT_UPDATE_INTERVAL));
        localStorage.setItem('weather',    String(settings.WEATHER_INTERVAL));
        localStorage.setItem('faceMode',   String(settings.FACE_MODE || 0));
        if (typeof settings.BACKLIGHT_COLOR !== 'undefined') {
            localStorage.setItem('backlight', String(settings.BACKLIGHT_COLOR));
        }

        Pebble.sendAppMessage(settings,
            function ()  { console.log('Settings sent: ' + JSON.stringify(settings)); },
            function (er) { console.log('Settings send failed: ' + JSON.stringify(er)); }
        );
    } catch (err) {
        console.log('Settings parse error: ' + err);
    }
});
