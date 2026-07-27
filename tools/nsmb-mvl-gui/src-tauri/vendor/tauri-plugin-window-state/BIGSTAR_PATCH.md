# Bigstar patch

This directory vendors `tauri-plugin-window-state` 2.4.1.

Bigstar adds `Builder::with_state_directory` so the plugin can keep the
upstream window-state tracking and restoration behavior while storing
`.window-state.json` alongside the rest of the edition-specific application
data. Relative paths are resolved from Tauri's platform `data_dir`; omitting
the option preserves the upstream `app_config_dir` behavior.

Keep all other source changes aligned with the upstream 2.4.1 release when
updating this patch.
