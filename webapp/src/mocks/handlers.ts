/**
 * @file handlers.ts
 * @description Framework-independent mock of the firmware REST API. Used by the Vite dev-server plugin
 * (dev + Playwright) and by unit tests, so the web app can be developed and tested without hardware
 * (CLAUDE.md §3.7). It reproduces the firmware's CSRF guard and validation rules.
 */
import type {
  Backup,
  Device,
  OnlineUpdate,
  OnlineRelease,
  Diagnostics,
  OtaStatus,
  Pairing,
  ScanResult,
  Settings,
  Status,
} from '../api/types';
import { passwordProblem } from '../lib/password.ts';

/** A request as seen by the mock. */
export interface MockRequest {
  /** HTTP method. */
  method: string;
  /** Path without query string. */
  path: string;
  /** Lower-cased request headers. */
  headers: Record<string, string | undefined>;
  /** Parsed JSON body, if any. */
  json?: unknown;
  /** Total request body size in bytes (used by the upload endpoint). */
  bodyBytes: number;
}

/** Result of a mocked request. */
export interface MockResponse {
  /** HTTP status code. */
  status: number;
  /** JSON body (mutually exclusive with `text`). */
  json?: unknown;
  /** Plain text body, e.g. the log download. */
  text?: string;
}

/** Version the mocked dongle reports as running. */
export const MOCK_VERSION = '0.2.0-mock';
/** Version the mock "installs" on upload. */
export const MOCK_UPLOAD_VERSION = '0.2.1-rc.1-mock';
/** Slot size of the S3-Zero; uploads above this are rejected like the firmware does. */
export const MOCK_SLOT_BYTES = 0x1e0000;
/** Uploads smaller than this cannot be a real image. */
export const MOCK_MIN_IMAGE_BYTES = 1024;

/** Devices the mock dongle starts with. */
function initialDevices(): Device[] {
  const base = {
    boot_fallback: false,
    reconnects: 0,
    last_seen_s: 0,
    connection: { interval_ms: 7.5, latency: 0, timeout_ms: 2000 },
  };
  return [
    {
      ...base,
      id: 'c0ffee000001',
      slot: 0,
      name: 'YUNZII B68',
      type: 'keyboard',
      connected: true,
      ready: true,
      battery: 87,
      rssi: -52,
      latency_us: { count: 4210, min: 310, avg: 780, p95: 1500, max: 4000 },
      security: 'green',
    },
    {
      ...base,
      id: 'c0ffee000002',
      slot: 1,
      name: 'ProtoArc EM06',
      type: 'pointer',
      connected: true,
      ready: true,
      battery: 64,
      rssi: -61,
      latency_us: { count: 9800, min: 290, avg: 640, p95: 1250, max: 3500 },
      security: 'yellow',
    },
  ];
}

/** Devices the mock scan "finds": a keyboard, a mouse and a legacy-only keyboard. */
export const MOCK_SCAN: ScanResult[] = [
  { address: 'aabbccddee01', address_type: 0, name: 'Mock Keyboard', rssi: -48, appearance: 961 },
  { address: 'aabbccddee02', address_type: 0, name: 'Mock Mouse', rssi: -55, appearance: 962 },
  { address: 'aabbccddee03', address_type: 0, name: 'Old Keyboard', rssi: -70, appearance: 961 },
];
/** Click sequence of the mock quarantine challenge. */
export const MOCK_CLICKS = 'LRRLMR';
/** Passkey shown by the mock. */
export const MOCK_PASSKEY = '123456';
/** Consecutive failures that lock pairing (CLAUDE.md §3.12). */
const LOCK_AFTER = 3;
/** Seconds of the pairing lock. */
const LOCK_S = 60;
/** Maximum devices. */
const MAX_DEVICES = 4;

/** What the mocked "check for updates" finds (set with `POST /api/v1/mock/ota`). */
export type OtaScenario = 'available' | 'downgrade' | 'up_to_date' | 'wifi_failed' | 'confirm';

