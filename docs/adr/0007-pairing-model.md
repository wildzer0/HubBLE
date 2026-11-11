# ADR 7: Logitech-style pairing model

Status: accepted

## Context

Mice usually only support Just Works, so authenticity needs another factor.

## Decision

Pair only from the web app, LE Secure Connections, passkey for keyboards and a quarantine challenge for Just Works devices (CLAUDE.md §3.4).

## Consequences

Clear security levels; the challenge does not stop an active radio MITM, which the docs must state.
