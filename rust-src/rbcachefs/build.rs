use std::{env, path::{Path, PathBuf}};

fn main() {
	let crate_dir: PathBuf = env::var("CARGO_MANIFEST_DIR").expect("envvar `CARGO_MANIFEST_DIR` not specified").into();
	cbindgen(&crate_dir);
	bch_bindgen(&crate_dir);
}

/// Generate bindings that C can use to call into the rust binary
fn cbindgen(crate_dir: &Path) {

	cbindgen::generate(crate_dir)
		.expect("Unable to generate bindings")
		.write_to_file("include/rbcachefs_bindings.h");
}

/// Generate bindings from C that rust can use to call into the C binary
fn bch_bindgen(crate_dir: &Path) {
	// use std::process::Command;

	let out_dir: PathBuf = std::env::var_os("OUT_DIR").expect("ENV Var 'OUT_DIR' Expected").into();

	let libbcachefs_inc_dir =
		std::env::var("LIBBCACHEFS_INCLUDE").unwrap_or_else(|_| crate_dir.join("libbcachefs").display().to_string());
	let libbcachefs_inc_dir = std::path::Path::new(&libbcachefs_inc_dir);

	let _libbcachefs_dir = crate_dir.join("libbcachefs").join("libbcachefs");
	libbcachefs_bindings(&out_dir, &crate_dir, &libbcachefs_inc_dir);
	libkeyutils_bindings(&out_dir, &crate_dir);
}

/// Generate bindings for libbcachefs
fn libbcachefs_bindings(output_dir: &Path, crate_dir: &Path, libbcachefs_src: &Path) {
	let bindings = bindgen::builder()
		.header(
			crate_dir
				.join("src/c/bcachefs/libbcachefs_wrapper.h")
				.display()
				.to_string(),
		)
		// Provided by CFLAGS from Makefile
		// Because we are dependent on the "Makefile", we do not add clang args here that are present in the CFLAGS variable which we expect to have passed to us in some form 

		.clang_arg("-v")
		.derive_debug(true)
		.derive_default(true)
		.derive_eq(true)
		.layout_tests(true)
		.default_enum_style(bindgen::EnumVariation::Rust { non_exhaustive: true })
		.allowlist_function(".*bch2_.+")
		// .allowlist_function("bio_.*")
		.allowlist_function("derive_passphrase")
		.allowlist_function("request_key")
		.allowlist_function("add_key")
		.allowlist_function("keyctl_search")
		// .blocklist_type("bch_extent_ptr")
		// .blocklist_type("btree_node")
		// .blocklist_type("bch_extent_crc32")
		// .blocklist_type("rhash_lock_head")
		// .blocklist_type("srcu_struct")
		// .blocklist_item("")
		.allowlist_var(".*")
		.allowlist_var("BCACHE_MAGIC")
		// .allowlist_var("BCH_.*")
		// .allowlist_var("KEY_SPEC_.*")
		.allowlist_type("*")
		// .allowlist_type("bch_kdf_types")
		// .allowlist_type("bch_sb_field_.*")
		// .allowlist_type("bch_encrypted_key")
		// .allowlist_type("nonce")
		.newtype_enum("bch_kdf_types")
		.opaque_type("gendisk")
		.opaque_type("bkey")
		.opaque_type("btree_node")
		.opaque_type("open_bucket.*")
		.opaque_type("bch_extent_ptr")
		.opaque_type("bch_extent_crc32")
		.opaque_type("rhash_lock_head")
		.opaque_type("srcu_struct")
		.opaque_type("bch_replicas_usage")
		.generate()
		.expect("BindGen Generation Failiure: [libbcachefs_wrapper]");
	bindings
		.write_to_file(output_dir.join("bcachefs.rs"))
		.expect("Writing to output file failed for: `bcachefs.rs`");
}

/// Generate bindings to libkeyutils
fn libkeyutils_bindings(output_dir: &Path, manifest_dir: &Path) {
	let keyutils = pkg_config::probe_library("libkeyutils").expect("Failed to find keyutils lib");
	let bindings = bindgen::builder()
		.header(
			manifest_dir
				.join("src/c///keyutils/keyutils_wrapper.h")
				.display()
				.to_string(),
		)
		.clang_args(keyutils.include_paths.iter().map(|p| format!("-I{}", p.display())))
		.generate()
		.expect("BindGen Generation Failiure: [Keyutils]");
	bindings
		.write_to_file(output_dir.join("keyutils.rs"))
		.expect("Writing to output file failed for: `keyutils.rs`");
}
