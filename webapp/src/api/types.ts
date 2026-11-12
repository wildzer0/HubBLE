/**
 * @file types.ts
 * @description Types of the firmware REST/WebSocket contract (see docs/api/openapi.yaml and websocket.md).
 */

/** State of the USB link to the host PC. */
export type UsbState = 'detached' | 'suspended' | 'active';

/** Response of `GET /api/v1/status`. */
export interface Status {
  /** Firmware version (SemVer, no leading "v"). */
  version: string;
  /** Board identifier, e.g. "s3_zero" or "xiao_s3". */
  board: string;
  /** Seconds since boot. */
  uptime_s: number;
  /** Current USB state as seen by the dongle. */
  usb_state: UsbState;
  /** Seconds until the Wi-Fi AP switches off, or null if no countdown is running. */
  ap_timeout_s: number | null;
  /** True until the setup wizard has set a Wi-Fi password. */
  setup_required: boolean;
}

/** One OTA slot as shown on the Update page. */
export interface OtaSlot {
  /** Partition label, e.g. "ota_0". */
  slot: string;
  /** Version stored in the slot; empty if the slot holds no valid image. */
  version: string;
}

/** Response of `GET /api/v1/ota/status`. */
export interface OtaStatus {
  /** Slot the firmware is running from. */
  running: OtaSlot;
  /** The other slot (target of the next update). */
  other: OtaSlot;
  /** True while the running image has not yet passed its self-test. */
  pending_verify: boolean;
  /** True if the other slot holds an image that failed and was rolled back. */
  rollback_happened: boolean;
  /** Consecutive boots without a passed self-test. */
  boot_failures: number;
  /** Text describing the last OTA attempt; empty if none. */
  last_result: string;
  /** State of the "check for updates" engine (GitHub). */
  online: OnlineUpdate;
}

/** States of the online update engine. */
export type OnlineState =
  | 'idle'
  | 'connecting'
  | 'checking'
  | 'available'
  | 'up_to_date'
  | 'confirm'
  | 'downloading'
  | 'installed'
  | 'error';

/** Errors of the online update engine. */
export type OnlineError =
  | 'no_wifi_credentials'
  | 'wifi_failed'
  | 'network_error'
  | 'no_release'
  | 'bad_manifest'
  | 'no_board_image'
  | 'unsigned'
  | 'too_old'
  | 'downgrade'
  | 'confirm_timeout'
  | 'hash_mismatch'
  | 'install_failed'
  | 'busy';

/** Release found by "check for updates". */
export interface OnlineRelease {
  /** Release version. */
  version: string;
  /** Oldest running version that may install it directly. */
  min_version: string;
  /** Release notes page. */
  notes_url: string;
  /** Image size in bytes. */
  size: number;
  /** The image is signed. */
  signed: boolean;
  /** True if it is older than the running version (needs explicit confirmation). */
  downgrade: boolean;
}

/** The `online` object of `GET /api/v1/ota/status` and the `ota.state` event. */
export interface OnlineUpdate {
  /** Engine state. */
  state: OnlineState;
  /** Error, when `state` is `error`. */
  error?: OnlineError;
  /** True when Wi-Fi credentials for updates are saved. */
  wifi_configured: boolean;
  /** Channel used by the last check. */
  channel: 'stable' | 'prerelease';
  /** Release found, when any. */
  release?: OnlineRelease;
  /** Bytes downloaded. */
  received?: number;
  /** Bytes to download. */
  total?: number;
  /** Seconds left to confirm physically. */
  confirm_remaining_s?: number;
}

/** A flash partition as reported by diagnostics. */
export interface PartitionInfo {
  /** Partition label. */
  label: string;
  /** Flash offset. */
  address: number;
  /** Size in bytes. */
  size: number;
}

