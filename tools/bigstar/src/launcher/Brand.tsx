import { css } from 'styled-system/css';
import { currentEditionConfig } from '../buildProfile';
import { PixelStar } from './PixelIcons';

/** 星と BIGSTAR のロゴ。Insiders 版では下に版の名前を添える */
export function Brand({
  eyeColor,
  testIds = false,
}: {
  /** 星の目の色。置く場所の背景色にして、目を抜いて見せる */
  eyeColor: string;
  /** サイドバーのロゴだけに付け、テストが 1 つに絞れるようにする */
  testIds?: boolean;
}) {
  const edition = currentEditionConfig();

  return (
    <div
      className={css({
        alignItems: 'center',
        display: 'flex',
        gap: '2.5',
        userSelect: 'none',
        '& > *': { pointerEvents: 'none' },
      })}
      data-tauri-drag-region
    >
      <PixelStar
        className={css({ color: 'amber.9', flexShrink: '0' })}
        eyeColor={eyeColor}
      />
      <span
        className={css({
          display: 'flex',
          flexDirection: 'column',
          gap: '1',
        })}
        data-testid={testIds ? 'brand' : undefined}
      >
        <span
          className={css({
            fontFamily: 'display',
            fontSize: 'xl',
            letterSpacing: 'wider',
            lineHeight: 'none',
          })}
        >
          BIGSTAR
        </span>
        {edition.edition === 'insiders' ? (
          <span
            className={css({
              color: 'amber.9',
              fontFamily: 'mono',
              // 2xs（8px）だと読みにくいので、ロゴのこの文字だけトークン外の大きさにする
              fontSize: '[9.5px]',
              fontWeight: 'semibold',
              letterSpacing: 'widest',
              lineHeight: 'none',
              textTransform: 'uppercase',
            })}
            data-testid={testIds ? 'edition-badge' : undefined}
          >
            {edition.badge}
          </span>
        ) : null}
      </span>
    </div>
  );
}
