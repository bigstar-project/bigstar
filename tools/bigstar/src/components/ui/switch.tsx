'use client'
import { Switch as Base } from '@base-ui/react/switch'
import { switchRecipe } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'

const { withProvider, withContext } = createStyleContext(switchRecipe)

export const Root = withProvider(Base.Root, 'root')
export const Thumb = withContext(Base.Thumb, 'thumb')
/** Wraps Root and its text. Its size and variant reach Root; it dims with a disabled Root. */
export const Label = withProvider('label', 'label')
