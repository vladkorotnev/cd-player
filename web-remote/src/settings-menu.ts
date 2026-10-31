export type SettingField = {
  kind: 'field';
  label: string;
  key: string;
  help?: string;
  options?: readonly { label: string; value: number }[];
};

export type SettingGroup = {
  kind: 'group';
  label: string;
  items: readonly SettingNode[];
  help?: string;
  presetIndex?: number;
};

export type SettingNode = SettingField | SettingGroup;

const field = (label: string, key: string, help?: string): SettingField =>
  ({ kind: 'field', label, key, help });
const group = (label: string, items: readonly SettingNode[], help?: string): SettingGroup =>
  ({ kind: 'group', label, items, help });

const presets: SettingGroup[] = Array.from({ length: 6 }, (_, index) => ({
  kind: 'group',
  label: `Preset ${index + 1}`,
  presetIndex: index,
  items: [
    field('Name', `netr_n${index}`),
    field('URL', `netr_u${index}`)
  ]
}));

/** Editable parts of the device's Settings menu in settings_mode.cpp. */
export const settingsMenu: SettingGroup = group('Settings', [
  group('WiFi', [
    field('Network name', 'wifi_ssid'),
    field('Password', 'wifi_pass')
  ], 'Network selection and connection are available on the device.'),
  group('CD', [
    field('Show Lyrics', 'cd_lyrics'),
    group('Metadata', [
      field('Cache Metadata', 'cd_cache_meta'),
      group('Metadata sources', [
        field('CD Text', 'cd_meta_cdtxt'),
        field('MusicBrainz', 'cd_meta_mb'),
        field('CDDB', 'cd_meta_cddb'),
        field('CDDB server', 'cddb_srv'),
        field('CDDB e-mail', 'cddb_email')
      ]),
      group('Lyrics sources', [
        field('LRCLib', 'cd_meta_lrclib'),
        field('NetEase (163)', 'cd_meta_162'),
        field('QQ Music', 'cd_meta_qq')
      ])
    ]),
    group('Last.FM', [
      field('User name', 'cd_lfm_user'),
      field('Password', 'cd_lfm_pass'),
      field('Enable', 'cd_lastfm')
    ])
  ]),
  group('Radio Stations', presets),
  group('Bluetooth', [
    field('Device Name', 'bt_name'),
    field('Require PIN code', 'bt_use_pin'),
    field('Auto-connect', 'bt_auto_conn')
  ]),
  group('System', [
    {
      kind: 'field', label: 'Language', key: 'lang',
      help: 'Restart the device to apply a language change.',
      options: [
        { label: 'English', value: 0 },
        { label: 'Русский', value: 1 },
        { label: '日本語', value: 2 }
      ]
    },
    group('Mode toggle button', [
      field('CD', 'cd_mode_in'),
      field('Web Radio', 'ra_mode_in'),
      field('Bluetooth', 'bt_mode_in')
    ]),
    group('Developer', [
      field('OTA FVU', 'otafvu_allow'),
      field('Telnet Logging', 'telnet_log')
    ]),
    field('NTP server', 'ntp_srv')
  ])
]);
