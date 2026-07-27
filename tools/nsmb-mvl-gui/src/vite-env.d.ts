/// <reference types="vite/client" />

declare const __NSMB_MVL_GUI_VERSION__: string;
declare var __NSMB_MVL_EDITION_CONFIG__:
  | {
      badge: string;
      displayName: string;
      edition: 'insiders' | 'public';
    }
  | undefined;
declare var __NSMB_MVL_RUNTIME_CAPABILITIES__:
  | {
      aiDevTools: boolean;
      automaticUnresolvedSessionReport: boolean;
      configurableSignalServer: boolean;
      manualLogUpload: boolean;
      notifyOwnRooms: boolean;
    }
  | undefined;
