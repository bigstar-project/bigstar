import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { NuqsAdapter } from 'nuqs/adapters/react';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { installGlobalAppErrorHandlers } from './appDiagnostics';
import { AppErrorBoundary } from './components/AppErrorBoundary';
import './styles.css';

const app = document.querySelector<HTMLDivElement>('#app');

if (!app) {
  throw new Error('missing #app');
}

document.documentElement.classList.add('dark');
installGlobalAppErrorHandlers();

const [legacyView, legacyQuery = ''] = window.location.hash.slice(1).split('?');
if (['battle', 'history', 'settings'].includes(legacyView)) {
  const search = new URLSearchParams(window.location.search);
  if (!search.has('view')) search.set('view', legacyView);
  for (const [key, value] of new URLSearchParams(legacyQuery)) {
    if (!search.has(key)) search.set(key, value);
  }
  window.history.replaceState(
    null,
    '',
    `${window.location.pathname}?${search.toString()}`,
  );
}

const queryClient = new QueryClient();

createRoot(app).render(
  <StrictMode>
    <AppErrorBoundary>
      <QueryClientProvider client={queryClient}>
        <NuqsAdapter>
          <App />
        </NuqsAdapter>
      </QueryClientProvider>
    </AppErrorBoundary>
  </StrictMode>,
);
