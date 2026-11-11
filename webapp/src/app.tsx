/**
 * @file app.tsx
 * @description Root component (M0 skeleton): title, language switch, status card.
 */
import { useState } from 'preact/hooks';
import { StatusCard } from './components/StatusCard';
import { pickLocale, translate } from './i18n';
import type { Locale } from './i18n';

/**
 * Root component.
 *
 * @returns The application element.
 */
export function App() {
  const [locale, setLocale] = useState<Locale>(pickLocale(navigator.language));

  return (
    <main>
      <h1>{translate(locale, 'app.title')}</h1>
      <label>
        {translate(locale, 'lang.label')}{' '}
        <select
          value={locale}
          onChange={(e) => {
            setLocale(pickLocale(e.currentTarget.value));
          }}
        >
          <option value="en">English</option>
          <option value="it">Italiano</option>
        </select>
      </label>
      <StatusCard locale={locale} />
    </main>
  );
}
