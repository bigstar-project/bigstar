import type { ReactNode } from 'react';
import { css } from 'styled-system/css';

/** 各画面の見出し。右側に画面の主な操作を置ける */
export function PageHeader({
  actions,
  title,
}: {
  actions?: ReactNode;
  title: string;
}) {
  return (
    <header
      className={css({
        alignItems: 'center',
        display: 'flex',
        gap: '6',
        justifyContent: 'space-between',
        minH: '11',
      })}
    >
      <h1
        className={css({
          color: 'fg.default',
          fontWeight: 'bold',
          textStyle: '2xl',
        })}
      >
        {title}
      </h1>
      {actions}
    </header>
  );
}
