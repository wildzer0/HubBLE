/**
 * @file battery.ts
 * @description Low-battery rule shared by the pages: a device is "low" at or below 20 %.
 */
import type { Device } from '../api/types';

/** Battery percentage at or below which a connected device is reported as low. */
export const LOW_BATTERY_PCT = 20;

/**
 * Is this device's battery low? An unknown level (null) is not reported, and neither is a disconnected device
 * (its last level may be old).
 *
 * @param d Device.
 * @returns true if the battery is known and at or below {@link LOW_BATTERY_PCT}.
 */
export function isLowBattery(d: Device): boolean {
  return d.connected && d.battery !== null && d.battery <= LOW_BATTERY_PCT;
}

/**
 * Devices whose battery is low.
 *
 * @param devices All devices.
 * @returns The low-battery ones, in the same order.
 */
export function lowBatteryDevices(devices: readonly Device[]): Device[] {
  return devices.filter(isLowBattery);
}
