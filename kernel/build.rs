use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=arch/x86_64/linker.ld");
    println!("cargo:rerun-if-changed=arch/aarch64/linker.ld");
    let root = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").expect("manifest directory"));
    let path = root.join("arch/x86_64/linker.ld");
    println!(
        "cargo:rustc-link-arg-bin=finn-kernel-x86_64=-T{}",
        path.display()
    );
    println!("cargo:rustc-link-arg-bin=finn-kernel-x86_64=-no-pie");
    let path = root.join("arch/aarch64/linker.ld");
    println!(
        "cargo:rustc-link-arg-bin=finn-kernel-aarch64=-T{}",
        path.display()
    );
    println!("cargo:rustc-link-arg-bin=finn-kernel-aarch64=-no-pie");
}
