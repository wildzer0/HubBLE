/**
 * @file SecurityBadge.tsx
 * @description Security level badge (green / yellow / red). The level is carried by the text as well as the
 * colour so colour is never the only carrier of the information (accessibility).
 */
import type { SecurityLevel } from '../api/types';
import { useT } from '../i18n/context';
import { Pill, SECURITY_TONE } from './ui';

/**
 * Render the badge.
 *
 * @param props.level Security level.
 * @returns The badge element.
 */
export function SecurityBadge({ level }: { level: SecurityLevel }) {
  const t = useT();
  return (
    <Pill tone={SECURITY_TONE[level]} testId="security">
      {t(`security.${level}`)}
    </Pill>
  );
}
