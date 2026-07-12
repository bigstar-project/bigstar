import { defineRecipe } from '@pandacss/dev';

export const surface = defineRecipe({
  className: 'surface',
  base: {
    borderColor: 'gray.surface.border',
    borderRadius: 'l2',
    borderWidth: '1px',
  },
  variants: {
    variant: {
      glass: {
        bg: 'app.card',
        backdropFilter: 'auto',
        backdropBlur: 'md',
        backdropSaturate: '180%',
        boxShadow: 'panel',
      },
      inset: {
        bg: 'app.panel',
      },
    },
  },
  defaultVariants: {
    variant: 'glass',
  },
});
