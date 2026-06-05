// Clay config for the PebbleSky Detail companion. Mirrors the watchface's
// weather + color settings (units/provider/key/colors). Note: Pebble apps can't
// share settings storage, so these are configured separately from the face.
module.exports = [
  { type: 'heading', defaultValue: 'PebbleSky Detail' },
  { type: 'text', defaultValue: 'Settings for the detail companion (configured separately from the watchface).' },

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
      },
      { type: 'toggle', messageKey: 'CFG_CELSIUS', label: 'Use Celsius (°C)', defaultValue: false }
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
