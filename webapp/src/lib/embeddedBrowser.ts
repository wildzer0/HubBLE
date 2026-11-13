/**
 * @file embeddedBrowser.ts
 * @description Detects the small browser that phones open for "sign in to the Wi-Fi network" (Android's captive-portal
 * WebView, iOS's captive-network assistant). Those windows cannot open the file chooser or download files, so the pages
 * that need them say so and point to the real browser.
 */

/**
 * Whether a user-agent string looks like a captive-portal window or another embedded WebView.
 *
 * @param ua The user-agent string.
 * @returns true for an Android WebView (`; wv)`) or the iOS captive-network assistant.
 */
export function isLimitedBrowser(ua: string): boolean {
  return /;\s*wv\)/i.test(ua) || /CaptiveNetworkSupport/i.test(ua);
}
