# ADR 3: Preact + Vite + TypeScript for the web app

Status: accepted

## Context

The web app is served from flash with a 150 KB gzipped budget and must work on phones.

## Decision

Use Preact with Vite and strict TypeScript, plain CSS, no UI framework.

## Consequences

Tiny bundle and fast builds; fewer ready-made widgets.
