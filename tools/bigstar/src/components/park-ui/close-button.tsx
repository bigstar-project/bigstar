import { XIcon } from 'lucide-react';
import { forwardRef } from 'react';
import { IconButton, type IconButtonProps } from './icon-button';

export type CloseButtonProps = IconButtonProps;

// colorPalette を JSX 属性と {...props} の両方で渡すと、strictTokens の型を合成しきれず
// TS2590 になる。既定値はオブジェクトにまとめて展開する。
const closeButtonDefaults: CloseButtonProps = {
  'aria-label': 'Close',
  colorPalette: 'gray',
  variant: 'plain',
};

export const CloseButton = forwardRef<HTMLButtonElement, CloseButtonProps>(
  function CloseButton(props, ref) {
    return (
      <IconButton {...closeButtonDefaults} ref={ref} {...props}>
        {props.children ?? <XIcon />}
      </IconButton>
    );
  },
);
