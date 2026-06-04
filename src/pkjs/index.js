// PebbleSky — PebbleKit JS weather component.
//
// Fetches a hyperlocal forecast on the phone and pushes a compact, canonical
// model to the watch over AppMessage. Two providers are supported and chosen in
// the Clay settings: Open-Meteo (free, no key) and Tomorrow.io (needs an API
// key). The watch is provider-agnostic — both normalize to the same model:
// { temp, high, icon, anchor(epoch sec of bucket0), precip[16] (0-4) }.

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var REFRESH_MS = 30 * 60 * 1000; // re-fetch every 30 min while connected

// 15-min precip total (inches) -> intensity level 0..4 (shared by both providers).
function precipToLevel(inches) {
  if (!inches || inches <= 0) return 0;
  if (inches < 0.01) return 1;
  if (inches < 0.03) return 2;
  if (inches < 0.08) return 3;
  return 4;
}

function isDayNow() {
  var h = new Date().getHours();
  return h >= 6 && h < 19;
}

// Push the normalized model to the watch.
function sendModel(temp, high, icon, anchorSec, buf) {
  Pebble.sendAppMessage(
    { WX_TEMP: temp, WX_HIGH: high, WX_ICON: icon, WX_ANCHOR: anchorSec, WX_PRECIP: buf },
    function () { console.log('PebbleSky: sent ' + temp + 'F icon ' + icon + ' (' + buf.length + ' buckets)'); },
    function (e) { console.log('PebbleSky: send failed: ' + JSON.stringify(e)); }
  );
}

// ---- Open-Meteo (WMO codes; minutely_15 precipitation in inches) -----------
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
    + '&current=temperature_2m,weather_code,is_day'
    + '&daily=temperature_2m_max'
    + '&minutely_15=precipitation'
    + '&temperature_unit=fahrenheit&precipitation_unit=inch'
    + '&timezone=auto&forecast_days=2';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      var temp = Math.round(d.current.temperature_2m);
      var high = Math.round(d.daily.temperature_2m_max[0]);
      var icon = wmoToIcon(d.current.weather_code, d.current.is_day === 1);

      var times = d.minutely_15.time;
      var precs = d.minutely_15.precipitation;
      var nowMs = Date.now();
      var start = 0;
      for (var i = 0; i < times.length; i++) {
        if (new Date(times[i]).getTime() + 15 * 60 * 1000 > nowMs) { start = i; break; }
      }
      var buf = [];
      for (var j = 0; j < 16; j++) buf.push(precipToLevel(precs[start + j]));
      var anchor = Math.floor(new Date(times[start]).getTime() / 1000);
      sendModel(temp, high, icon, anchor, buf);
    } catch (e) { console.log('PebbleSky: open-meteo parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky: open-meteo fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

// ---- Tomorrow.io (own weather codes; minutely precipitationIntensity in/hr) -
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
      var minutely = d.timelines.minutely;
      var daily = d.timelines.daily;
      var cur = minutely[0].values;
      var temp = Math.round(cur.temperature);
      var high = Math.round(daily[0].values.temperatureMax);
      var icon = tomorrowToIcon(cur.weatherCode, isDayNow());
      var anchor = Math.floor(new Date(minutely[0].time).getTime() / 1000);

      // Aggregate 1-min intensities into 16 x 15-min buckets (max in/hr -> inches).
      var buf = [];
      for (var b = 0; b < 16; b++) {
        var maxI = 0;
        for (var k = 0; k < 15; k++) {
          var m = minutely[b * 15 + k];
          if (m && m.values && m.values.precipitationIntensity > maxI) {
            maxI = m.values.precipitationIntensity;
          }
        }
        buf.push(precipToLevel(maxI * 0.25)); // in/hr over 15 min -> inches
      }
      sendModel(temp, high, icon, anchor, buf);
    } catch (e) { console.log('PebbleSky: tomorrow.io parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky: tomorrow.io fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

// ---- Provider dispatch -----------------------------------------------------
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
    },
    function (err) { console.log('PebbleSky: geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () {
  getWeather();
  setInterval(getWeather, REFRESH_MS);
});

// Watch-initiated refresh (e.g. on launch).
Pebble.addEventListener('appmessage', function () { getWeather(); });

// Re-fetch after the user saves settings (provider/key/units may have changed).
// Clay's own webviewclosed handler runs too and pushes the format/color settings
// to the watch; we just trigger a fresh forecast.
Pebble.addEventListener('webviewclosed', function (e) {
  if (e && e.response) setTimeout(getWeather, 800);
});
