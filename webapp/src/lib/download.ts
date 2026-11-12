/**
 * @file download.ts
 * @description Save text as a file through the browser (used for report maps and backups).
 */

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
