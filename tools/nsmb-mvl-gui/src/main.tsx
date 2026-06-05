import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import './styles.css';

const app = document.querySelector<HTMLDivElement>('#app');

if (!app) {
  throw new Error('missing #app');
}

document.documentElement.classList.add('dark');

createRoot(app).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
