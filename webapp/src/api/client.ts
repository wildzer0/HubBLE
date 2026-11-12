/**
 * @file client.ts
 * @description Typed client for the firmware REST API.
 * State-changing calls must send `X-HubBLE: 1` and a JSON (or octet-stream for uploads) content type
 * (CLAUDE.md §3.12, simple CSRF protection); {@link request} does this.
 */
import type {
  ApiErrorBody,
  Backup,
  Device,
  DeviceList,
  Diagnostics,
  OtaStatus,
  ReportMap,
  ScanResponse,
  Settings,
  Status,
} from './types';

/** Error thrown for non-2xx API responses. */
export class ApiError extends Error {
  /**
   * @param status HTTP status code.
   * @param code Machine readable error code from the response body.
   * @param message Human readable message.
   */
  constructor(
    public readonly status: number,
    public readonly code: string,
    message: string,
  ) {
    super(message);
    this.name = 'ApiError';
  }
}

/**
 * Turn a failed response body into an {@link ApiError}.
 *
 * @param status HTTP status.
 * @param body Parsed body (may not be an error object).
 * @returns The error to throw.
 */
function toApiError(status: number, body: unknown): ApiError {
  const e = (body as Partial<ApiErrorBody> | null)?.error;
  return new ApiError(status, e?.code ?? 'http_error', e?.message ?? `HTTP ${String(status)}`);
}

/**
 * Perform an API request and parse the JSON response.
 *
 * @param method HTTP method.
 * @param path Path below the origin, e.g. `/api/v1/status`.
 * @param body Optional JSON body (state-changing calls).
 * @returns The parsed response body.
 * @throws {ApiError} when the server answers with a non-2xx status.
 */
export async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = { Accept: 'application/json' };
  if (method !== 'GET') {
    headers['X-HubBLE'] = '1';
    headers['Content-Type'] = 'application/json';
  }
  const init: RequestInit = { method, headers };
  if (body !== undefined) init.body = JSON.stringify(body);
  const res = await fetch(path, init);
  const data: unknown = await res.json();
  if (!res.ok) throw toApiError(res.status, data);
  return data as T;
}

/**
 * Fetch the dongle status.
 *
 * @returns The current {@link Status}.
 */
export function getStatus(): Promise<Status> {
  return request<Status>('GET', '/api/v1/status');
}

/**
 * Fetch the OTA slots and state.
 *
 * @returns The current {@link OtaStatus}.
 */
export function getOtaStatus(): Promise<OtaStatus> {
  return request<OtaStatus>('GET', '/api/v1/ota/status');
}

/**
 * Fetch system diagnostics.
 *
 * @returns The current {@link Diagnostics}.
 */
export function getDiagnostics(): Promise<Diagnostics> {
  return request<Diagnostics>('GET', '/api/v1/diagnostics');
}

/**
 * Change the Wi-Fi access point password. The dongle restarts its AP afterwards, so the phone will
 * be disconnected and must rejoin with the new password.
 *
 * @param password New password (validated by the firmware too).
 */
export async function setWifiPassword(password: string): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/settings/wifi-password', { password });
}

/** Reboot the dongle. */
export async function reboot(): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/system/reboot', {});
}

/** Reboot the dongle into the ROM USB download mode (for a USB re-flash without the BOOT button). */
export async function rebootDownload(): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/system/reboot-download', {});
}

/** Reset the dongle to factory state: settings, Wi-Fi password and all bonds are erased, then it reboots. */
export async function factoryReset(): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/system/factory-reset', {});
}

/**
 * List the bonded devices.
 *
 * @returns The device list.
 */
export function getDevices(): Promise<DeviceList> {
  return request<DeviceList>('GET', '/api/v1/devices');
}

/**
 * Fetch one device.
 *
 * @param id Device id (12 hex digits).
 * @returns The device.
 */
export function getDevice(id: string): Promise<Device> {
  return request<Device>('GET', `/api/v1/devices/${id}`);
}

