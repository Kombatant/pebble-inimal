// =============================================================================
// PebbleKit JS — runs on the phone, fetches weather from Open-Meteo,
// and sends the temperature back to the watch via AppMessage.
// =============================================================================

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
                function ()  { console.log('Weather sent: ' + temperature + 'C, code ' + weather_code); },
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
        { timeout: 15000, maximumAge: 60000 }
    );
}

Pebble.addEventListener('ready', function () {
    console.log('PebbleKit JS ready');
    getWeather();
});

Pebble.addEventListener('appmessage', function (e) {
    if (e.payload.REQUEST_WEATHER) {
        getWeather();
    }
});

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

'<div class="field">' +
'<div class="toggle">' +
'<input type="checkbox" id="nightMode" __NIGHT_CHECKED__>' +
'<label for="nightMode">Night idle mode</label></div>' +
'<div class="desc">After 20 minutes without wrist movement during your night ' +
'window, the watchface drops to a 5-minute update cadence to save battery. ' +
'Shaking your wrist wakes it back up immediately.</div>' +
'<div id="nightOptions" class="__NIGHT_OPTS_CLASS__">' +
'<div class="row">' +
'<div><label for="nightStart">Start</label>' +
'<input type="time" id="nightStart" value="__NIGHT_START__" step="3600"></div>' +
'<div><label for="nightEnd">End</label>' +
'<input type="time" id="nightEnd" value="__NIGHT_END__" step="3600"></div>' +
'</div></div></div>' +

'<div class="field">' +
'<label for="weather">Weather refresh</label>' +
'<select id="weather">' +
'<option value="15"  __W15__>Every 15 minutes</option>' +
'<option value="30"  __W30__>Every 30 minutes</option>' +
'<option value="60"  __W60__>Every hour</option>' +
'<option value="120" __W120__>Every 2 hours</option>' +
'<option value="360" __W360__>Every 6 hours</option>' +
'</select></div>' +

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
'FACE_MODE:parseInt(document.getElementById("faceMode").value,10),' +
'WEATHER_INTERVAL:parseInt(document.getElementById("weather").value,10)};' +
'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(d));}' +
'</script></body></html>';

function getStoredSettings() {
    return {
        nightMode:  localStorage.getItem('nightMode')  === '1',  // default false
        nightStart: parseInt(localStorage.getItem('nightStart') || '0', 10),
        nightEnd:   parseInt(localStorage.getItem('nightEnd')   || '6', 10),
        weather:    parseInt(localStorage.getItem('weather')    || '30', 10),
        faceMode:   parseInt(localStorage.getItem('faceMode')   || '1', 10)
    };
}

function pad2(n) { return (n < 10 ? '0' : '') + n; }

function buildConfigUrl() {
    var s = getStoredSettings();
    var html = CONFIG_HTML
        .replace('__NIGHT_CHECKED__',    s.nightMode ? 'checked' : '')
        .replace('__NIGHT_OPTS_CLASS__', s.nightMode ? '' : 'hidden')
        .replace('__NIGHT_START__',      pad2(s.nightStart) + ':00')
        .replace('__NIGHT_END__',        pad2(s.nightEnd)   + ':00')
        .replace('__FM0__',  s.faceMode === 0 ? 'selected' : '')
        .replace('__FM1__',  s.faceMode === 1 ? 'selected' : '')
        .replace('__W15__',  s.weather === 15  ? 'selected' : '')
        .replace('__W30__',  s.weather === 30  ? 'selected' : '')
        .replace('__W60__',  s.weather === 60  ? 'selected' : '')
        .replace('__W120__', s.weather === 120 ? 'selected' : '')
        .replace('__W360__', s.weather === 360 ? 'selected' : '');
    return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

Pebble.addEventListener('showConfiguration', function () {
    Pebble.openURL(buildConfigUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
    if (!e || !e.response) { return; }
    try {
        var settings = JSON.parse(decodeURIComponent(e.response));
        // Cache locally so the page pre-fills correctly on next open
        localStorage.setItem('nightMode',  settings.NIGHT_MODE_ENABLED ? '1' : '0');
        localStorage.setItem('nightStart', String(settings.NIGHT_START_HOUR));
        localStorage.setItem('nightEnd',   String(settings.NIGHT_END_HOUR));
        localStorage.setItem('weather',    String(settings.WEATHER_INTERVAL));
        localStorage.setItem('faceMode',   String(settings.FACE_MODE || 0));

        Pebble.sendAppMessage(settings,
            function ()  { console.log('Settings sent: ' + JSON.stringify(settings)); },
            function (er) { console.log('Settings send failed: ' + JSON.stringify(er)); }
        );
    } catch (err) {
        console.log('Settings parse error: ' + err);
    }
});
