import { absoluteCenter } from './absolute-center';
import { badge } from './badge';
import { button } from './button';
import { card } from './card';
import { collapsible } from './collapsible';
import { dialog } from './dialog';
import { field } from './field';
import { group } from './group';
import { input } from './input';
import { kbd } from './kbd';
import { numberInput } from './number-input';
import { select } from './select';
import { spinner } from './spinner';
import { surface } from './surface';
import { switchRecipe } from './switch';
import { tabs } from './tabs';
import { textarea } from './textarea';
export const recipes = {
  parkButton: button,
  parkSelect: select,
  parkTabs: tabs,
  parkBadge: badge,
  parkCard: card,
  parkField: field,
  parkInput: input,
  numberInput,
  group,
  absoluteCenter,
  parkSpinner: spinner,
  parkDialog: dialog,
  parkCollapsible: collapsible,
  parkSwitch: switchRecipe,
  surface,
  parkKbd: kbd,
  parkTextarea: textarea,
};
export const slotRecipes = {};
