use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=arch/x86_64/linker.ld");
    let path = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").expect("manifest directory"))
        .join("arch/x86_64/linker.ld");
    println!(
        "cargo:rustc-link-arg-bin=finn-kernel-x86_64=-T{}",
        path.display()
    );
    println!("cargo:rustc-link-arg-bin=finn-kernel-x86_64=-no-pie");
}
