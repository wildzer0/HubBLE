/**
 * @file Check.tsx
 * @description Health check page: runs the checks of `lib/healthcheck.ts` on the live data of the dongle and shows
 * each one with a verdict and a plain sentence; the summary can be copied to share it.
 */
import { useCallback, useEffect, useState } from 'preact/hooks';
import { getDevices, getDiagnostics, getOtaStatus } from '../api/client';
import { Alert, Icon, Pill } from '../components/ui';
import { useT } from '../i18n/context';
import type { Check as CheckItem } from '../lib/healthcheck';
import { issueCount, runChecks } from '../lib/healthcheck';

/**
 * Render the health check.
 *
 * @returns The page element.
 */
export function Check() {
  const t = useT();
  const [checks, setChecks] = useState<CheckItem[] | null>(null);
  const [failed, setFailed] = useState(false);
  const [copied, setCopied] = useState(false);

  const run = useCallback(() => {
    setCopied(false);
    Promise.all([getDiagnostics(), getDevices(), getOtaStatus()]).then(
      ([diag, list, ota]) => {
        setChecks(runChecks(diag, list.devices, ota));
        setFailed(false);
      },
      () => {
        setFailed(true);
      },
    );
  }, []);
  useEffect(run, [run]);

  const issues = checks ? issueCount(checks) : 0;
  const summary = checks
    ? checks.map((c) => `[${c.level.toUpperCase()}] ${t(c.key, c.params)}`).join('\n')
    : '';

  return (
    <>
      <div class="page-head">
        <a class="back" href="#/system">
          <Icon name="back" />
          {t('common.back')}
        </a>
      </div>
      <h2>{t('check.heading')}</h2>
      <p class="hint">{t('check.intro')}</p>
      {failed && (
        <Alert kind="bad" role="alert">
          {t('common.error')}
        </Alert>
      )}
      {checks && (
        <>
          <Alert kind={issues === 0 ? 'ok' : 'warn'} role="status" testId="check-summary">
            {issues === 0 ? t('check.summary_ok') : t('check.summary_issues', { n: issues })}
          </Alert>
          <ul class="checks" data-testid="checks">
            {checks.map((c, i) => (
              <li key={i} class="card check">
                <Pill tone={c.level === 'ok' ? 'ok' : c.level === 'warn' ? 'warn' : 'bad'}>
                  {t(`check.level.${c.level}`)}
                </Pill>
                <span>{t(c.key, c.params)}</span>
              </li>
            ))}
          </ul>
          <div class="actions">
            <button type="button" class="btn primary" onClick={run}>
              {t('check.run')}
            </button>
            <button
              type="button"
              class="btn"
              onClick={() => {
                navigator.clipboard.writeText(summary).then(
                  () => {
                    setCopied(true);
                  },
                  () => undefined,
                );
              }}
            >
              {copied ? t('check.copied') : t('check.copy')}
            </button>
          </div>
        </>
      )}
      {!checks && !failed && <p class="muted">{t('common.loading')}</p>}
    </>
  );
}
