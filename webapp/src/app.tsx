/**
 * @file app.tsx
 * @description Root component: header, hash-based navigation (Dashboard, Update, System), language
 * switch and the live-connection indicator.
 */
import { useEffect, useState } from 'preact/hooks';
import { subscribeState } from './api/live';
import { LocaleContext, useT } from './i18n/context';
import { pickLocale } from './i18n';
import type { Locale } from './i18n';
import { Dashboard } from './pages/Dashboard';
import { System } from './pages/System';
import { Update } from './pages/Update';

/** Routes of the app, as `#/name` hashes. */
const ROUTES = ['dashboard', 'update', 'system'] as const;
type Route = (typeof ROUTES)[number];

/**
 * Parse the location hash into a route.
 *
 * @param hash Value of `location.hash`.
 * @returns A known route (the dashboard if unknown).
 */
export function parseRoute(hash: string): Route {
  const name = hash.replace(/^#\/?/, '');
  return ROUTES.find((r) => r === name) ?? 'dashboard';
}

/**
 * Navigation bar and connection indicator.
 *
 * @param props.route Current route.
 * @returns The header element.
 */
function Nav({ route }: { route: Route }) {
  const t = useT();
  const [live, setLive] = useState(false);
  useEffect(() => subscribeState(setLive), []);

  return (
    <nav aria-label={t('nav.label')}>
      {ROUTES.map((r) => (
        <a key={r} href={`#/${r}`} aria-current={r === route ? 'page' : undefined}>
          {t(`nav.${r}`)}
        </a>
      ))}
      <span class="live" role="status">
        {live ? t('live.connected') : t('live.disconnected')}
      </span>
    </nav>
  );
}

/**
 * Root component.
 *
 * @returns The application element.
 */
export function App() {
  const [locale, setLocale] = useState<Locale>(pickLocale(navigator.language));
  const [route, setRoute] = useState<Route>(parseRoute(location.hash));

  useEffect(() => {
    const onHash = () => {
      setRoute(parseRoute(location.hash));
    };
    window.addEventListener('hashchange', onHash);
    return () => {
      window.removeEventListener('hashchange', onHash);
    };
  }, []);

  useEffect(() => {
    document.documentElement.lang = locale;
  }, [locale]);

  return (
    <LocaleContext.Provider value={locale}>
      <main>
        <Header locale={locale} onLocale={setLocale} />
        <Nav route={route} />
        {route === 'dashboard' && <Dashboard />}
        {route === 'update' && <Update />}
        {route === 'system' && <System />}
      </main>
    </LocaleContext.Provider>
  );
}

/**
 * Title row with the language selector.
 *
 * @param props.locale Active locale.
 * @param props.onLocale Called when the user picks another language.
 * @returns The header element.
 */
function Header({ locale, onLocale }: { locale: Locale; onLocale: (l: Locale) => void }) {
  const t = useT();
  return (
    <header>
      <h1>{t('app.title')}</h1>
      <label>
        {t('lang.label')}{' '}
        <select
          value={locale}
          onChange={(e) => {
            onLocale(pickLocale(e.currentTarget.value));
          }}
        >
          <option value="en">English</option>
          <option value="it">Italiano</option>
        </select>
      </label>
    </header>
  );
}