/** Response of `GET /api/v1/diagnostics`. */
export interface Diagnostics {
  /** Seconds since boot. */
  uptime_s: number;
  /** Human readable reset reason. */
  reset_reason: string;
  /** Internal RAM statistics in bytes. */
  heap: { free: number; min_free: number; largest_block: number };
  /** PSRAM statistics in bytes (0 if the board has none). */
  psram: { total: number; free: number };
  /** Running and next-update partitions. */
  partitions: { running: PartitionInfo; next_update: PartitionInfo };
  /** Boot-failure counter. */
  boot_failures: number;
  /** Chip (die) temperature, degrees Celsius; absent if the sensor cannot be read. */
  chip_temp_c?: number;
  /** USB state (M2). */
  usb_state: UsbState;
  /** BLE stack facts. */
  ble: { ready: boolean; connected: number; bonded: number };
  /** Power management state. */
  power: { mode: 'active' | 'low'; remote_wakeup: boolean };
  /** Keyboard LED bits last sent by the host (Num/Caps/Scroll Lock). */
  keyboard_leds?: number;
  /** Wi-Fi AP facts. */
  wifi: { ssid: string; clients: number };
}

/** Error body used by every failing endpoint. */
export interface ApiErrorBody {
  /** The error details. */
  error: { code: string; message: string };
}

/** Server → client WebSocket message. */
export interface WsMessage {
  /** Event name, e.g. "system.status", "log.line", "ota.progress". */
  event: string;
  /** Event payload (shape depends on the event). */
  data: unknown;
}

/** Security badge of a bonded device (green = passkey + LE SC, yellow = verified Just Works, red = legacy). */
export type SecurityLevel = 'green' | 'yellow' | 'red' | 'unknown';

/** Round-trip statistics of one device, microseconds (BLE notify entry to USB report queued). */
export interface LatencyStats {
  /** Number of samples. */
  count: number;
  /** Minimum. */
  min: number;
  /** Average. */
  avg: number;
  /** 95th percentile. */
  p95: number;
  /** Maximum. */
  max: number;
}

/** A bonded device from `GET /api/v1/devices`. */
export interface Device {
  /** 12 hex digits (the address). */
  id: string;
  /** Registry slot 0..3. */
  slot: number;
  /** User-visible name. */
  name: string;
  /** Kind of device, known once its report map has been parsed. */
  type: 'keyboard' | 'pointer' | 'unknown';
  /** True while the BLE link is up. */
  connected: boolean;
  /** True once HID discovery finished and input is forwarded. */
  ready: boolean;
  /** True if the boot-protocol fallback is used instead of a parsed report map. */
  boot_fallback: boolean;
  /** Battery percentage, or null if unknown. */
  battery: number | null;
  /** Last RSSI in dBm, or null if not connected. */
  rssi: number | null;
  /** Negotiated connection parameters. */
  connection: { interval_ms: number; latency: number; timeout_ms: number };
  /** Number of reconnections since boot. */
  reconnects: number;
  /** Seconds since the device was last seen. */
  last_seen_s: number;
  /** Forwarding latency statistics. */
  latency_us: LatencyStats;
  /** Security badge. */
  security: SecurityLevel;
}

/** Response of `GET /api/v1/devices`. */
export interface DeviceList {
  /** Registered devices. */
  devices: Device[];
  /** Maximum number of devices (4). */
  max: number;
}

/** States of the pairing state machine. */
export type PairState =
  'idle' | 'scanning' | 'connecting' | 'pairing' | 'quarantine' | 'confirm' | 'bonded' | 'failed';

/** Why a pairing failed. */
export type PairFailure =
  | 'timeout'
  | 'cancelled'
  | 'legacy_rejected'
  | 'security_failed'
  | 'no_hid_service'
  | 'challenge_failed';

