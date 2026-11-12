/**
 * @file useApi.ts
 * @description Hook that loads data from the API, optionally refreshing on an interval.
 */
import { useEffect, useState } from 'preact/hooks';

/** State returned by {@link useApi}. */
export interface ApiState<T> {
  /** Latest data, or null before the first success. */
  data: T | null;
  /** True if the last attempt failed. */
  failed: boolean;
}

/**
 * Load data with `load` on mount and then every `intervalMs` (0 = once).
 *
 * @param load Function returning the data.
 * @param intervalMs Refresh period in milliseconds, or 0 to load once.
 * @returns The current {@link ApiState}.
 */
export function useApi<T>(
  load: () => Promise<T>,
  intervalMs = 0,
): ApiState<T> & { reload: () => void } {
  const [state, setState] = useState<ApiState<T>>({ data: null, failed: false });
  const [tick, setTick] = useState(0);

  useEffect(() => {
    let alive = true;
    const run = () => {
      load().then(
        (data) => {
          if (alive) setState({ data, failed: false });
        },
        () => {
          if (alive) setState((s) => ({ ...s, failed: true }));
        },
      );
    };
    run();
    const timer = intervalMs > 0 ? setInterval(run, intervalMs) : null;
    return () => {
      alive = false;
      if (timer) clearInterval(timer);
    };
    // `load` is a stable module-level API function at every call site.
  }, [intervalMs, tick]);

  return {
    ...state,
    reload: () => {
      setTick((n) => n + 1);
    },
  };
}
