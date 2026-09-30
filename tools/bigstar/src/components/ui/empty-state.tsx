'use client'
import { emptyState } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'
const { withProvider, withContext } = createStyleContext(emptyState)
export const Root = withProvider('div', 'root')
export const Icon = withContext('span', 'icon')
export const Title = withContext('h3', 'title')
export const Description = withContext('p', 'description')
export const Actions = withContext('div', 'actions')
