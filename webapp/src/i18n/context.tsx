/**
 * @file context.tsx
 * @description Preact context carrying the active locale, plus the `useT` translation hook.
 */
import { createContext } from 'preact';
import { useContext } from 'preact/hooks';
import { translate } from './index';
import type { Locale, MessageKey } from './index';

/** The active UI language. */
export const LocaleContext = createContext<Locale>('en');

/** Translation function returned by {@link useT}. */
export type TFunction = (key: MessageKey, params?: Record<string, string | number>) => string;

/**
 * Hook returning a translation function bound to the active locale.
 *
 * @returns The `t` function.
 */
export function useT(): TFunction {
  const locale = useContext(LocaleContext);
  return (key, params) => translate(locale, key, params);
}