/** Release the mock finds on each channel. */
function mockRelease(channel: 'stable' | 'prerelease', downgrade: boolean): OnlineRelease {
  return {
    version: downgrade ? '0.1.0' : channel === 'prerelease' ? '0.8.0-rc.1' : '0.7.0',
    min_version: '0.1.0',
    notes_url: 'https://example.org/releases/notes',
    size: 1_179_648,
    signed: true,
    downgrade,
  };
}

/** Options for {@link resetMock}. */
export interface MockOptions {
  /** Start with the setup already done (dev server); unit tests use false like a fresh dongle. */
  setupDone?: boolean;
}

/** Initial mock state; also what {@link resetMock} restores. */
function initialState(opts: MockOptions = {}) {
  return {
    setupDone: opts.setupDone ?? false,
    version: MOCK_VERSION,
    devices: initialDevices(),
    settings: {
      require_confirmation: true,
      ap_boot_window_s: 300,
      ap_idle_timeout_s: 180,
      update_channel: 'stable',
      sta_configured: false,
      remote_wakeup: true,
      led_brightness: 25,
      status_led: false,
      shift_hold_ms: 3000,
    } as Settings,
    online: { state: 'idle', channel: 'stable', wifi_configured: false } as OnlineUpdate,
    scenario: 'available' as OtaScenario,
    pairing: idlePairing(),
    pairingTarget: null as ScanResult | null,
    pairingLegacy: false,
    failures: 0,
  };
}
let state = initialState();

/** A pairing object in the idle state. */
function idlePairing(): Pairing {
  return {
    state: 'idle',
    level: 'unknown',
    device: '',
    remaining_s: 0,
    lock_remaining_s: 0,
    needs_confirmation: false,
  };
}

/**
 * Reset the mutable mock state (tests and the e2e reset endpoint).
 *
 * @param opts Initial options.
 */
export function resetMock(opts: MockOptions = {}): void {
  state = initialState(opts);
}

/** Record a failed pairing and apply the lock after {@link LOCK_AFTER} consecutive failures. */
function failPairing(reason: NonNullable<Pairing['reason']>): void {
  state.failures += 1;
  state.pairing = {
    ...idlePairing(),
    state: 'failed',
    reason,
    device: state.pairingTarget?.name ?? '',
    lock_remaining_s: state.failures >= LOCK_AFTER ? LOCK_S : 0,
  };
}

/** Finish a successful pairing: add the device and report the level. */
function bond(): void {
  const t = state.pairingTarget;
  if (!t) return;
  const level = state.pairing.level;
  state.devices.push({
    id: t.address,
    slot: state.devices.length,
    name: t.name,
    type: t.appearance === 962 ? 'pointer' : 'keyboard',
    connected: true,
    ready: true,
    boot_fallback: false,
    battery: 100,
    rssi: t.rssi,
    connection: { interval_ms: 7.5, latency: 0, timeout_ms: 2000 },
    reconnects: 0,
    last_seen_s: 0,
    latency_us: { count: 0, min: 0, avg: 0, p95: 0, max: 0 },
    security: level,
  });
  state.failures = 0;
  state.pairing = { ...idlePairing(), state: 'bonded', level, device: t.name };
}

/** Move to the physical-confirmation step, or bond if none is needed. */
function confirmOrBond(): void {
  const hasKeyboard = state.devices.some((d) => d.type === 'keyboard');
  if (state.settings.require_confirmation && hasKeyboard) {
    state.pairing = {
      ...state.pairing,
      state: 'confirm',
      needs_confirmation: true,
      remaining_s: 30,
    };
  } else {
    bond();
  }
}

/**
 * Test hook (`POST /api/v1/mock/pairing`): what the user would do on the device.
 *
 * @param action `advance` (type the passkey / click / hold Shift), `fail` or `timeout`.
 */
