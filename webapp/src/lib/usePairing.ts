/**
 * @file usePairing.ts
 * @description Hook that follows the pairing state machine: polls `GET /pairing/scan` every second and
 * applies `pairing.*` WebSocket events as soon as they arrive.
 */
import { useEffect, useState } from 'preact/hooks';
import { getScan } from '../api/client';
import { subscribe } from '../api/live';
import type { Pairing, ScanResult } from '../api/types';

/** Poll period. */
const POLL_MS = 1000;

/** What the pairing page needs to render. */
export interface PairingView {
  /** Latest pairing status, or null before the first answer. */
  pairing: Pairing | null;
  /** Devices found by the scan. */
  results: ScanResult[];
  /** True if the dongle could not be reached on the last poll. */
  failed: boolean;
  /** Force an immediate refresh (after the user pressed a button). */
  refresh: () => void;
}

/**
 * Follow the pairing state while the component is mounted.
 *
 * @returns The current {@link PairingView}.
 */
export function usePairing(): PairingView {
  const [pairing, setPairing] = useState<Pairing | null>(null);
  const [results, setResults] = useState<ScanResult[]>([]);
  const [failed, setFailed] = useState(false);
  const [tick, setTick] = useState(0);

  useEffect(() => {
    let alive = true;
    const run = () => {
      getScan().then(
        (r) => {
          if (!alive) return;
          setPairing(r.pairing);
          setResults(r.results);
          setFailed(false);
        },
        () => {
          if (alive) setFailed(true);
        },
      );
    };
    run();
    const timer = setInterval(run, POLL_MS);
    const off = subscribe((m) => {
      if (m.event.startsWith('pairing.') && alive) setPairing(m.data as Pairing);
    });
    return () => {
      alive = false;
      clearInterval(timer);
      off();
    };
  }, [tick]);

  return {
    pairing,
    results,
    failed,
    refresh: () => {
      setTick((n) => n + 1);
    },
  };
}
