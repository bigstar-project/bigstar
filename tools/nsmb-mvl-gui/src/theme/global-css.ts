export const globalCss = {
  extend: {
    '*': {
      '--global-color-border': 'colors.border',
      '--global-color-placeholder': 'colors.fg.subtle',
      // '--global-color-selection': 'colors.colorPalette.subtle.bg',
      '--global-color-focus-ring': 'colors.colorPalette.solid.bg',
    },
    html: {
      colorPalette: 'gray',
      colorScheme: 'dark',
    },
    body: {
      // background: 'app.bg',
      // color: 'fg.default',
      fontFamily: 'body',
      // lineHeight: '1.5',
      // margin: '0',
      // minWidth: 'appMin',

      background: 'app.bg',
      color: 'fg.default',
    },
    // 'button, input, select': {
    //   font: 'inherit',
    // },
    // button: {
    //   cursor: 'pointer',
    // },
    // 'h1, h2, p': {
    //   letterSpacing: '0',
    //   margin: '0',
    // },
    // 'input::selection': {
    //   background: 'blue.a5',
    // },
    // '::-webkit-scrollbar': {
    //   width: '2.5',
    //   height: '2.5',
    // },
    // '::-webkit-scrollbar-track': {
    //   background: 'app.bg',
    // },
    // '::-webkit-scrollbar-thumb': {
    //   background: 'gray.6',
    //   borderColor: 'app.bg',
    //   borderRadius: 'full',
    //   borderStyle: 'solid',
    //   borderWidth: '0.5',
    // },
  },
};
