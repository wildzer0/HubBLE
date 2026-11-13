/**
 * @file transport.ts
 * @description Where API calls go: the normal `fetch` (Wi-Fi, or the mock in development) or, once a USB link is connected,
 * the link. Everything that talks to the dongle uses {@link apiFetch}, so a page does not know the difference.
 */
import type { UsbLink } from './usbLink';

let usb: UsbLink | null = null;

/**
 * Use (or stop using) a USB link for all API calls.
 *
 * @param link The connected link, or null to go back to `fetch`.
 */
export function setUsbLink(link: UsbLink | null): void {
  usb = link;
}

/**
 * The USB link in use, if any.
 *
 * @returns The link or null.
 */
export function getUsbLink(): UsbLink | null {
  return usb;
}

/**
 * `fetch` for the dongle's API: over the USB link when connected, otherwise the browser's fetch.
 *
 * @param path API path (`/api/v1/...`).
 * @param init Request options (method, headers, body as string).
 * @returns A standard Response.
 */
export async function apiFetch(path: string, init: RequestInit = {}): Promise<Response> {
  if (!usb) return fetch(path, init);
  const headers = new Headers(init.headers);
  const body = typeof init.body === 'string' ? init.body : undefined;
  const r = await usb.request(
    init.method ?? 'GET',
    path,
    body,
    headers.get('Content-Type') ?? undefined,
  );
  // A body is not allowed on 204/304 responses.
  const nullBody = r.status === 204 || r.status === 304;
  return new Response(nullBody ? null : (r.body as unknown as BodyInit), {
    status: r.status,
    headers: r.contentType ? { 'Content-Type': r.contentType } : {},
  });
}
