/**
 * @file download.ts
 * @description Save text or binary data as a file through the browser (report maps, backups, crash dumps).
 */

/**
 * Offer binary data to the user as a download.
 *
 * @param filename Suggested file name.
 * @param bytes File contents.
 * @param mime MIME type.
 */
export function downloadBytes(
  filename: string,
  bytes: ArrayBuffer,
  mime = 'application/octet-stream',
): void {
  const url = URL.createObjectURL(new Blob([bytes], { type: mime }));
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

/**
 * Offer `text` to the user as a download.
 *
 * @param filename Suggested file name.
 * @param text File contents.
 * @param mime MIME type.
 */
export function downloadText(filename: string, text: string, mime = 'text/plain'): void {
  const url = URL.createObjectURL(new Blob([text], { type: mime }));
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}