function mockPairingAction(action: string): void {
  const p = state.pairing;
  if (action === 'fail') failPairing('challenge_failed');
  else if (action === 'timeout') failPairing('timeout');
  else if (p.state === 'pairing') confirmOrBond();
  else if (p.state === 'quarantine' && p.challenge) {
    const progress = p.challenge.progress + 1;
    if (progress >= p.challenge.length) confirmOrBond();
    else state.pairing = { ...p, challenge: { ...p.challenge, progress } };
  } else if (p.state === 'confirm') bond();
}

/** Handle `/api/v1/pairing/*` requests; null if the route is not a pairing route. */
function handlePairing(req: MockRequest): MockResponse | null {
  const { method, path } = req;
  if (method === 'GET' && path === '/api/v1/pairing/scan') {
    return {
      status: 200,
      json: {
        state: state.pairing.state,
        pairing: state.pairing,
        results: state.pairing.state === 'scanning' ? MOCK_SCAN : [],
      },
    };
  }
  if (method !== 'POST') return null;
  const locked = state.pairing.lock_remaining_s > 0;
  const lockedErr = err(
    429,
    'pairing_locked',
    'Pairing is locked for 60 s after 3 failed attempts',
  );
  if (path === '/api/v1/pairing/scan') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    if (locked) return lockedErr;
    if (state.devices.length >= MAX_DEVICES) {
      return err(409, 'no_free_slot', 'Four devices are already paired; forget one first');
    }
    state.pairing = { ...idlePairing(), state: 'scanning', remaining_s: 30 };
    return { status: 200, json: { ok: true } };
  }
  if (path === '/api/v1/pairing/start') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    if (locked) return lockedErr;
    const body = req.json as { address?: string; allow_legacy?: boolean } | undefined;
    const target = MOCK_SCAN.find((r) => r.address === body?.address);
    if (!target || state.pairing.state !== 'scanning') {
      return err(409, 'busy', 'Pairing already running, or the device is already paired');
    }
    state.pairingTarget = target;
    const base = { ...idlePairing(), device: target.name, remaining_s: 60 };
    if (target.address === 'aabbccddee03') {
      // This keyboard only supports legacy pairing.
      if (body?.allow_legacy === true)
        state.pairing = { ...base, state: 'pairing', level: 'red', passkey: MOCK_PASSKEY };
      else failPairing('legacy_rejected');
    } else if (target.appearance === 961) {
      state.pairing = { ...base, state: 'pairing', level: 'green', passkey: MOCK_PASSKEY };
    } else {
      state.pairing = {
        ...base,
        state: 'quarantine',
        level: 'yellow',
        challenge: { kind: 'clicks', value: MOCK_CLICKS, progress: 0, length: MOCK_CLICKS.length },
      };
    }
    return { status: 200, json: { ok: true } };
  }
  if (path === '/api/v1/pairing/cancel') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    state.pairing = idlePairing();
    return { status: 200, json: { ok: true } };
  }
  if (path === '/api/v1/mock/pairing') {
    mockPairingAction((req.json as { action?: string } | undefined)?.action ?? '');
    return { status: 200, json: state.pairing };
  }
  return null;
}

/** Handle `/api/v1/devices*` requests; null if the route is not a device route. */
function handleDevices(req: MockRequest): MockResponse | null {
  const { method, path } = req;
  if (method === 'GET' && path === '/api/v1/devices') {
    return { status: 200, json: { devices: state.devices, max: MAX_DEVICES } };
  }
  const m = /^\/api\/v1\/devices\/([0-9a-f]{12})(\/report-map)?$/.exec(path);
  if (!m) return null;
  const dev = state.devices.find((d) => d.id === m[1]);
  if (!dev) return err(404, 'not_found', 'No such device');
  if (m[2]) {
    if (method !== 'GET') return null;
    if (!dev.connected) return err(409, 'not_connected', 'The device is not connected');
    return { status: 200, json: { hex: MOCK_REPORT_MAP, length: MOCK_REPORT_MAP.length / 2 } };
  }
  if (method === 'GET') return { status: 200, json: dev };
  const bad = guard(req, 'application/json');
  if (bad) return bad;
  if (method === 'PATCH') {
    const name = (req.json as { name?: unknown } | undefined)?.name;
    if (typeof name !== 'string' || name.trim() === '' || name.length > 31) {
      return err(400, 'bad_name', 'Name must be 1-31 characters');
    }
    dev.name = name;
    return { status: 200, json: dev };
  }
  if (method === 'DELETE') {
    state.devices = state.devices.filter((d) => d !== dev);
    state.devices.forEach((d, i) => (d.slot = i));
    return { status: 200, json: { ok: true } };
  }
  return null;
}

