/**
 * @file client.ts
 * @description Minimal typed client for the firmware REST API.
 * State-changing calls must send `X-HubBLE: 1` and a JSON content type
 * (CLAUDE.md §3.12, simple CSRF protection); {@link request} does this.
 */
import type { ApiErrorBody, Status } from './types';

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
  const data = (await res.json()) as T | ApiErrorBody;
  if (!res.ok) {
    const err = (data as ApiErrorBody).error;
    throw new ApiError(res.status, err.code, err.message);
  }
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
