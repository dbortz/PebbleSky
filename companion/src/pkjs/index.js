// PebbleSky Detail — companion PebbleKit JS.
// Fetches the fuller forecast (current + 5-day daily + precip probability +
// minutely precip) and pushes it to the app over AppMessage. Two providers,
// chosen in Clay: Open-Meteo (free) and Tomorrow.io (needs key). Both normalize
// to the same model the app consumes.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function precipToLevel(inches) {
  if (!inches || inches <= 0) return 0;
  if (inches < 0.01) return 1;
  if (inches < 0.03) return 2;
  if (inches < 0.08) return 3;
  return 4;
}
function clamp8(n) { n = Math.round(n); return n < 0 ? 0 : (n > 255 ? 255 : n); }
function isDayNow() { var h = new Date().getHours(); return h >= 6 && h < 19; }

// Strip diacritics + any non-ASCII so the subsetted watch font can render it.
function asciiNormalize(s) {
  try { s = s.normalize('NFD'); } catch (e) { /* older JS */ }
  return s.replace(/[^\x20-\x7E]/g, '').trim();
}

// Reverse-geocode lat/lon to a city/locality name (free, no API key) and send
// it to the watch as WX_LOC. Independent of the weather provider.
function reverseGeocode(lat, lon) {
  var url = 'https://api.bigdatacloud.net/data/reverse-geocode-client?latitude='
    + lat + '&longitude=' + lon + '&localityLanguage=en';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      var name = asciiNormalize(d.city || d.locality || d.principalSubdivision || '');
      if (name) Pebble.sendAppMessage({ WX_LOC: name },
        function () { console.log('PebbleSky Detail: location ' + name); },
        function (e) { console.log('PebbleSky Detail: loc send failed'); });
    } catch (e) { console.log('PebbleSky Detail: geocode parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky Detail: geocode fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

function send(m) {
  Pebble.sendAppMessage(m,
    function () { console.log('PebbleSky Detail: sent forecast'); },
    function (e) { console.log('PebbleSky Detail: send failed: ' + JSON.stringify(e)); });
}

// ---- Open-Meteo ------------------------------------------------------------
function wmoToIcon(code, isDay) {
  if (code === 0) return isDay ? 0 : 1;
  if (code === 1 || code === 2) return isDay ? 2 : 3;
  if (code === 3) return 4;
  if (code === 45 || code === 48) return 5;
  if (code >= 51 && code <= 57) return 6;
  if (code === 66 || code === 67) return 10;
  if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) return 7;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 9;
  if (code === 95 || code === 96 || code === 99) return 8;
  return 4;
}
function fetchOpenMeteo(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast'
    + '?latitude=' + lat + '&longitude=' + lon
    + '&current=temperature_2m,weather_code,is_day,apparent_temperature,relative_humidity_2m,wind_speed_10m'
    + '&daily=temperature_2m_max,temperature_2m_min,weather_code'
    + '&hourly=precipitation_probability'
    + '&minutely_15=precipitation'
    + '&temperature_unit=fahrenheit&precipitation_unit=inch&wind_speed_unit=mph'
    + '&timezone=auto&forecast_days=5';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      var cur = d.current;
      var mt = d.minutely_15.time, mp = d.minutely_15.precipitation;
      var nowMs = Date.now(), start = 0;
      for (var i = 0; i < mt.length; i++) {
        if (new Date(mt[i]).getTime() + 15 * 60 * 1000 > nowMs) { start = i; break; }
      }
      var buf = [];
      for (var j = 0; j < 16; j++) buf.push(precipToLevel(mp[start + j]));
      var anchor = Math.floor(new Date(mt[start]).getTime() / 1000);

      var ht = d.hourly.time, hp = d.hourly.precipitation_probability;
      function probAt(off) {
        var target = nowMs + off * 60000, best = 0;
        for (var k = 0; k < ht.length; k++) {
          if (new Date(ht[k]).getTime() <= target) best = hp[k] || 0; else break;
        }
        return clamp8(best);
      }
      var probs = [probAt(0), probAt(30), probAt(60), probAt(90), probAt(120)];

      var daily = [];
      for (var n = 0; n < 5 && n < d.daily.time.length; n++) {
        daily.push(wmoToIcon(d.daily.weather_code[n], true));
        daily.push(clamp8(Math.round(d.daily.temperature_2m_max[n]) + 50));
        daily.push(clamp8(Math.round(d.daily.temperature_2m_min[n]) + 50));
      }
      send({
        WX_TEMP: Math.round(cur.temperature_2m),
        WX_HIGH: Math.round(d.daily.temperature_2m_max[0]),
        WX_ICON: wmoToIcon(cur.weather_code, cur.is_day === 1),
        WX_ANCHOR: anchor, WX_PRECIP: buf, WX_PROBS: probs, WX_DAILY: daily,
        WX_FEELS: Math.round(cur.apparent_temperature),
        WX_WIND: Math.round(cur.wind_speed_10m),
        WX_HUMID: Math.round(cur.relative_humidity_2m)
      });
    } catch (e) { console.log('PebbleSky Detail: open-meteo parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky Detail: open-meteo fetch error'); };
  xhr.open('GET', url); xhr.send();
}

