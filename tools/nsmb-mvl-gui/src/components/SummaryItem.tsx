import { css } from 'styled-system/css';

export function SummaryItem({
  label,
  value,
}: {
  label: string;
  value: string;
}) {
  return (
    <div
      className={css({
        display: 'grid',
        gap: '0.5',
      })}
    >
      <span
        className={css({
          color: 'fg.subtle',
          fontWeight: 'black',
          textStyle: 'xs',
        })}
      >
        {label}
      </span>
      <strong
        className={css({
          color: 'fg.default',
          overflowWrap: 'anywhere',
          textStyle: 'sm',
        })}
      >
        {value}
      </strong>
    </div>
  );
}
