export type AppEdition = 'insiders' | 'public';
export type RuntimeCapabilities = {
  soloTest: boolean;
  aiDevTools: boolean;
  automaticUnresolvedSessionReport: boolean;
  configurableSignalServer: boolean;
  feedbackSubmission: boolean;
  notifyOwnRooms: boolean;
};

const insidersFallback = {
  badge: 'Insiders',
  displayName: 'Bigstar Insiders',
  edition: 'insiders' as const,
};

const localInsidersCapabilities: RuntimeCapabilities = {
  // Solo testing requires an explicitly injected local build capability.
  soloTest: false,
  aiDevTools: true,
  automaticUnresolvedSessionReport: true,
  configurableSignalServer: true,
  feedbackSubmission: true,
  notifyOwnRooms: true,
};

export function areAiDevToolsEnabled() {
  return currentRuntimeCapabilities().aiDevTools;
}

export function currentEditionConfig() {
  return globalThis.__BIGSTAR_EDITION_CONFIG__ ?? insidersFallback;
}

export function currentEdition(): AppEdition {
  return currentEditionConfig().edition;
}

export function currentRuntimeCapabilities(): RuntimeCapabilities {
  return (
    globalThis.__BIGSTAR_RUNTIME_CAPABILITIES__ ?? localInsidersCapabilities
  );
}
