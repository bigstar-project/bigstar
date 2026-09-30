'use client'
import { NumberField as Base } from '@base-ui/react/number-field'
import { numberField } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'
import { MinusIcon, PlusIcon, withIcon } from './icons'

const { withProvider, withContext } = createStyleContext(numberField)

export const Root = withProvider(Base.Root, 'root')
export const Group = withContext(Base.Group, 'group')
export const Input = withContext(Base.Input, 'input')
export const Increment = withIcon(withContext(Base.Increment, 'increment'), <PlusIcon />)
export const Decrement = withIcon(withContext(Base.Decrement, 'decrement'), <MinusIcon />)
export const ScrubArea = withContext(Base.ScrubArea, 'scrubArea')
export const ScrubAreaCursor = withContext(Base.ScrubAreaCursor, 'scrubAreaCursor')
