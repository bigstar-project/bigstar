import { ArrowLeft, ArrowRight } from '@phosphor-icons/react';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { type ReactNode, useEffect, useState } from 'react';
import { css } from 'styled-system/css';

type NavigationController = EventTarget & {
  back: () => unknown;
  canGoBack: boolean;
  canGoForward: boolean;
  forward: () => unknown;
};

function browserNavigation() {
  return (window as Window & { navigation?: NavigationController }).navigation;
}

function isTauriRuntime() {
  return '__TAURI_INTERNALS__' in window && !('__BIGSTAR_E2E__' in window);
}

export function AppTitlebar() {
  const navigation = browserNavigation();
  const [canGoBack, setCanGoBack] = useState(
    navigation?.canGoBack ?? window.history.length > 1,
  );
  const [canGoForward, setCanGoForward] = useState(
    navigation?.canGoForward ?? false,
  );
  const [maximized, setMaximized] = useState(false);

  useEffect(() => {
    const updateNavigation = () => {
      const current = browserNavigation();
      setCanGoBack(current?.canGoBack ?? window.history.length > 1);
      setCanGoForward(current?.canGoForward ?? false);
    };
    const current = browserNavigation();
    current?.addEventListener('currententrychange', updateNavigation);
    window.addEventListener('popstate', updateNavigation);
    return () => {
      current?.removeEventListener('currententrychange', updateNavigation);
      window.removeEventListener('popstate', updateNavigation);
    };
  }, []);

  useEffect(() => {
    if (!isTauriRuntime()) return;
    const appWindow = getCurrentWindow();
    let unlisten: (() => void) | undefined;
    void appWindow
      .isMaximized()
      .then(setMaximized)
      .catch(() => undefined);
    void appWindow
      .onResized(() => {
        void appWindow
          .isMaximized()
          .then(setMaximized)
          .catch(() => undefined);
      })
      .then((cleanup) => {
        unlisten = cleanup;
      })
      .catch(() => undefined);
    return () => unlisten?.();
  }, []);

  return (
    <div className={titlebarClassName}>
      <div
        className={css({ alignItems: 'center', display: 'flex', gap: '0.5' })}
      >
        <TitlebarButton
          ariaLabel="戻る"
          disabled={!canGoBack}
          onClick={() => browserNavigation()?.back() ?? window.history.back()}
        >
          <ArrowLeft size={16} weight="bold" />
        </TitlebarButton>
        <TitlebarButton
          ariaLabel="進む"
          disabled={!canGoForward}
          onClick={() =>
            browserNavigation()?.forward() ?? window.history.forward()
          }
        >
          <ArrowRight size={16} weight="bold" />
        </TitlebarButton>
      </div>

      <div className={css({ h: 'full', minW: '0' })} data-tauri-drag-region />

      <div
        className={css({ alignItems: 'stretch', display: 'flex', h: 'full' })}
      >
        <WindowButton
          ariaLabel="最小化"
          onClick={() => void getCurrentWindow().minimize()}
        >
          <WindowsCaptionIcon glyph={'\uE921'} />
        </WindowButton>
        <WindowButton
          ariaLabel={maximized ? '元のサイズに戻す' : '最大化'}
          onClick={() => void getCurrentWindow().toggleMaximize()}
        >
          {maximized ? (
            <WindowsCaptionIcon glyph={'\uE923'} />
          ) : (
            <WindowsCaptionIcon glyph={'\uE922'} />
          )}
        </WindowButton>
        <WindowButton
          ariaLabel="閉じる"
          close
          onClick={() => void getCurrentWindow().close()}
        >
          <WindowsCaptionIcon glyph={'\uE8BB'} />
        </WindowButton>
      </div>
    </div>
  );
}

function WindowsCaptionIcon({ glyph }: { glyph: string }) {
  return (
    <span
      aria-hidden="true"
      className={css({ fontSize: '[10px]' })}
      style={{
        fontFamily: "'Segoe Fluent Icons', 'Segoe MDL2 Assets'",
        lineHeight: 1,
      }}
    >
      {glyph}
    </span>
  );
}

function TitlebarButton({
  ariaLabel,
  children,
  disabled,
  onClick,
}: {
  ariaLabel: string;
  children: ReactNode;
  disabled: boolean;
  onClick: () => void;
}) {
  return (
    <button
      aria-label={ariaLabel}
      className={navigationButtonClassName}
      disabled={disabled}
      onClick={onClick}
      type="button"
    >
      {children}
    </button>
  );
}

function WindowButton({
  ariaLabel,
  children,
  close = false,
  onClick,
}: {
  ariaLabel: string;
  children: ReactNode;
  close?: boolean;
  onClick: () => void;
}) {
  return (
    <button
      aria-label={ariaLabel}
      className={css({
        alignItems: 'center',
        color: 'fg.muted',
        cursor: 'pointer',
        display: 'flex',
        justifyContent: 'center',
        transition: 'common',
        w: '11',
        _hover: close
          ? { bg: 'red.solid.bg', color: 'white' }
          : { bg: 'white.a2', color: 'fg.default' },
      })}
      onClick={onClick}
      type="button"
    >
      {children}
    </button>
  );
}

const titlebarClassName = css({
  alignItems: 'center',
  backdropBlur: 'md',
  backdropFilter: 'auto',
  bg: 'app.sidebar',
  borderBottomColor: 'gray.surface.border',
  borderBottomWidth: '1px',
  display: 'grid',
  gridTemplateColumns: 'auto minmax(0, 1fr) auto',
  h: '8',
  pl: '1.5',
  position: 'sticky',
  top: '0',
  userSelect: 'none',
  zIndex: 'banner',
});

const navigationButtonClassName = css({
  alignItems: 'center',
  borderRadius: 'l1',
  color: 'fg.muted',
  cursor: 'pointer',
  display: 'flex',
  h: '7',
  justifyContent: 'center',
  transition: 'common',
  w: '7',
  _hover: { bg: 'white.a2', color: 'fg.default' },
  _disabled: { color: 'fg.subtle', cursor: 'default', opacity: '0.38' },
});
