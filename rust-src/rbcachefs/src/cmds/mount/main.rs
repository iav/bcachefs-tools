#[no_mangle]
pub extern "C" fn RS_mount_main() -> i32 {
	crate::call_err_fn_for_c(|| main_inner())
}

#[tracing::instrument("main")]
pub fn main_inner() -> anyhow::Result<()> {
	use crate::cmds::mount::{filesystem, key, Options};
	use structopt::StructOpt;
	unsafe {
		libc::setvbuf(filesystem::stdout, std::ptr::null_mut(), libc::_IONBF, 0);
		// libc::fflush(filesystem::stdout);
	}

	let opt = Options::from_args();

	tracing::trace!(?opt);

	let fss = filesystem::probe_filesystems()?;

	let selected_uuid = match &opt.uuid {
		Some(uuid) => uuid,
		None => {
			tracing::info!("Probed Filesystems, Exiting");
			return Ok(());
		}
	};
	let fs = fss
		.get(selected_uuid)
		.ok_or_else(|| anyhow::anyhow!("filesystem was not found"))?;

	tracing::info!(msg="found filesystem", %fs);
	if fs.encrypted() {
		let key = opt
			.key_location
			.0
			.ok_or_else(|| anyhow::anyhow!("no keyoption specified for locked filesystem"))?;

		key::prepare_key(&fs, key)?;
	}

	let mountpoint = opt
		.mountpoint
		.ok_or_else(|| anyhow::anyhow!("mountpoint option was not specified"))?;

	fs.mount(&mountpoint, &opt.options)?;

	Ok(())
}

#[cfg(test)]
mod test {
	// use insta::assert_debug_snapshot;
	// #[test]
	// fn snapshot_testing() {
	// 	insta::assert_debug_snapshot!();
	// }
}
