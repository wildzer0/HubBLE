# ADR 2: NimBLE as BLE stack

Status: accepted

## Context

We need several simultaneous BLE central connections with small flash/RAM use.

## Decision

Use NimBLE (not Bluedroid).

## Consequences

Smaller footprint; HOGP client must be written on top of the NimBLE GATT client API.
