import { defineGlobalStyles } from '@pandacss/dev';
import { globalCss as kisoGlobalCss } from '../global-css';

// Kiso の globalCss に、Bigstar だけの指定を重ねる
export const globalCss = defineGlobalStyles({
  ...kisoGlobalCss,
  html: {
    ...kisoGlobalCss.html,
    overscrollBehavior: 'none',
  },
  body: {
    ...kisoGlobalCss.body,
    overscrollBehavior: 'none',
  },
});
