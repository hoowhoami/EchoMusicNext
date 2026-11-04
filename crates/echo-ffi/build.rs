fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    // Keep the ELF identity local to this cdylib. Cargo's target rustflags can
    // be replaced by user RUSTFLAGS and also affect unrelated build artifacts.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("linux") {
        println!("cargo:rustc-link-arg-cdylib=-Wl,-soname,libecho_ffi.so");
    }
}