// ---- Tomorrow.io -----------------------------------------------------------
function tomorrowToIcon(code, isDay) {
  switch (code) {
    case 1000: return isDay ? 0 : 1;
    case 1100: case 1101: return isDay ? 2 : 3;
    case 1102: case 1001: return 4;
    case 2000: case 2100: return 5;
    case 4000: case 4200: return 6;
    case 4001: case 4201: return 7;
    case 5000: case 5001: case 5100: case 5101: return 9;
    case 6000: case 6001: case 6200: return 10;
    case 7000: case 7101: case 7102: return 10;
    case 8000: return 8;
    default: return 4;
  }
}
function fetchTomorrow(lat, lon, key) {
  var url = 'https://api.tomorrow.io/v4/weather/forecast'
    + '?location=' + lat + ',' + lon
    + '&timesteps=1m,1d&units=imperial&apikey=' + encodeURIComponent(key);
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      var minutely = d.timelines.minutely, day = d.timelines.daily;
      var cur = minutely[0].values;
      var anchor = Math.floor(new Date(minutely[0].time).getTime() / 1000);

      var buf = [];
      for (var b = 0; b < 16; b++) {
        var maxI = 0;
        for (var k = 0; k < 15; k++) {
          var m = minutely[b * 15 + k];
          if (m && m.values && m.values.precipitationIntensity > maxI) maxI = m.values.precipitationIntensity;
        }
        buf.push(precipToLevel(maxI * 0.25));
      }
      function probAt(off) {
        var m = minutely[off]; // 1-min steps: index == minutes offset
        return clamp8(m && m.values ? (m.values.precipitationProbability || 0) : 0);
      }
      var probs = [probAt(0), probAt(30), probAt(60), probAt(90), probAt(120)];

      var daily = [];
      for (var n = 0; n < 5 && n < day.length; n++) {
        var dv = day[n].values;
        daily.push(tomorrowToIcon(dv.weatherCodeMax || dv.weatherCode, true));
        daily.push(clamp8(Math.round(dv.temperatureMax) + 50));
        daily.push(clamp8(Math.round(dv.temperatureMin) + 50));
      }
      send({
        WX_TEMP: Math.round(cur.temperature),
        WX_HIGH: Math.round(day[0].values.temperatureMax),
        WX_ICON: tomorrowToIcon(cur.weatherCode, isDayNow()),
        WX_ANCHOR: anchor, WX_PRECIP: buf, WX_PROBS: probs, WX_DAILY: daily,
        WX_FEELS: Math.round(cur.temperatureApparent),
        WX_WIND: Math.round(cur.windSpeed),
        WX_HUMID: Math.round(cur.humidity)
      });
    } catch (e) { console.log('PebbleSky Detail: tomorrow.io parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky Detail: tomorrow.io fetch error'); };
  xhr.open('GET', url); xhr.send();
}

function getWeather() {
  var s = {};
  try { s = clay.getSettings() || {}; } catch (e) { /* defaults */ }
  var provider = s.CFG_PROVIDER || 'openmeteo';
  var key = s.CFG_APIKEY || '';
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      var lat = pos.coords.latitude, lon = pos.coords.longitude;
      if (provider === 'tomorrowio' && key) fetchTomorrow(lat, lon, key);
      else fetchOpenMeteo(lat, lon);
      reverseGeocode(lat, lon);  // location label (independent of weather fetch)
    },
    function (err) { console.log('PebbleSky Detail: geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () { getWeather(); });
Pebble.addEventListener('appmessage', function () { getWeather(); });
Pebble.addEventListener('webviewclosed', function (e) { if (e && e.response) setTimeout(getWeather, 800); });
