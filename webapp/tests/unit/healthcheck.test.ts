import { describe, expect, it } from 'vitest';
import type { Device, Diagnostics, OtaStatus } from '../../src/api/types';
import { issueCount, runChecks } from '../../src/lib/healthcheck';

const diag = (over: Partial<Diagnostics> = {}): Diagnostics =>
  ({
    uptime_s: 100,
    reset_reason: 'power-on',
    heap: { free: 100 * 1024, min_free: 90 * 1024, largest_block: 50 * 1024 },
    usb_state: 'configured',
    boot_failures: 0,
    chip_temp_c: 55,
    ...over,
  }) as Diagnostics;
const ota = (over: Partial<OtaStatus> = {}): OtaStatus =>
  ({ pending_verify: false, rollback_happened: false, ...over }) as OtaStatus;
const dev = (over: Partial<Device> = {}): Device =>
  ({
    id: 'a',
    name: 'Kbd',
    connected: true,
    ready: true,
    boot_fallback: false,
    battery: 80,
    security: 'green',
    connection: { interval_ms: 7.5, latency: 0, timeout_ms: 2000 },
    latency_us: { count: 10, avg: 500, p95: 1500, max: 2000, min: 100 },
    ...over,
  }) as Device;

const find = (cs: ReturnType<typeof runChecks>, key: string) => cs.find((c) => c.key === key);

describe('health check', () => {
  it('is all fine for a healthy dongle', () => {
    const cs = runChecks(diag(), [dev()], ota());
    expect(issueCount(cs)).toBe(0);
  });
  it('flags USB states', () => {
    expect(
      find(runChecks(diag({ usb_state: 'detached' }), [], ota()), 'check.usb.bad')?.level,
    ).toBe('bad');
    expect(
      find(runChecks(diag({ usb_state: 'suspended' }), [], ota()), 'check.usb.warn')?.level,
    ).toBe('warn');
  });
  it('flags no devices, an offline device and a device not ready', () => {
    expect(find(runChecks(diag(), [], ota()), 'check.nodevices.warn')).toBeDefined();
    expect(
      find(runChecks(diag(), [dev({ connected: false })], ota()), 'check.connected.warn'),
    ).toBeDefined();
    expect(
      find(runChecks(diag(), [dev({ ready: false })], ota()), 'check.ready.warn'),
    ).toBeDefined();
  });
  it('flags latency, interval, battery, legacy security and boot fallback of a device', () => {
    const cs = runChecks(
      diag(),
      [
        dev({
          latency_us: { count: 5, avg: 9000, p95: 20000, max: 30000, min: 1 },
          connection: { interval_ms: 30, latency: 0, timeout_ms: 2000 },
          battery: 10,
          security: 'red',
          boot_fallback: true,
        }),
      ],
      ota(),
    );
    for (const k of [
      'check.latency.warn',
      'check.interval.warn',
      'check.battery.warn',
      'check.security.warn',
      'check.fallback.warn',
    ]) {
      expect(find(cs, k)).toBeDefined();
    }
  });
  it('flags memory, temperature and restarts', () => {
    expect(
      find(
        runChecks(diag({ heap: { free: 20 * 1024, min_free: 0, largest_block: 0 } }), [], ota()),
        'check.memory.warn',
      )?.level,
    ).toBe('bad');
    expect(find(runChecks(diag({ chip_temp_c: 80 }), [], ota()), 'check.temp.warn')).toBeDefined();
    expect(find(runChecks(diag({ chip_temp_c: 90 }), [], ota()), 'check.temp.bad')).toBeDefined();
    expect(
      find(runChecks(diag({ reset_reason: 'brownout' }), [], ota()), 'check.reset.brownout.warn'),
    ).toBeDefined();
    expect(
      find(runChecks(diag({ reset_reason: 'panic' }), [], ota()), 'check.reset.panic.warn'),
    ).toBeDefined();
    expect(
      find(runChecks(diag({ boot_failures: 2 }), [], ota()), 'check.bootfail.warn'),
    ).toBeDefined();
    expect(
      find(runChecks(diag(), [], ota({ rollback_happened: true })), 'check.firmware.rollback.warn'),
    ).toBeDefined();
  });
  it('lists problems before warnings before the rest', () => {
    const cs = runChecks(diag({ usb_state: 'detached', chip_temp_c: 80 }), [dev()], ota());
    expect(cs[0]?.level).toBe('bad');
    const levels = cs.map((c) => c.level);
    expect(levels).toEqual(
      [...levels].sort(
        (a, b) => ['bad', 'warn', 'ok'].indexOf(a) - ['bad', 'warn', 'ok'].indexOf(b),
      ),
    );
  });
});