/** A short boot-keyboard report map (hex), enough to exercise the export. */
export const MOCK_REPORT_MAP =
  '05010906a101050719e029e715002501750195088102950175088101950575010508190129059102950175039101' +
  '95067508150025650507190029658100c0';

/** Networks the mock scan "finds" (strongest first, like the firmware). */
const MOCK_NETWORKS = [
  { ssid: 'Casa-Rossi', rssi: -42, secure: true },
  { ssid: 'TIM-38291746', rssi: -58, secure: true },
  { ssid: 'Vodafone-Guest', rssi: -66, secure: false },
  { ssid: 'FRITZ!Box 7590', rssi: -78, secure: true },
];

/** Handle STA credentials and the online update endpoints; null otherwise. */
function handleOnline(req: MockRequest): MockResponse | null {
  const { method, path } = req;
  if (method === 'PUT' && path === '/api/v1/settings/sta') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    const b = req.json as { ssid?: unknown; password?: unknown } | undefined;
    const ssid = b?.ssid;
    const pw = typeof b?.password === 'string' ? b.password : '';
    if (typeof ssid !== 'string' || ssid.length > 32) {
      return err(400, 'invalid_sta', 'SSID must be 1-32 characters');
    }
    if (ssid !== '' && pw !== '' && (pw.length < 8 || pw.length > 63)) {
      return err(400, 'invalid_sta', 'Password must be empty or 8-63 characters');
    }
    state.settings = { ...state.settings, sta_configured: ssid !== '' };
    return { status: 200, json: state.settings };
  }
  if (method === 'GET' && path === '/api/v1/wifi/scan') {
    return { status: 200, json: { networks: MOCK_NETWORKS } };
  }
  if (method === 'POST' && path === '/api/v1/ota/check') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    if (!state.settings.sta_configured) {
      return err(409, 'no_wifi_credentials', 'Save the Wi-Fi network used for updates first');
    }
    const channel =
      (req.json as { channel?: string } | undefined)?.channel === 'prerelease'
        ? 'prerelease'
        : 'stable';
    const o = { channel, wifi_configured: true } as const;
    switch (state.scenario) {
      case 'wifi_failed':
        state.online = { ...o, state: 'error', error: 'wifi_failed' };
        break;
      case 'up_to_date':
        state.online = { ...o, state: 'up_to_date' };
        break;
      default:
        state.online = {
          ...o,
          state: 'available',
          release: mockRelease(channel, state.scenario === 'downgrade'),
        };
    }
    return { status: 200, json: { ok: true } };
  }
  if (method === 'POST' && path === '/api/v1/ota/install') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    const rel = state.online.release;
    if (state.online.state !== 'available' || !rel) {
      return err(409, 'nothing_to_install', 'Check for updates first');
    }
    if (
      rel.downgrade &&
      (req.json as { allow_downgrade?: boolean } | undefined)?.allow_downgrade !== true
    ) {
      return err(409, 'downgrade_not_confirmed', 'This release is older than the running one');
    }
    if (state.scenario === 'confirm') {
      state.online = { ...state.online, state: 'confirm', confirm_remaining_s: 30 };
    } else {
      state.version = rel.version;
      state.online = { ...state.online, state: 'installed', received: rel.size, total: rel.size };
    }
    return { status: 200, json: { ok: true } };
  }
  if (method === 'POST' && path === '/api/v1/mock/ota') {
    // Test hook: choose what the next check finds, or advance an install waiting for the confirmation.
    const b = req.json as { scenario?: OtaScenario; advance?: boolean } | undefined;
    if (b?.scenario) state.scenario = b.scenario;
    const rel = state.online.release;
    if (b?.advance && rel) {
      if (state.online.state === 'confirm') {
        state.online = {
          ...state.online,
          state: 'downloading',
          received: rel.size / 2,
          total: rel.size,
        };
      } else if (state.online.state === 'downloading') {
        state.version = rel.version;
        state.online = { ...state.online, state: 'installed', received: rel.size, total: rel.size };
      }
    }
    return { status: 200, json: state.online };
  }
  return null;
}

