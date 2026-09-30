// Kiso の条件は Base UI の属性（data-open / data-checked）だけを見る。
// 旧 Park UI（Ark UI）の部品は data-state を使うので、移行が終わるまで両方に一致させる。
export const parkUiConditions = {
  open: '&:is([data-open], [data-state=open])',
  closed: '&:is([data-closed], [data-state=closed])',
  checked:
    '&:is(:checked, [data-checked], [data-state=checked], [aria-checked=true], [data-state=indeterminate])',
  invalid: '&:is(:user-invalid, [data-invalid], [aria-invalid=true])',
  on: '&:is([data-state=on])',
  pinned: '&:is([data-pinned])',
} as const;
