import { describe, expect, test } from 'vitest';
import { readyLabel, shortPath } from './path';

describe('ランチャーのパス補助関数', () => {
  test('ウィンドウズ形式と POSIX 形式のパスを短縮表示する', () => {
    expect(shortPath('C:\\roms\\nsmb.nds')).toBe('nsmb.nds');
    expect(shortPath('/tmp/roms/client.nds')).toBe('client.nds');
    expect(shortPath('')).toBe('未設定');
  });

  test('設定済みと未設定の表示ラベルを返す', () => {
    expect(readyLabel('C:\\roms\\base.nds')).toBe('設定済み');
    expect(readyLabel('')).toBe('未設定');
  });
});