/** Handle `/api/v1/settings`, backup and restore; null otherwise. */
function handleSettings(req: MockRequest): MockResponse | null {
  const { method, path } = req;
  if (path === '/api/v1/settings' && method === 'GET') return { status: 200, json: state.settings };
  if (path === '/api/v1/settings' && method === 'PUT') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    const next = {
      ...state.settings,
      ...(req.json as Partial<Settings>),
      sta_configured: state.settings.sta_configured,
    };
    if (!validSettings(next)) return err(400, 'invalid_setting', 'A setting is out of range');
    state.settings = next;
    return { status: 200, json: state.settings };
  }
  if (path === '/api/v1/backup' && method === 'GET') {
    const b: Backup = { format: 'hubble-backup', version: 1, settings: state.settings };
    return { status: 200, json: b };
  }
  if (path === '/api/v1/restore' && method === 'POST') {
    const bad = guard(req, 'application/json');
    if (bad) return bad;
    const b = req.json as Partial<Backup> | undefined;
    if (b?.format !== 'hubble-backup' || b.version !== 1 || !validSettings(b.settings)) {
      return err(400, 'bad_backup', 'Not a valid HubBLE backup');
    }
    // Older backups lack the newer fields: keep the current values for them.
    const incoming = b.settings as Partial<Settings>;
    state.settings = {
      ...state.settings,
      ...incoming,
      sta_configured: state.settings.sta_configured,
    };
    return { status: 200, json: { ok: true } };
  }
  return null;
}

/** Range check shared by PUT /settings and restore (mirrors the firmware). */
function validSettings(s: Partial<Settings> | undefined): s is Settings {
  const win = (v: unknown) => typeof v === 'number' && Number.isInteger(v) && v >= 30 && v <= 86400;
  return (
    typeof s?.require_confirmation === 'boolean' &&
    win(s.ap_boot_window_s) &&
    win(s.ap_idle_timeout_s) &&
    // Older backups (and clients) may omit the channel.
    [undefined, 'stable', 'prerelease'].includes(s.update_channel) &&
    (s.led_brightness === undefined ||
      (Number.isInteger(s.led_brightness) && s.led_brightness >= 0 && s.led_brightness <= 100)) &&
    (s.shift_hold_ms === undefined ||
      (Number.isInteger(s.shift_hold_ms) && s.shift_hold_ms >= 1000 && s.shift_hold_ms <= 10000))
  );
}

/** Fixed fake status; realistic values, deterministic for tests. */
export function mockStatus(): Status {
  return {
    version: state.version,
    board: 's3_zero',
    uptime_s: 1234,
    usb_state: 'detached',
    ap_timeout_s: 180,
    setup_required: !state.setupDone,
  };
}

/** Fake OTA status with a pending-verify image and a failed previous attempt. */
function mockOta(): OtaStatus {
  return {
    running: { slot: 'ota_0', version: state.version },
    other: { slot: 'ota_1', version: '0.1.1' },
    pending_verify: false,
    rollback_happened: false,
    boot_failures: 0,
    last_result: `running ${state.version}, self-test passed`,
    online: { ...state.online, wifi_configured: state.settings.sta_configured },
  };
}

