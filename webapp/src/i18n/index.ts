/**
 * @file index.ts
 * @description Tiny i18n layer: English (default) and Italian, no dependencies.
 */
import { en } from './en';
import type { MessageKey } from './en';
import { it } from './it';

export type { MessageKey } from './en';

/** Supported UI languages. */
export type Locale = 'en' | 'it';

const tables: Record<Locale, Record<MessageKey, string>> = { en, it };

/**
 * Pick the best supported locale for a BCP 47 language tag.
 *
 * @param tag Language tag such as "it-IT"; may be undefined.
 * @returns "it" for Italian tags, otherwise the default "en".
 */
export function pickLocale(tag: string | undefined): Locale {
  return tag?.toLowerCase().startsWith('it') ? 'it' : 'en';
}

/**
 * Translate a key, substituting `{name}` placeholders.
 *
 * @param locale Target language.
 * @param key Message key (compile-time checked).
 * @param params Values for the placeholders.
 * @returns The translated string.
 */
export function translate(
  locale: Locale,
  key: MessageKey,
  params?: Record<string, string | number>,
): string {
  const text = tables[locale][key];
  if (!params) return text;
  return text.replace(/\{(\w+)\}/g, (m, name: string) => {
    const v = params[name];
    return v === undefined ? m : String(v);
  });
}
