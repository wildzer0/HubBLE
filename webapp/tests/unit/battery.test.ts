import { describe, expect, it } from 'vitest';
import type { Device } from '../../src/api/types';
import { isLowBattery, lowBatteryDevices, LOW_BATTERY_PCT } from '../../src/lib/battery';

const dev = (over: Partial<Device>): Device =>
  ({ id: 'a', connected: true, battery: 50, ...over }) as Device;

describe('low battery', () => {
  it('flags a connected device at or below the threshold', () => {
    expect(isLowBattery(dev({ battery: LOW_BATTERY_PCT }))).toBe(true);
    expect(isLowBattery(dev({ battery: 5 }))).toBe(true);
    expect(isLowBattery(dev({ battery: LOW_BATTERY_PCT + 1 }))).toBe(false);
  });
  it('ignores an unknown level and a disconnected device', () => {
    expect(isLowBattery(dev({ battery: null }))).toBe(false);
    expect(isLowBattery(dev({ battery: 3, connected: false }))).toBe(false);
  });
  it('lists only the low ones, in order', () => {
    const list = [
      dev({ id: 'a', battery: 90 }),
      dev({ id: 'b', battery: 10 }),
      dev({ id: 'c', battery: 15 }),
    ];
    expect(lowBatteryDevices(list).map((d) => d.id)).toEqual(['b', 'c']);
  });
});
