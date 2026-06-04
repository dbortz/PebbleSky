// PebbleSky Detail — companion PebbleKit JS.
// Fetches the fuller forecast (current + 5-day daily + hourly probability +
// minutely precip) from Open-Meteo and pushes it to the app over AppMessage.
// (Tomorrow.io parity + Clay settings for the companion are a later pass.)

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
function precipToLevel(inches) {
  if (!inches || inches <= 0) return 0;
  if (inches < 0.01) return 1;
  if (inches < 0.03) return 2;
  if (inches < 0.08) return 3;
  return 4;
}
function clamp8(n) { n = Math.round(n); return n < 0 ? 0 : (n > 255 ? 255 : n); }

function fetchWeather(lat, lon) {
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
      var temp = Math.round(cur.temperature_2m);
      var high = Math.round(d.daily.temperature_2m_max[0]);
      var icon = wmoToIcon(cur.weather_code, cur.is_day === 1);

      // 16x15-min precip buffer + anchor (same model as the face).
      var mt = d.minutely_15.time, mp = d.minutely_15.precipitation;
      var nowMs = Date.now(), start = 0;
      for (var i = 0; i < mt.length; i++) {
        if (new Date(mt[i]).getTime() + 15 * 60 * 1000 > nowMs) { start = i; break; }
      }
      var buf = [];
      for (var j = 0; j < 16; j++) buf.push(precipToLevel(mp[start + j]));
      var anchor = Math.floor(new Date(mt[start]).getTime() / 1000);

      // Probability at NOW, +30, +60, +90, +120 (sample the hour buckets).
      var ht = d.hourly.time, hp = d.hourly.precipitation_probability;
      function probAt(offMin) {
        var target = nowMs + offMin * 60 * 1000, best = 0;
        for (var k = 0; k < ht.length; k++) {
          if (new Date(ht[k]).getTime() <= target) best = hp[k] || 0; else break;
        }
        return clamp8(best);
      }
      var probs = [probAt(0), probAt(30), probAt(60), probAt(90), probAt(120)];

      // 5-day forecast: [icon, hi+50, lo+50] per day.
      var daily = [];
      for (var n = 0; n < 5 && n < d.daily.time.length; n++) {
        daily.push(wmoToIcon(d.daily.weather_code[n], true));
        daily.push(clamp8(Math.round(d.daily.temperature_2m_max[n]) + 50));
        daily.push(clamp8(Math.round(d.daily.temperature_2m_min[n]) + 50));
      }

      Pebble.sendAppMessage({
        WX_TEMP: temp, WX_HIGH: high, WX_ICON: icon, WX_ANCHOR: anchor,
        WX_PRECIP: buf, WX_PROBS: probs, WX_DAILY: daily,
        WX_FEELS: Math.round(cur.apparent_temperature),
        WX_WIND: Math.round(cur.wind_speed_10m),
        WX_HUMID: Math.round(cur.relative_humidity_2m)
      },
      function () { console.log('PebbleSky Detail: sent forecast'); },
      function (e) { console.log('PebbleSky Detail: send failed: ' + JSON.stringify(e)); });
    } catch (e) { console.log('PebbleSky Detail: parse error: ' + e); }
  };
  xhr.onerror = function () { console.log('PebbleSky Detail: fetch error'); };
  xhr.open('GET', url);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function (err) { console.log('PebbleSky Detail: geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () { getWeather(); });
Pebble.addEventListener('appmessage', function () { getWeather(); });
