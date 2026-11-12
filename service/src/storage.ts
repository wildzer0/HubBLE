/**
 * @file storage.ts
 * @description The small slice of the Workers KV API the service uses, so tests can supply an in-memory fake.
 */

/** One key returned by `list`, with its metadata. */
export interface KvKey<M> {
  name: string;
  metadata?: M;
}

/** Result page of `list`. */
export interface KvListResult<M> {
  keys: KvKey<M>[];
  list_complete: boolean;
  cursor?: string;
}

/** The KV operations used by the Worker (a subset of KVNamespace). */
export interface KvLike {
  get(key: string): Promise<string | null>;
  put(
    key: string,
    value: string,
    options?: { expirationTtl?: number; metadata?: unknown },
  ): Promise<void>;
  delete(key: string): Promise<void>;
  list<M = unknown>(options?: {
    prefix?: string;
    cursor?: string;
    limit?: number;
  }): Promise<KvListResult<M>>;
}

/** Bindings and secrets of the Worker. */
export interface Env {
  /** KV namespace holding the reports. */
  REPORTS: KvLike;
  /** Bearer token for the admin endpoints (unset: they always answer 401). */
  ADMIN_TOKEN?: string;
  /** Salt of the rate-limiter's client hash (unset: a built-in constant, weaker against enumeration). */
  RATE_SALT?: string;
}
