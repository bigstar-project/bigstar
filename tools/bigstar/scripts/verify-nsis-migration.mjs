import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const [edition, installerPath] = process.argv.slice(2);
if (!['insiders', 'public'].includes(edition) || !installerPath) {
  console.error(
    'Usage: node scripts/verify-nsis-migration.mjs <insiders|public> <installer.nsi>',
  );
  process.exit(2);
}

const installer = readFileSync(installerPath, 'utf8');
const legacyProductName = 'NSMB Mario vs Luigi Online';
const migrationHooks = readFileSync(
  new URL(
    '../src-tauri/windows/legacy-install-migration.nsh',
    import.meta.url,
  ),
  'utf8',
);

if (edition === 'insiders') {
  assert.match(installer, /!define PRODUCTNAME "Bigstar Insiders"/);
  assert.match(
    installer,
    /!define BUNDLEID "io\.github\.bigstar-project\.bigstar\.insiders"/,
  );
  assert.match(
    installer,
    /!include "[^"\r\n]*legacy-install-migration\.nsh"/,
  );
  assert.match(migrationHooks, /!define BIGSTAR_LEGACY_PRODUCT_NAME/);
  assert.match(migrationHooks, /!macro NSIS_HOOK_PREINSTALL/);
  assert.match(migrationHooks, /!macro NSIS_HOOK_POSTINSTALL/);
  assert.match(migrationHooks, /\/UPDATE \/P/);
} else {
  assert.match(installer, /!define PRODUCTNAME "Bigstar"/);
  assert.match(
    installer,
    /!define BUNDLEID "io\.github\.bigstar-project\.bigstar"/,
  );
  assert.doesNotMatch(installer, new RegExp(legacyProductName));
  assert.doesNotMatch(installer, /legacy-install-migration/);
}

console.log(`${edition} NSIS migration policy verified: ${installerPath}`);
