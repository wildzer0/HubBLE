/**
 * @file main.tsx
 * @description Browser entry point: mounts the app and loads the stylesheet.
 */
import { render } from 'preact';
import { App } from './app';
import './style.css';

const root = document.getElementById('app');
if (!root) throw new Error('Missing #app mount point');
render(<App />, root);
