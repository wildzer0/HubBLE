import { describe, expect, it } from 'vitest';
import { isLimitedBrowser } from '../../src/lib/embeddedBrowser';

describe('limited browser detection', () => {
  it('recognises an Android WebView and the iOS captive-network assistant', () => {
    expect(
      isLimitedBrowser(
        'Mozilla/5.0 (Linux; Android 14; Pixel 7 Build/UP1A; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/120.0 Mobile Safari/537.36',
      ),
    ).toBe(true);
    expect(isLimitedBrowser('CaptiveNetworkSupport-355.30.1 wispr')).toBe(true);
  });
  it('does not flag normal browsers', () => {
    expect(
      isLimitedBrowser(
        'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Mobile Safari/537.36',
      ),
    ).toBe(false);
    expect(
      isLimitedBrowser('Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0 Safari/537.36'),
    ).toBe(false);
  });
});
