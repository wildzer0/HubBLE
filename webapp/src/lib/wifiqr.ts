/**
 * @file wifiqr.ts
 * @description Builds the payload of the Wi-Fi QR code (the de-facto "WIFI:" format understood by the
 * iOS and Android camera apps) and renders any text as an SVG QR code for the printable sticker.
 */
import qrcode from 'qrcode-generator';

/**
 * Escape a field of the `WIFI:` payload: backslash, semicolon, comma, colon and double quote must be
 * prefixed with a backslash.
 *
 * @param v Raw SSID or password.
 * @returns The escaped text.
 */
export function wifiEscape(v: string): string {
  return v.replace(/[\\;,:"]/g, (c) => `\\${c}`);
}

/**
 * Build the `WIFI:` payload for a WPA2 network.
 *
 * @param ssid Network name.
 * @param password WPA2 passphrase.
 * @returns Text to encode in the QR code.
 */
export function wifiPayload(ssid: string, password: string): string {
  return `WIFI:T:WPA;S:${wifiEscape(ssid)};P:${wifiEscape(password)};;`;
}

/** Quiet zone (in modules) around the code, as required by the QR specification. */
const QUIET_ZONE = 4;

/**
 * Render text as an SVG QR code (error correction M, automatic version).
 *
 * @param text Text to encode.
 * @returns A standalone `<svg>` markup string with `shape-rendering="crispEdges"`.
 */
export function qrSvg(text: string): string {
  const qr = qrcode(0, 'M');
  qr.addData(text);
  qr.make();
  const n = qr.getModuleCount();
  const size = n + 2 * QUIET_ZONE;
  let path = '';
  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      if (qr.isDark(y, x)) path += `M${String(x + QUIET_ZONE)} ${String(y + QUIET_ZONE)}h1v1h-1z`;
    }
  }
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${String(size)} ${String(size)}" ` +
    `shape-rendering="crispEdges" role="img"><rect width="${String(size)}" height="${String(size)}" fill="#fff"/>` +
    `<path d="${path}" fill="#000"/></svg>`
  );
}
