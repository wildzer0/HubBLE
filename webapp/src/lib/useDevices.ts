/**
 * @file useDevices.ts
 * @description Hook returning the device list, refreshed every few seconds and immediately when a
 * `device.updated` / `device.removed` WebSocket event arrives.
 */
import { useEffect } from 'preact/hooks';
import { getDevices } from '../api/client';
import { subscribe } from '../api/live';
import type { DeviceList } from '../api/types';
import { useApi } from './useApi';
import type { ApiState } from './useApi';

/** Polling period; WebSocket events make updates feel immediate, this is the fallback. */
const REFRESH_MS = 3000;

/**
 * Load the device list and keep it fresh.
 *
 * @returns The current {@link ApiState} of the list.
 */
export function useDevices(): ApiState<DeviceList> & { reload: () => void } {
  const state = useApi(getDevices, REFRESH_MS);
  useEffect(
    () =>
      subscribe((m) => {
        if (m.event === 'device.updated' || m.event === 'device.removed') {
          state.reload();
        }
      }),
    [],
  );
  return state;
}
