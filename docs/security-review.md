# Security review (M8)

Review of the implementation against the threat model in [Security](security.md). Method: reading the code paths that
handle untrusted input, fuzzing the parsers, running the host tests under AddressSanitizer/LeakSanitizer/UBSan, and
checking each mitigation listed in `security.md`. Nothing here replaces testing on hardware; items that need it are marked.

## Findings and fixes

| # | Finding | Severity | Status |
|---|---|---|---|
| 1 | **Cross-site WebSocket hijacking:** browsers do not apply the same-origin policy to WebSockets, so a web page open in a phone's other tab could connect to `ws://192.168.4.1/api/v1/ws` and read pairing passkeys, logs and (when subscribed) the input tester. | High | **Fixed:** the handshake is refused unless the `Host` names the dongle and the `Origin`, when present, is that same host (`http_guard_ws_ok`, host tests). |
| 2 | The API only compared `Origin` with `Host`, so a page served under another name that resolves to the dongle (DNS rebinding) passed the check. | Medium | **Fixed:** state-changing calls also require the `Host` to be `192.168.4.1` or `hubble.local` (`http_guard_host_ours`, exact match, host tests). |
| 3 | The captive-portal redirect decided "ours" by prefix, so `192.168.4.1.example.org` was served instead of redirected. | Low | **Fixed:** same strict matcher. |
| 4 | **Signed integer overflow in the HID normaliser:** a report map declaring many 32-bit axis fields of the same usage made the per-report sum overflow (undefined behaviour). Found by the fuzzer within minutes. Impact on the device: a wrong pointer delta from a hostile peripheral, no memory corruption. | Low | **Fixed:** saturating addition, regression test (fails without the fix). |
| 5 | Parsers of untrusted input had no sustained fuzzing. | n/a | **Added:** fuzz targets for the HID report-map parser/normaliser (any BLE peripheral), the manifest/GitHub answer parser (network) and the captive-portal DNS responder (any Wi-Fi client); libFuzzer with Clang in CI (bounded), `tools/fuzz.sh` locally (a mutation driver when Clang is missing). After the fix above, runs of 5-8 minutes per target found nothing else. This is shallow compared with a long coverage-guided campaign: run `tools/fuzz.sh 3600` before releases when possible. |
| 6 | Host tests were only partly sanitized. | n/a | **Added:** every host test also runs under ASan + LSan + UBSan (CI and `tools/check.sh full`): no findings. |

## Threat by threat

### Attacker within radio range

- Wi-Fi: WPA2-PSK, the setup wizard forces a new password in the web app. *Residual:* the firmware itself does not force the
  change, so until the wizard is completed anyone who knows the public default password can use the API; deauthentication
  floods are not mitigated (no 802.11w on the SoftAP).
- Bluetooth: only bonded devices are allowed (controller allow-list); encryption is required before HID discovery; legacy
  pairing is refused by default (override per pairing, red badge); Just Works devices are muted until a challenge passes.
  *Residual (documented):* an active radio MITM during the pairing window against a Just Works device is not prevented.

### Attacker who knows the Wi-Fi password

- State-changing calls need `X-HubBLE: 1`, a JSON (or octet-stream) content type, a same-host `Origin` and our `Host`; the
  WebSocket handshake is guarded the same way.
- Pairing: 60 s window, one device, 30 s challenge, three failures lock pairing for 60 s; the physical confirmation (Ctrl + Alt on
  a paired keyboard) protects new pairings and online installs when a keyboard is paired.
- *Residual:* the Wi-Fi password is the only authentication. Anyone holding it can read logs, the passkey of a pairing in
  progress and, while a page subscribes, the input tester stream; can upload signed firmware; can factory reset.
  This is by design (single-user device) and stated in the user guide.

### Malicious or tampered OTA image

- Images must carry a valid RSA-3072 signature from the release key (`esp_ota_end`), match the board marker and the
  project name, and pass the ESP-IDF checksum/SHA-256; the online path additionally checks the manifest SHA-256 before the
  slot is selected, refuses older versions without confirmation and respects `min_version`. Downloads use HTTPS with the
  certificate bundle; redirects are followed to any host (GitHub's asset store) but the signature, not the host, is the trust anchor.
- The asset file name from the manifest is restricted to `[A-Za-z0-9._+-]` (no path or URL tricks); sizes are bounded.
- Rollback: a new image must pass a self-test within 60 s or the bootloader returns to the previous one; a boot-failure counter covers crash loops.
- *Residual:* no hardware secure boot or flash encryption (physical access or a USB re-flash can install anything; stored
  Wi-Fi credentials are readable from flash); a leaked release key allows malicious updates until devices are re-flashed
  (`key-management.md`). To be verified on hardware: that a wrongly signed image is refused on the device (`testing/m6.md` A).

### Malicious peripheral (malformed report maps and reports)

- The report-map parser and normaliser are fuzzed and run under sanitizers; the parser bounds every collection
  (fields, collections, usages, report IDs) and keeps a usable partial layout on error; unknown usages are ignored.
  The report-map read is bounded, notifications are decoded in place with length checks, and a peripheral that does not
  provide a usable map falls back to the boot keyboard layout.
- A quarantined device cannot inject input into the host.

### Keystroke leakage

- No payloads are logged: reviewed all log statements on the report path, the HOGP client and the pairing code; passkeys
  and challenges are only returned by the API while displayed. Keystrokes are never persisted. The input tester streams
  only to sockets that subscribed in the last 15 s, paced and best effort, and the web page keeps nothing but the last 50 lines in memory.
- *Residual:* the passkey and challenge are visible to anyone who can reach the API during a pairing (they are meant to be shown to the user).

### Other

- The captive-portal DNS responder is fuzzed and answers every query with the dongle's address by design.
- The HTTP server rejects oversized JSON bodies and WebSocket frames; uploads stream to flash and are not buffered.
- Supply chain: GitHub Actions pinned by commit SHA, Dependabot for Actions and npm, lockfile installs, signing key only in a
  secret. The docs site loads the ESP Web Tools script from a pinned version on unpkg without subresource integrity (docs only; the
  web app served by the dongle loads nothing from outside).

## Open items for hardware

Signature rejection on the device, TLS to GitHub, behaviour under the soak tests in `testing/stability.md`.
