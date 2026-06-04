// Clay configuration page for PebbleSky.
// Settings with a messageKey are also sent to the watch; provider/apiKey are
// used phone-side by index.js (the watch ignores them).
module.exports = [
  { type: 'heading', defaultValue: 'PebbleSky' },
  { type: 'text', defaultValue: 'Weather watchface settings.' },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Weather' },
      {
        type: 'select',
        messageKey: 'CFG_PROVIDER',
        label: 'Provider',
        defaultValue: 'openmeteo',
        options: [
          { label: 'Open-Meteo (free, no key)', value: 'openmeteo' },
          { label: 'Tomorrow.io (needs API key)', value: 'tomorrowio' }
        ]
      },
      {
        type: 'input',
        messageKey: 'CFG_APIKEY',
        label: 'Tomorrow.io API key',
        attributes: { placeholder: 'only needed for Tomorrow.io' }
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Format' },
      { type: 'toggle', messageKey: 'CFG_CELSIUS', label: 'Use Celsius (°C)', defaultValue: false },
      { type: 'toggle', messageKey: 'CFG_CLOCK24', label: '24-hour clock', defaultValue: false },
      { type: 'toggle', messageKey: 'CFG_DATEDMY', label: 'Day/Month date order', defaultValue: false }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colors' },
      { type: 'color', messageKey: 'CFG_ACCENT', label: 'Accent', defaultValue: '00aaff', sunlight: false },
      { type: 'color', messageKey: 'CFG_WARNING', label: 'Rain warning', defaultValue: 'ffff00', sunlight: false }
    ]
  },

  { type: 'submit', defaultValue: 'Save' }
];
