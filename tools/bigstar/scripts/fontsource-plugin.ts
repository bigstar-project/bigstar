import type { Plugin } from 'vite';

export function fontsourcePlugin(): Plugin {
  return {
    name: 'bigstar-fontsource',
    enforce: 'pre',
    transform(code, id) {
      const path = id.split('?')[0].replaceAll('\\', '/');
      if (!/\/@fontsource\/.+\.css$/.test(path)) return null;

      // WebView2 は WOFF2 対応。和文フォントだけで約6MBある WOFF 代替を省く。
      let css = code.replace(/,\s*url\([^)]+\.woff\) format\('woff'\)/g, '');

      if (path.includes('/@fontsource/ibm-plex-sans-jp/')) {
        // 元の106% / 44%の合計150%を保ち、組版用88% / 12%と同じ上下差にする。
        // 字形や幅を変えず、本文・入力欄・ボタンに共通する上寄りの描画を補正する。
        // ウェイトと unicode-range ごとの既存定義に追加し、分割配信を維持する。
        css = css.replace(
          /@font-face\s*\{/g,
          '@font-face {\n  ascent-override: 113%;\n  descent-override: 37%;\n  line-gap-override: 0%;',
        );
      }

      return css;
    },
  };
}