/** Pairing status object (REST and `pairing.*` WebSocket events). */
export interface Pairing {
  /** Current state. */
  state: PairState;
  /** Failure reason, present only when `state` is `failed`. */
  reason?: PairFailure;
  /** Security level reached or planned. */
  level: SecurityLevel;
  /** Name of the device being paired. */
  device: string;
  /** Seconds left in the current step. */
  remaining_s: number;
  /** Seconds left of the 60 s lock after 3 failures; 0 if not locked. */
  lock_remaining_s: number;
  /** Notifications received from the device being paired (shows whether it is talking to the dongle at all). */
  rx_reports?: number;
  /** True while a physical confirmation (both Shifts, 3 s) is awaited. */
  needs_confirmation: boolean;
  /** 6-digit passkey to type on the keyboard, when present. */
  passkey?: string;
  /** Quarantine challenge, when present. */
  challenge?: { kind: 'clicks' | 'code'; value: string; progress: number; length: number };
}

/** One advertising HID device found by the scan. */
export interface ScanResult {
  /** 12 hex digits. */
  address: string;
  /** BLE address type (0 public, 1 random). */
  address_type: number;
  /** Advertised name. */
  name: string;
  /** RSSI in dBm. */
  rssi: number;
  /** GAP appearance value (961 keyboard, 962 mouse). */
  appearance: number;
}

/** Response of `GET /api/v1/pairing/scan`. */
export interface ScanResponse {
  /** Pairing state name. */
  state: PairState;
  /** Full pairing status. */
  pairing: Pairing;
  /** Devices found so far. */
  results: ScanResult[];
}

/** Non-secret settings (`GET/PUT /api/v1/settings`). */
export interface Settings {
  /** Require both-Shift physical confirmation for new pairings. */
  require_confirmation: boolean;
  /** Seconds the AP stays on after power-up. */
  ap_boot_window_s: number;
  /** Seconds the AP stays on after the last client left. */
  ap_idle_timeout_s: number;
  /** Update channel used by "check for updates". */
  update_channel: 'stable' | 'prerelease';
  /** True when Wi-Fi credentials for updates are saved (the credentials themselves are never returned). */
  sta_configured: boolean;
  /** A key press, click or movement wakes the suspended host. */
  remote_wakeup: boolean;
  /** LED brightness in percent; 0 turns the status LED off. */
  led_brightness: number;
  /** Show dim green while a device is connected. */
  status_led: boolean;
  /** Hold time of the both-Shift gesture in ms (applies from the next boot). */
  shift_hold_ms: number;
}

/** Response of `GET /api/v1/devices/{id}/report-map`. */
export interface ReportMap {
  /** Raw report map, hex encoded. */
  hex: string;
  /** Length in bytes. */
  length: number;
}

/** Event `input.event` (input tester only): what one device sent. */
export interface InputEvent {
  /** Slot of the source device. */
  slot: number;
  /** Kind of input. */
  kind: 'key' | 'mouse' | 'consumer';
  /** Pressed key usages (keyboard, HID usage page 0x07). */
  keys?: number[];
  /** Modifier bitmask (keyboard). */
  modifiers?: number;
  /** Mouse button bitmask. */
  buttons?: number;
  /** Mouse delta X. */
  dx?: number;
  /** Mouse delta Y. */
  dy?: number;
  /** Wheel delta. */
  wheel?: number;
  /** Horizontal pan delta. */
  pan?: number;
  /** Consumer usage code. */
  usage?: number;
}

/** Settings backup file (`GET /api/v1/backup`). Bonds are never included: they contain keys. */
export interface Backup {
  /** Constant marker so a wrong file is rejected. */
  format: 'hubble-backup';
  /** Backup schema version. */
  version: 1;
  /** The exported settings (the Wi-Fi credentials are not part of it). */
  settings: Settings;
}

/** A Wi-Fi network found by `GET /api/v1/wifi/scan`. */
export interface WifiNetwork {
  /** Network name (hidden networks are not listed). */
  ssid: string;
  /** Signal strength, dBm. */
  rssi: number;
  /** True if the network needs a password. */
  secure: boolean;
}
