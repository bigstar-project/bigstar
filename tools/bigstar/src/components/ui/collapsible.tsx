'use client'
import { Collapsible as Base } from '@base-ui/react/collapsible'
import { collapsible } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'
import { ChevronDownIcon, withIcon } from './icons'
const { withProvider, withContext } = createStyleContext(collapsible)
export const Root = withProvider(Base.Root, 'root')
export const Trigger = withContext(Base.Trigger, 'trigger')
/** Put it last in Trigger: a chevron that turns while the panel is open; children replace it. */
export const Indicator = withIcon(withContext('span', 'indicator'), <ChevronDownIcon />)
export const Panel = withContext(Base.Panel, 'panel')
