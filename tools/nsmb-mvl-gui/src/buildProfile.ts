export type AppEdition = 'insiders' | 'public';
export type RuntimeCapabilities = {
  aiDevTools: boolean;
  automaticUnresolvedSessionReport: boolean;
  configurableSignalServer: boolean;
  manualLogUpload: boolean;
  notifyOwnRooms: boolean;
};

const insidersFallback = {
  badge: 'Insiders',
  displayName: 'Bigstar Insiders',
  edition: 'insiders' as const,
};

const localInsidersCapabilities: RuntimeCapabilities = {
  aiDevTools: true,
  automaticUnresolvedSessionReport: true,
  configurableSignalServer: true,
  manualLogUpload: true,
  notifyOwnRooms: true,
};

export function areAiDevToolsEnabled() {
  return currentRuntimeCapabilities().aiDevTools;
}

export function currentEditionConfig() {
  return globalThis.__NSMB_MVL_EDITION_CONFIG__ ?? insidersFallback;
}

export function currentEdition(): AppEdition {
  return currentEditionConfig().edition;
}

export function currentRuntimeCapabilities(): RuntimeCapabilities {
  return (
    globalThis.__NSMB_MVL_RUNTIME_CAPABILITIES__ ?? localInsidersCapabilities
  );
}
