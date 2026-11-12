/**
 * @file app.tsx
 * @description Root component: header, hash-based navigation, the first-run redirect to the setup
 * wizard and the live-connection indicator.
 */
import { useEffect, useState } from 'preact/hooks';
import { getStatus } from './api/client';
import { subscribeState } from './api/live';
import { LocaleContext, useT } from './i18n/context';
import { pickLocale } from './i18n';
import type { Locale } from './i18n';
import { Icon } from './components/ui';
import { PairingBanner } from './components/PairingBanner';
import { Dashboard } from './pages/Dashboard';
import { DeviceDetail } from './pages/DeviceDetail';
import { Pair } from './pages/Pair';
import { Settings } from './pages/Settings';
import { Setup } from './pages/Setup';
import { System } from './pages/System';
import { Tester } from './pages/Tester';
import { Update } from './pages/Update';

/** Routes of the app, as `#/name` hashes (`device` takes an id: `#/device/<id>`). */
const ROUTES = [
  'dashboard',
  'pair',
  'device',
  'tester',
  'settings',
  'update',
  'system',
  'setup',
] as const;
type Route = (typeof ROUTES)[number];
/** Routes shown in the navigation bar. */
const NAV_ROUTES = ['dashboard', 'update', 'settings', 'system'] as const;
/** Icon of each tab. */
const NAV_ICON = {
  dashboard: 'devices',
  update: 'update',
  settings: 'settings',
  system: 'system',
} as const;

/**
 * Parse the location hash into a route.
 *
 * @param hash Value of `location.hash`.
 * @returns A known route (the dashboard if unknown).
 */
export function parseRoute(hash: string): Route {
  const name = hash.replace(/^#\/?/, '').split('/')[0];
  return ROUTES.find((r) => r === name) ?? 'dashboard';
}

/**
 * Parameter of a route, e.g. the device id of `#/device/aabbccddeeff`.
 *
 * @param hash Value of `location.hash`.
 * @returns The part after the route name, or an empty string.
 */
export function routeParam(hash: string): string {
  return hash.replace(/^#\/?/, '').split('/')[1] ?? '';
}

/**
 * "Skip to content" link for keyboard and screen-reader users. It moves focus with script instead of
 * changing the hash, because the hash is the router.
 *
 * @returns The link element.
 */
function SkipLink() {
  const t = useT();
  return (
    <a
      class="skip"
      href="#/"
      onClick={(e) => {
        e.preventDefault();
        document.getElementById('main')?.focus();
      }}
    >
      {t('common.skip')}
    </a>
  );
}

/**
 * Top bar with the logo and the live-connection indicator.
 *
 * @returns The header element.
 */
function TopBar() {
  const t = useT();
  const [live, setLive] = useState(false);
  useEffect(() => subscribeState(setLive), []);
  return (
    <header class="topbar">
      <div class="topbar-in">
        <h1 class="logo">{'HubBLE'}</h1>
        <span class="live" role="status">
          <span class={`dot ${live ? 'on' : ''}`} aria-hidden="true" />
          {live ? t('live.connected') : t('live.disconnected')}
        </span>
      </div>
    </header>
  );
}

/**
 * Bottom tab bar (thumb-friendly on phones).
 *
 * @param props.route Current route.
 * @returns The navigation element.
 */
function TabBar({ route }: { route: Route }) {
  const t = useT();
  // Sub-pages keep their parent tab highlighted.
  const active = route === 'pair' || route === 'device' || route === 'tester' ? 'dashboard' : route;
  return (
    <nav class="tabbar" aria-label={t('nav.label')}>
      <div class="tabbar-in">
        {NAV_ROUTES.map((r) => (
          <a key={r} class="tab" href={`#/${r}`} aria-current={r === active ? 'page' : undefined}>
            <Icon name={NAV_ICON[r]} />
            {t(`nav.${r}`)}
          </a>
        ))}
      </div>
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
  const [hash, setHash] = useState(location.hash);
  const route = parseRoute(hash);

  useEffect(() => {
    const onHash = () => {
      setHash(location.hash);
    };
    window.addEventListener('hashchange', onHash);
    return () => {
      window.removeEventListener('hashchange', onHash);
    };
  }, []);

  // Until a Wi-Fi password has been chosen, everything but the wizard is off limits (CLAUDE.md §3.5).
  useEffect(() => {
    if (route === 'setup') return;
    getStatus().then(
      (st) => {
        if (st.setup_required) location.hash = '#/setup';
      },
      () => undefined, // an unreachable dongle is reported by the pages themselves
    );
  }, [route]);

  useEffect(() => {
    document.documentElement.lang = locale;
  }, [locale]);

  return (
    <LocaleContext.Provider value={locale}>
      <SkipLink />
      <TopBar />
      <main id="main" tabIndex={-1}>
        {route !== 'pair' && route !== 'setup' && <PairingBanner />}
        {route === 'setup' && <Setup />}
        {route === 'dashboard' && <Dashboard />}
        {route === 'pair' && <Pair />}
        {route === 'device' && <DeviceDetail id={routeParam(hash)} />}
        {route === 'tester' && <Tester />}
        {route === 'settings' && <Settings locale={locale} onLocale={setLocale} />}
        {route === 'update' && <Update />}
        {route === 'system' && <System />}
      </main>
      {route !== 'setup' && <TabBar route={route} />}
    </LocaleContext.Provider>
  );
}
