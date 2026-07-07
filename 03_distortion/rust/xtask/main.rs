//! Build and packaging tasks for the multiband distortion plugin.
//! Invoked via `cargo xtask <task> [--release]`.
//!
//! Tasks:
//!   bundle    Build and package the plugin as a .clap file (default)
//!   install   Bundle, then copy to the system CLAP plugin directory

use std::path::{Path, PathBuf};
use std::process::Command;

// Plugin metadata — keep in sync with the root Cargo.toml
const PLUGIN_NAME: &str = "multiband-distortion-plugin";
const PLUGIN_LIB:  &str = "multiband_distortion_plugin"; // hyphens → underscores
const BUNDLE_ID:   &str = "org.ccrma.multiband-distortion-plugin";
const VERSION:     &str = env!("CARGO_PKG_VERSION"); // inherited from [workspace.package]

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let task    = args.first().map(String::as_str).unwrap_or("bundle");
    let release = args.iter().any(|a| a == "--release");

    match task {
        "bundle"  => { bundle(release); }
        "install" => install(release),
        other => {
            eprintln!("unknown task '{other}'");
            eprintln!("available tasks: bundle [--release], install [--release]");
            std::process::exit(1);
        }
    }
}

fn bundle(release: bool) -> PathBuf {
    let root    = workspace_root();
    let profile = if release { "release" } else { "debug" };
    let target  = root.join("target").join(profile);

    // Build the plugin crate
    let cargo = std::env::var("CARGO").unwrap_or_else(|_| "cargo".into());
    let ok = Command::new(cargo)
        .args(["build", "-p", PLUGIN_NAME])
        .args(if release { &["--release"][..] } else { &[] })
        .current_dir(&root)
        .status().expect("failed to run cargo")
        .success();
    assert!(ok, "cargo build failed");

    // Package for the current platform
    let bundle = match std::env::consts::OS {
        "macos"   => bundle_macos(&target),
        "linux"   => bundle_linux(&target),
        "windows" => bundle_windows(&target),
        os => panic!("unsupported OS: {os}"),
    };

    println!("bundled: {}", bundle.display());
    bundle
}

fn install(release: bool) {
    let bundle = bundle(release);

    let dest = match std::env::consts::OS {
        "macos" => {
            let home = std::env::var("HOME").expect("HOME not set");
            PathBuf::from(home).join("Library/Audio/Plug-Ins/CLAP")
        }
        "linux" => {
            let home = std::env::var("HOME").expect("HOME not set");
            PathBuf::from(home).join(".clap")
        }
        "windows" => {
            let common = std::env::var("COMMONPROGRAMFILES")
                .unwrap_or_else(|_| r"C:\Program Files\Common Files".into());
            PathBuf::from(common).join("CLAP")
        }
        os => panic!("unsupported OS: {os}"),
    };

    let dest = dest.join(bundle.file_name().unwrap());
    copy_bundle(&bundle, &dest);
    println!("installed: {}", dest.display());
}

// macOS: .clap is a bundle directory with a Contents/MacOS/ layout
fn bundle_macos(target: &Path) -> PathBuf {
    let bundle   = target.join(format!("{PLUGIN_NAME}.clap"));
    let contents = bundle.join("Contents");
    let macos    = contents.join("MacOS");

    std::fs::create_dir_all(&macos).unwrap();
    std::fs::copy(
        target.join(format!("lib{PLUGIN_LIB}.dylib")),
        macos.join(PLUGIN_NAME),
    ).unwrap();
    std::fs::write(contents.join("Info.plist"), plist()).unwrap();

    bundle
}

// Linux: .clap is just the .so renamed
fn bundle_linux(target: &Path) -> PathBuf {
    let bundle = target.join(format!("{PLUGIN_NAME}.clap"));
    std::fs::copy(target.join(format!("lib{PLUGIN_LIB}.so")), &bundle).unwrap();
    bundle
}

// Windows: .clap is just the .dll renamed
fn bundle_windows(target: &Path) -> PathBuf {
    let bundle = target.join(format!("{PLUGIN_NAME}.clap"));
    std::fs::copy(target.join(format!("{PLUGIN_LIB}.dll")), &bundle).unwrap();
    bundle
}

fn plist() -> String {
    format!(r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>{PLUGIN_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>{BUNDLE_ID}</string>
    <key>CFBundleName</key>
    <string>{PLUGIN_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>{VERSION}</string>
</dict>
</plist>
"#)
}

// Recursively copy src into dest (handles both file and directory bundles)
fn copy_bundle(src: &Path, dest: &Path) {
    if src.is_dir() {
        std::fs::create_dir_all(dest).unwrap();
        for entry in std::fs::read_dir(src).unwrap() {
            let entry = entry.unwrap();
            copy_bundle(&entry.path(), &dest.join(entry.file_name()));
        }
    } else {
        if let Some(parent) = dest.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        std::fs::copy(src, dest).unwrap();
    }
}

// CARGO_MANIFEST_DIR for the xtask crate is the xtask/ subdirectory;
// the workspace root (and plugin crate) is one level up.
fn workspace_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent().unwrap()
        .to_path_buf()
}
