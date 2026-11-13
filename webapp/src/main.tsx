/**
 * @file main.tsx
 * @description Browser entry point: mounts the app, starts the live WebSocket connection and loads the
 * stylesheet. In dev builds the WebSocket is replaced by a mock (the branch is removed from production).
 */
import { render } from 'preact';
import { startLive } from './api/live';
import { App, isHosted } from './app';
import './style.css';

const root = document.getElementById('app');
if (!root) throw new Error('Missing #app mount point');
render(<App />, root);

if (import.meta.env.DEV) {
  void import('./mocks/mockSocket').then((m) => {
    startLive(m.mockSocketFactory);
  });
} else if (!isHosted()) {
  startLive(); // The hosted copy starts it after the USB connection (see App).
}
