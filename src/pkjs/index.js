// PebbleSky — PebbleKit JS weather component.
//
// Fetches a hyperlocal forecast on the phone and pushes a compact, canonical
// model to the watch over AppMessage. This pass implements the Open-Meteo
// provider (free, no API key). The Tomorrow.io provider + Clay-configurable
// selection are a later pass; the watch is provider-agnostic.

var REFRESH_MS = 30 * 60 * 1000; // re-fetch every 30 min while connected

// WMO weather code -> icon index (matches the C dispatcher / icons.js ORDER):
// 0 clear-day 1 clear-night 2 pc-day 3 pc-night 4 cloudy 5 fog 6 drizzle
// 7 rain 8 thunder 9 snow 10 sleet 11 wind
function wmoToIcon(code, isDay) {
  if (code === 0) return isDay ? 0 : 1;
  if (code === 1 || code === 2) return isDay ? 2 : 3;
  if (code === 3) return 4;
  if (code === 45 || code === 48) return 5;
  if (code >= 51 && code <= 57) return 6;
  if (code === 66 || code === 67) return 10;          // freezing rain -> sleet
  if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82)) return 7;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 9;
  if (code === 95 || code === 96 || code === 99) return 8;
  return 4;
}

// 15-min precip total (inches) -> intensity level 0..4.
function precipToLevel(inches) {
  if (!inches || inches <= 0)  return 0;
  if (inches < 0.01) return 1;
  if (inches < 0.03) return 2;
  if (inches < 0.08) return 3;
  return 4;
}

function fetchWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast'
    + '?latitude=' + lat + '&longitude=' + lon
    + '&current=temperature_2m,weather_code,is_day'
    + '&daily=temperature_2m_max'
    + '&minutely_15=precipitation'
    + '&temperature_unit=fahrenheit&precipitation_unit=inch'
    + '&timezone=auto&forecast_days=1';

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      var temp = Math.round(d.current.temperature_2m);
      var high = Math.round(d.daily.temperature_2m_max[0]);
      var icon = wmoToIcon(d.current.weather_code, d.current.is_day === 1);

      // Build the next-2h precip strip: 8 x 15-min buckets from "now".
      var times = d.minutely_15.time;
      var precs = d.minutely_15.precipitation;
      var nowMs = Date.now();
      var start = 0;
      for (var i = 0; i < times.length; i++) {
        if (new Date(times[i]).getTime() + 15 * 60 * 1000 > nowMs) { start = i; break; }
      }
      var levels = [];
      for (var j = 0; j < 8; j++) levels.push(precipToLevel(precs[start + j]));

      // Derive rain state + warning headline.
      var rain = false, warn = '';
      if (levels[0] > 0) {
        var clearAt = -1;
        for (var m = 1; m < 8; m++) { if (levels[m] === 0) { clearAt = m; break; } }
        rain = true;
        warn = clearAt >= 0 ? ('CLEARING IN ' + (clearAt * 15) + 'M') : 'RAIN NEXT 2H';
      } else {
        for (var k = 1; k < 8; k++) {
          if (levels[k] > 0) { rain = true; warn = 'RAIN IN ' + (k * 15) + 'M'; break; }
        }
      }

      Pebble.sendAppMessage({
        WX_TEMP: temp,
        WX_HIGH: high,
        WX_ICON: icon,
        WX_RAIN: rain ? 1 : 0,
        WX_WARN: warn,
        WX_PRECIP: levels
      },
      function () { console.log('PebbleSky: weather sent (' + temp + 'F, icon ' + icon + ')'); },
      function (e) { console.log('PebbleSky: send failed: ' + JSON.stringify(e)); });
    } catch (e) {
      console.log('PebbleSky: parse error: ' + e);
    }
  };
  xhr.onerror = function () { console.log('PebbleSky: fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function (err) { console.log('PebbleSky: geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () {
  getWeather();
  setInterval(getWeather, REFRESH_MS);
});

// Allow the watch to request a refresh (e.g. on launch / flick).
Pebble.addEventListener('appmessage', function () { getWeather(); });
