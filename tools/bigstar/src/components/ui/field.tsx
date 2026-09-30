'use client'
import { Field as Base } from '@base-ui/react/field'
import { field } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'

const { withProvider, withContext } = createStyleContext(field)

export const Root = withProvider(Base.Root, 'root')
export const Item = withContext(Base.Item, 'item')
export const Label = withContext(Base.Label, 'label')
export const Description = withContext(Base.Description, 'description')
export const Error = withContext(Base.Error, 'error')
export const Control = Base.Control
export const Validity = Base.Validity
export type ValidityData = Base.ValidityData
