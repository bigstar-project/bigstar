import { commands } from './bindings';

type AppErrorSource = 'gui' | 'react' | 'tauri' | 'updater' | 'matchmaking';

let reporting = false;

function errorDetails(error: unknown) {
  if (error instanceof Error) {
    return {
      message: error.message || error.name,
      stack: error.stack ?? null,
    };
  }
  return { message: String(error), stack: null };
}

export async function recordAppError(
  source: AppErrorSource,
  operation: string,
  error: unknown,
) {
  if (
    reporting ||
    typeof window === 'undefined' ||
    !('__TAURI_INTERNALS__' in window)
  )
    return;
  const details = errorDetails(error);
  reporting = true;
  try {
    await commands.recordAppError({
      source,
      operation,
      message: details.message,
      stack: details.stack,
    });
  } catch {
    // 診断記録自体の失敗は再帰的に記録しない。
  } finally {
    reporting = false;
  }
}

export function installGlobalAppErrorHandlers() {
  window.addEventListener('error', (event) => {
    void recordAppError('gui', 'window.error', event.error ?? event.message);
  });
  window.addEventListener('unhandledrejection', (event) => {
    void recordAppError('gui', 'window.unhandledrejection', event.reason);
  });
  void recordAppContext();
}

async function recordAppContext() {
  if (!('__TAURI_INTERNALS__' in window)) return;
  const navigatorWithMemory = navigator as Navigator & {
    deviceMemory?: number;
  };
  try {
    await commands.recordAppContext({
      user_agent: navigator.userAgent,
      language: navigator.language,
      hardware_concurrency: navigator.hardwareConcurrency || 0,
      device_memory_gib: navigatorWithMemory.deviceMemory ?? null,
      screen_width: window.screen.width,
      screen_height: window.screen.height,
      device_pixel_ratio: window.devicePixelRatio,
      gpu_renderer: gpuRenderer(),
    });
  } catch (error) {
    void recordAppError('gui', 'app_context', error);
  }
}

function gpuRenderer() {
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('webgl');
  if (!context) return null;
  const extension = context.getExtension('WEBGL_debug_renderer_info') as {
    UNMASKED_RENDERER_WEBGL: number;
  } | null;
  if (!extension) return null;
  const renderer = context.getParameter(extension.UNMASKED_RENDERER_WEBGL);
  return typeof renderer === 'string' ? renderer : null;
}
