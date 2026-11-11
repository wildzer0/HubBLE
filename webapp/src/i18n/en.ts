/** English strings (default language and key source of truth). */
export const en = {
  'app.title': 'HubBLE',
  'status.heading': 'Dongle status',
  'status.loading': 'Loading…',
  'status.error': 'Cannot reach the dongle.',
  'status.version': 'Firmware version',
  'status.board': 'Board',
  'status.usb': 'USB state',
  'lang.label': 'Language',
} as const;

/** Union of all valid translation keys. */
export type MessageKey = keyof typeof en;
