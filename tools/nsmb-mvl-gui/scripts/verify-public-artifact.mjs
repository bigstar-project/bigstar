import { readFileSync } from 'node:fs';
import process from 'node:process';

const artifactPath = process.argv[2];
if (!artifactPath) {
  console.error('Public版の検査対象バイナリを指定してください');
  process.exit(1);
}

const artifact = readFileSync(artifactPath);
const forbiddenStrings = [
  'Insiders report endpoint',
  'Insiders unresolved session report',
  'discord.com/api/webhooks',
];
const found = forbiddenStrings.filter((value) =>
  artifact.includes(Buffer.from(value, 'utf8')),
);

if (found.length > 0) {
  console.error(`Public版にInsiders限定文字列が含まれています: ${found.join(', ')}`);
  process.exit(1);
}

console.log('Public版バイナリのInsiders限定機能スキャンに合格しました');
