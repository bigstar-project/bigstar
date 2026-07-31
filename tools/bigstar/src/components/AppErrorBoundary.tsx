import { Component, type ErrorInfo, type ReactNode } from 'react';
import { css } from 'styled-system/css';
import { recordAppError } from '../appDiagnostics';
import { Button } from './ui';

export class AppErrorBoundary extends Component<
  { children: ReactNode },
  { error: Error | null }
> {
  state: { error: Error | null } = { error: null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    void recordAppError(
      'react',
      'react.error_boundary',
      new Error(`${error.message}\n${info.componentStack ?? ''}`),
    );
  }

  render() {
    if (!this.state.error) return this.props.children;
    return (
      <main
        className={css({
          alignItems: 'center',
          bg: 'app.bg',
          color: 'fg.default',
          display: 'flex',
          flexDirection: 'column',
          gap: '4',
          h: 'dvh',
          justifyContent: 'center',
          p: '6',
          textAlign: 'center',
        })}
      >
        <div>
          <h1 className={css({ fontSize: 'xl', fontWeight: 'black' })}>
            画面の表示中にエラーが発生しました
          </h1>
          <p className={css({ color: 'fg.muted', mt: '2', textStyle: 'sm' })}>
            エラー情報は端末内の診断ログへ保存されました。
          </p>
        </div>
        <Button onClick={() => window.location.reload()}>再読み込み</Button>
      </main>
    );
  }
}