/**
 * Rename a device.
 *
 * @param id Device id.
 * @param name New name (1..31 characters; validated by the firmware).
 * @returns The updated device.
 */
export function renameDevice(id: string, name: string): Promise<Device> {
  return request<Device>('PATCH', `/api/v1/devices/${id}`, { name });
}

/**
 * Forget a device and delete its bond.
 *
 * @param id Device id.
 */
export async function forgetDevice(id: string): Promise<void> {
  await request<{ ok: boolean }>('DELETE', `/api/v1/devices/${id}`);
}

/**
 * Read the raw report map of a connected device.
 *
 * @param id Device id.
 * @returns The hex report map (409 `not_connected` if the device is offline).
 */
export function getReportMap(id: string): Promise<ReportMap> {
  return request<ReportMap>('GET', `/api/v1/devices/${id}/report-map`);
}

/** Start a 30 s scan for HID devices. */
export async function startScan(): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/pairing/scan', {});
}

/**
 * Read the pairing state and the scan results.
 *
 * @returns Pairing status and found devices.
 */
export function getScan(): Promise<ScanResponse> {
  return request<ScanResponse>('GET', '/api/v1/pairing/scan');
}

/**
 * Start pairing with a scanned device.
 *
 * @param address 12 hex digits.
 * @param addressType BLE address type from the scan result.
 * @param allowLegacy Per-pairing override accepting legacy pairing (security level red).
 */
export async function startPairing(
  address: string,
  addressType: number,
  allowLegacy: boolean,
): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/pairing/start', {
    address,
    address_type: addressType,
    allow_legacy: allowLegacy,
  });
}

/** Cancel scanning or pairing. */
export async function cancelPairing(): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/pairing/cancel', {});
}

/**
 * Read the settings.
 *
 * @returns Current settings (never secrets).
 */
export function getSettings(): Promise<Settings> {
  return request<Settings>('GET', '/api/v1/settings');
}

/**
 * Update settings; only the given fields change.
 *
 * @param patch Fields to change.
 * @returns The settings after the update.
 */
export function putSettings(patch: Partial<Settings>): Promise<Settings> {
  return request<Settings>('PUT', '/api/v1/settings', patch);
}

/**
 * Download the settings backup (no secrets, no bonds).
 *
 * @returns The backup document.
 */
export function getBackup(): Promise<Backup> {
  return request<Backup>('GET', '/api/v1/backup');
}

/**
 * Restore a settings backup.
 *
 * @param backup Parsed backup document.
 */
export async function restoreBackup(backup: Backup): Promise<void> {
  await request<{ ok: boolean }>('POST', '/api/v1/restore', backup);
}

/** Successful upload response. */
export interface UploadResult {
  /** Version of the installed image. */
  version: string;
}

/**
 * Upload a firmware image. Uses XMLHttpRequest because `fetch` cannot report upload progress.
 *
 * @param file The `.bin` file; it is streamed to flash by the dongle, never buffered in RAM.
 * @param onProgress Called with a fraction 0..1 while uploading.
 * @returns The installed version.
 * @throws {ApiError} if the dongle rejects the image or the connection fails.
 */
export function uploadFirmware(
  file: File,
  onProgress: (fraction: number) => void,
): Promise<UploadResult> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/v1/ota/upload');
    xhr.setRequestHeader('X-HubBLE', '1');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) onProgress(e.loaded / e.total);
    };
    xhr.onload = () => {
      let body: unknown = null;
      try {
        body = JSON.parse(xhr.responseText);
      } catch {
        /* Fall through to the generic error below. */
      }
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve({ version: (body as UploadResult | null)?.version ?? '' });
      } else {
        reject(toApiError(xhr.status, body));
      }
    };
    xhr.onerror = () => {
      reject(new ApiError(0, 'network_error', 'Connection to the dongle was lost'));
    };
    xhr.send(file);
  });
}
