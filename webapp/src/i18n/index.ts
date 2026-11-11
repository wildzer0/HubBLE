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
 * Translate a key.
 *
 * @param locale Target language.
 * @param key Message key (compile-time checked).
 * @returns The translated string.
 */
export function translate(locale: Locale, key: MessageKey): string {
  return tables[locale][key];
}
