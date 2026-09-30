import { Field } from '@ark-ui/react/field';
import type { ComponentProps } from 'react';
import { styled } from 'styled-system/jsx';
import { parkInput as input } from 'styled-system/recipes';

export type InputProps = ComponentProps<typeof Input>;
export const Input = styled(Field.Input, input);
