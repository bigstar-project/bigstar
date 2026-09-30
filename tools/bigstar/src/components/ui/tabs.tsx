'use client'
import { Tabs as Base } from '@base-ui/react/tabs'
import { tabs } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'

const { withProvider, withContext } = createStyleContext(tabs)

export const Root = withProvider(Base.Root, 'root')
export const List = withContext(Base.List, 'list')
export const Tab = withContext(Base.Tab, 'tab')
export const Panel = withContext(Base.Panel, 'panel')
export const Indicator = withContext(Base.Indicator, 'indicator')
