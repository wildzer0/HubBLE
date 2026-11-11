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
  /** USB state (M2). */
  usb_state: UsbState;
  /** BLE statistics, null until BLE exists (M3). */
  ble: null;
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
