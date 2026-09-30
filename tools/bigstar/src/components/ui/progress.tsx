'use client'
import { Progress as Base } from '@base-ui/react/progress'
import { progress } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'

const { withProvider, withContext } = createStyleContext(progress)

export const Root = withProvider(Base.Root, 'root')
export const Label = withContext(Base.Label, 'label')
export const Value = withContext(Base.Value, 'value')
export const Track = withContext(Base.Track, 'track')
export const Indicator = withContext(Base.Indicator, 'indicator')
export type Status = Base.Status
