use std::{env, path::Path};

fn main() {
	cbindgen();
}
/// Generate bindings that C can use to call into the rust binary
fn cbindgen() {
	let crate_dir = env::var("CARGO_MANIFEST_DIR").expect("envvar `CARGO_MANIFEST_DIR` not specified");

	cbindgen::generate(crate_dir)
		.expect("Unable to generate bindings")
		.write_to_file("include/rbcachefs_bindings.h");
}