/** Fake diagnostics. */
function mockDiagnostics(): Diagnostics {
  return {
    uptime_s: 1234,
    reset_reason: 'power-on',
    heap: { free: 142_000, min_free: 120_500, largest_block: 98_304 },
    psram: { total: 2_097_152, free: 2_000_000 },
    partitions: {
      running: { label: 'ota_0', address: 0x20000, size: MOCK_SLOT_BYTES },
      next_update: { label: 'ota_1', address: 0x200000, size: MOCK_SLOT_BYTES },
    },
    boot_failures: 0,
    usb_state: 'detached',
    power: { mode: 'active', remote_wakeup: state.settings.remote_wakeup },
    ble: {
      ready: true,
      connected: state.devices.filter((d) => d.connected).length,
      bonded: state.devices.length,
    },
    wifi: { ssid: 'HubBLE-A1B2', clients: 1 },
  };
}

/** Standard error response. */
function err(status: number, code: string, message: string): MockResponse {
  return { status, json: { error: { code, message } } };
}

/**
 * Apply the firmware's CSRF guard to a state-changing request.
 *
 * @param req Request.
 * @param expectedCt Required content type prefix.
 * @returns An error response, or null if the request passes.
 */
function guard(req: MockRequest, expectedCt: string): MockResponse | null {
  if (req.headers['x-hubble'] !== '1') {
    return err(403, 'forbidden', 'Missing X-HubBLE header or cross-origin request');
  }
  if (!(req.headers['content-type'] ?? '').toLowerCase().startsWith(expectedCt)) {
    return err(415, 'bad_content_type', 'Unexpected Content-Type');
  }
  return null;
}

/**
 * Answer a request the way the firmware would.
 *
 * @param req The request.
 * @returns The mocked response (404 error body for unknown routes).
 */
export function handleMock(req: MockRequest): MockResponse {
  const { method, path } = req;
  const routed =
    handlePairing(req) ?? handleDevices(req) ?? handleOnline(req) ?? handleSettings(req);
  if (routed) return routed;
  if (method === 'GET') {
    switch (path) {
      case '/api/v1/status':
        return { status: 200, json: mockStatus() };
      case '/api/v1/ota/status':
        return { status: 200, json: mockOta() };
      case '/api/v1/diagnostics':
        return { status: 200, json: mockDiagnostics() };
      case '/api/v1/logs':
        return {
          status: 200,
          text: 'I (100) hubble: HubBLE firmware (mock)\nI (200) web: HTTP server up\n',
        };
    }
  }
  if (method === 'POST') {
    switch (path) {
      case '/api/v1/settings/wifi-password': {
        const bad = guard(req, 'application/json');
        if (bad) return bad;
        const pw = (req.json as { password?: unknown } | undefined)?.password;
        if (typeof pw !== 'string' || passwordProblem(pw) !== null) {
          return err(
            400,
            'invalid_password',
            'Password must be 10-63 printable characters and not the default',
          );
        }
        state.setupDone = true;
        return { status: 200, json: { ok: true } };
      }
      case '/api/v1/system/factory-reset': {
        const bad = guard(req, 'application/json');
        if (bad) return bad;
        resetMock();
        return { status: 200, json: { ok: true } };
      }
      case '/api/v1/mock/reset': {
        // Test hook, not part of the firmware API.
        resetMock({
          setupDone: (req.json as { setupDone?: boolean } | undefined)?.setupDone ?? true,
        });
        return { status: 200, json: { ok: true } };
      }
      case '/api/v1/system/reboot':
      case '/api/v1/system/reboot-download': {
        const bad = guard(req, 'application/json');
        return bad ?? { status: 200, json: { ok: true } };
      }
      case '/api/v1/ota/upload': {
        const bad = guard(req, 'application/octet-stream');
        if (bad) return bad;
        if (req.bodyBytes > MOCK_SLOT_BYTES) return err(413, 'bad_size', 'bad size');
        if (req.bodyBytes < MOCK_MIN_IMAGE_BYTES)
          return err(400, 'bad_image', 'not a valid ESP32-S3 image');
        state.version = MOCK_UPLOAD_VERSION;
        return { status: 200, json: { ok: true, version: MOCK_UPLOAD_VERSION, reboot: true } };
      }
    }
  }
  return err(404, 'not_found', `No route ${method} ${path}`);
}
