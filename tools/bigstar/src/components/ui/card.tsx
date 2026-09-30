'use client'
import { card } from '../../../styled-system/recipes'
import { createStyleContext } from './style-context'
const { withProvider, withContext } = createStyleContext(card)
export const Root = withProvider('div', 'root')
export const Header = withContext('div', 'header')
export const Title = withContext('h3', 'title')
export const Description = withContext('p', 'description')
export const Body = withContext('div', 'body')
export const Footer = withContext('div', 'footer')
