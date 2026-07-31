fn main() {
    println!("cargo:rerun-if-env-changed=BIGSTAR_APP_VERSION");
    println!("cargo:rerun-if-env-changed=BIGSTAR_APP_DATA_DIR_NAME");
    println!("cargo:rerun-if-env-changed=BIGSTAR_DEFAULT_SIGNAL_URL");
    println!("cargo:rerun-if-env-changed=BIGSTAR_INSIDERS_REPORT_URL");
    println!("cargo:rerun-if-env-changed=BIGSTAR_EDITION");
    println!("cargo:rerun-if-env-changed=BIGSTAR_BUILD_PROFILE");
    println!("cargo:rerun-if-changed=migrations");
    tauri_build::build();
}
