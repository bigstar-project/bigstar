'use client'
import { alert } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'
const { withProvider, withContext } = createStyleContext(alert)
export const Root = withProvider('div', 'root')
export const Icon = withContext('span', 'icon')
export const Content = withContext('div', 'content')
export const Title = withContext('h3', 'title')
export const Description = withContext('p', 'description')
