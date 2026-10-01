import type { SVGProps } from 'react';

// サイドバーと空の一覧で使うドット絵のアイコン。1 ドットを 1 単位で描き、色は currentColor で渡す

type PixelIconProps = Omit<SVGProps<SVGSVGElement>, 'viewBox'> & {
  size?: number;
};

function PixelIcon({
  children,
  height,
  size = 18,
  viewBox,
  width,
  ...props
}: PixelIconProps & { viewBox: string }) {
  return (
    <svg
      aria-hidden="true"
      fill="currentColor"
      height={height ?? size}
      shapeRendering="crispEdges"
      viewBox={viewBox}
      width={width ?? size}
      {...props}
    >
      {children}
    </svg>
  );
}

const starBody =
  'M4 0h1v1H4zM3 1h3v1H3zM0 2h9v1H0zM1 3h7v1H1zM2 4h5v2H2zM1 6h3v1H1zM5 6h3v1H5zM1 7h2v1H1zM6 7h2v1H6z';
const starEyes = 'M3 3h1v2H3zM5 3h1v2H5z';

/** 目のあるスター。目は eyeColor で塗り、背景の色に合わせる */
export function PixelStar({
  eyeColor,
  size = 24,
  ...props
}: PixelIconProps & { eyeColor: string }) {
  return (
    <PixelIcon
      height={size}
      viewBox="0 0 9 8"
      width={(size * 9) / 8}
      {...props}
    >
      <path d={starBody} />
      <path d={starEyes} fill={eyeColor} />
    </PixelIcon>
  );
}

/** 「VS」の文字 */
export function PixelVersus(props: PixelIconProps) {
  return (
    <PixelIcon viewBox="0 0 9 9" {...props}>
      <path d="M0 1h1v1H0zM3 1h1v1H3zM5 1h4v1H5zM0 2h1v1H0zM3 2h1v1H3zM5 2h1v1H5zM0 3h1v1H0zM3 3h1v1H3zM5 3h1v1H5zM0 4h1v1H0zM3 4h1v1H3zM5 4h4v1H5zM0 5h1v1H0zM3 5h1v1H3zM8 5h1v1H8zM1 6h2v1H1zM8 6h1v1H8zM1 7h2v1H1zM5 7h4v1H5z" />
    </PixelIcon>
  );
}

// 高さを VS・つまみと同じ 7 段にして、9×9 の枠の上下中央に置く
export function PixelRobot(props: PixelIconProps) {
  return (
    <PixelIcon viewBox="0 0 9 9" {...props}>
      <path d="M4 1h1v1H4zM1 2h7v1H1zM1 3h1v1H1zM7 3h1v1H7zM0 4h2v1H0zM3 4h1v1H3zM5 4h1v1H5zM7 4h2v1H7zM1 5h1v1H1zM7 5h1v1H7zM1 6h1v1H1zM3 6h3v1H3zM7 6h1v1H7zM1 7h7v1H1z" />
    </PixelIcon>
  );
}

export function PixelClock(props: PixelIconProps) {
  return (
    <PixelIcon viewBox="0 0 9 9" {...props}>
      <path d="M2 0h5v1H2zM1 1h1v1H1zM7 1h1v1H7zM0 2h1v1H0zM4 2h1v1H4zM8 2h1v1H8zM0 3h1v1H0zM4 3h1v1H4zM8 3h1v1H8zM0 4h1v1H0zM4 4h3v1H4zM8 4h1v1H8zM0 5h1v1H0zM8 5h1v1H8zM0 6h1v1H0zM8 6h1v1H8zM1 7h1v1H1zM7 7h1v1H7zM2 8h5v1H2z" />
    </PixelIcon>
  );
}

export function PixelSliders(props: PixelIconProps) {
  return (
    <PixelIcon viewBox="0 0 9 9" {...props}>
      <path d="M1 1h3v1H1zM0 2h9v1H0zM1 3h3v1H1zM5 5h3v1H5zM0 6h9v1H0zM5 7h3v1H5z" />
    </PixelIcon>
  );
}
