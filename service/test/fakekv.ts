/**
 * @file fakekv.ts
 * @description In-memory stand-in for a Workers KV namespace (no network), with pagination so the cursor loops are
 * exercised: `list` returns at most `pageSize` keys per call.
 */
import type { KvLike, KvListResult } from '../src/storage';

/** In-memory KV. */
export class FakeKv implements KvLike {
  readonly data = new Map<string, { value: string; metadata?: unknown; ttl?: number }>();
  /** Keys per `list` page (small, to test paging). */
  pageSize = 2;

  get(key: string): Promise<string | null> {
    return Promise.resolve(this.data.get(key)?.value ?? null);
  }

  put(
    key: string,
    value: string,
    options?: { expirationTtl?: number; metadata?: unknown },
  ): Promise<void> {
    this.data.set(key, { value, metadata: options?.metadata, ttl: options?.expirationTtl });
    return Promise.resolve();
  }

  delete(key: string): Promise<void> {
    this.data.delete(key);
    return Promise.resolve();
  }

  list<M = unknown>(options?: {
    prefix?: string;
    cursor?: string;
    limit?: number;
  }): Promise<KvListResult<M>> {
    const names = [...this.data.keys()].filter((k) => k.startsWith(options?.prefix ?? '')).sort();
    const start = options?.cursor ? Number(options.cursor) : 0;
    const size = Math.min(options?.limit ?? this.pageSize, this.pageSize);
    const slice = names.slice(start, start + size);
    const complete = start + size >= names.length;
    return Promise.resolve({
      keys: slice.map((name) => ({ name, metadata: this.data.get(name)?.metadata as M })),
      list_complete: complete,
      ...(complete ? {} : { cursor: String(start + size) }),
    });
  }

  /** Names of the stored reports. */
  reportKeys(): string[] {
    return [...this.data.keys()].filter((k) => k.startsWith('r:'));
  }
}
