/**
 * @file types.ts
 * @description Types of the firmware REST contract (see docs/api/openapi.yaml).
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
  /** Seconds until the Wi-Fi AP switches off, or null if it stays on. */
  ap_timeout_s: number | null;
  /** True until the setup wizard has set a Wi-Fi password. */
  setup_required: boolean;
}

/** Error body used by every failing endpoint. */
export interface ApiErrorBody {
  error: { code: string; message: string };
}
