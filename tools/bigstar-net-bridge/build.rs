fn main() {
    if std::env::var_os("CARGO_FEATURE_WEBRTC").is_some() && cfg!(windows) {
        for lib in ["advapi32", "bcrypt", "crypt32", "gdi32", "user32", "ws2_32"] {
            println!("cargo:rustc-link-lib={lib}");
        }
    }
}
