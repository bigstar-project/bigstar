/// <reference types="vite/client" />

declare const __BIGSTAR_GUI_VERSION__: string;
declare var __BIGSTAR_EDITION_CONFIG__:
  | {
      badge: string;
      displayName: string;
      edition: 'insiders' | 'public';
    }
  | undefined;
declare var __BIGSTAR_RUNTIME_CAPABILITIES__:
  | {
      aiDevTools: boolean;
      automaticUnresolvedSessionReport: boolean;
      configurableSignalServer: boolean;
      feedbackSubmission: boolean;
      notifyOwnRooms: boolean;
    }
  | undefined;
