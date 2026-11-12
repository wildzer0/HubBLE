# HubBLE report service

A small Cloudflare Worker that collects the device-compatibility reports sent from the dongle's web app and publishes
aggregate statistics. The report format is `docs/api/device-report.schema.json`; the public page that explains it to
users is [`docs/compatibility.md`](../docs/compatibility.md).

| Endpoint               | Access | Purpose                                                                                                                        |
| ---------------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------ |
| `POST /v1/reports`     | public | Submit one report. `201 {"ok":true,"id":"..."}`, or `200` with `"duplicate":true` when the same device data is already stored. |
| `GET /v1/stats`        | public | Aggregate statistics (cached 5 minutes). No raw report maps.                                                                   |
| `GET /v1/reports/{id}` | admin  | One stored report.                                                                                                             |
| `GET /v1/export`       | admin  | Every report as NDJSON (one JSON document per line).                                                                           |

Errors are JSON: `{"ok":false,"error":{"code":"...","message":"..."}}` with `400 invalid_json` or `invalid_report`
(the message names the offending path), `413 too_large` (body over 16 KB), `429 rate_limited` (with `Retry-After`),
`401 unauthorized`, `404 not_found`, `405 method_not_allowed`. CORS allows any origin, because the web app is served over
plain `http://192.168.4.1` or `http://hubble.local`.

## Storage design

Workers KV, not D1: a report is written once and read back only by the maintainer, so key/value is enough. Each report
is stored under `r:<id>`, where the id is a hash of the device data, so de-duplication is a key lookup; a compact
summary (no report map, no free text) is stored as the key's _metadata_, so `GET /v1/stats` is computed from a few
`list` calls without reading any report.

## Deploy

You need a Cloudflare account (the free plan is enough) and Node.js 20 or later.

```sh
cd service
npm ci
npx wrangler login
npx wrangler kv namespace create REPORTS      # prints an id: paste it into wrangler.toml (kv_namespaces.id)
npx wrangler secret put ADMIN_TOKEN           # a long random string, e.g. from `openssl rand -hex 32`
npx wrangler secret put RATE_SALT             # another random string
npx wrangler deploy                           # prints https://hubble-reports.<account>.workers.dev
```

Check it: `curl https://hubble-reports.<account>.workers.dev/v1/stats` returns `{"total":0,...}`.

To use a domain of your own, add a `routes` entry to `wrangler.toml` (or attach a custom domain in the Cloudflare
dashboard) and redeploy.

### Telling the web app where to send reports

The web app reads the address at build time: set `VITE_REPORT_URL` to the base URL **without** a trailing slash, for
example `VITE_REPORT_URL=https://hubble-reports.example.workers.dev npm run build` (in CI, a repository variable passed
to the build step). Without it the web app only offers to download the report as a file.

The statistics reach the public compatibility page with `tools/build_compat_page.py`:

```sh
python3 tools/build_compat_page.py https://hubble-reports.example.workers.dev/v1/stats
```

## Local development and tests

```sh
npm test            # Vitest, with an in-memory fake of KV (no network)
npm run typecheck
npm run dev         # wrangler dev: a local Worker with a local KV
```

## Privacy

What is stored, exactly:

- **Reports** (`r:<id>`): the JSON document the user saw and agreed to send: firmware version and board, and for one
  peripheral its kind, advertised name, GAP appearance, Device Information strings (manufacturer, model, firmware
  revision), PnP ID, report map (hex), parse result, security level, connection parameters, whether it exposes a battery
  level, latency figures, and optionally a note typed by the user. No keystrokes, no Bluetooth address, no name the user
  gave the device, no Wi-Fi data. They are kept until the maintainer deletes them: there is no automatic expiry.
- **Rate-limit counters** (`rl:<hash>:<hour>`): a counter keyed by a SHA-256 hash of the client address mixed with a
  secret salt, truncated. The address itself is never stored, and the key expires after two hours.
- **Statistics cache** (`stats:cache`): the public document, five minutes.

The Worker does not log request bodies or addresses (`observability` is disabled in `wrangler.toml` and the code never
calls `console`). Cloudflare itself processes requests as the network provider; see its privacy policy.

## Abuse handling

- Reports are validated strictly (unknown properties are rejected), limited to 16 KB, and each client may send 20
  requests per hour (every request counts, valid or not).
- Identical device data is stored once, so repeating a report costs nothing.
- To remove a report, delete its key: `npx wrangler kv key delete --binding REPORTS "r:<id>"` (add `--remote`), then
  delete `stats:cache` the same way so the statistics are rebuilt. Use `GET /v1/export` to find ids by content.
- To stop a flood, lower `RATE_LIMIT_PER_HOUR` in `src/index.ts`, or add a Cloudflare rate-limiting rule for the path.
- Rotate the admin token with `wrangler secret put ADMIN_TOKEN`.
