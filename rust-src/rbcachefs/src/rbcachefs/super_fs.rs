use crate::c::bcachefs;

impl bcachefs::bch_fs {
	pub fn new(devices: &[Vec<u8>], opts: bcachefs::bch_opts) -> *mut Self {
		let cstrs: Vec<_> = devices
			.into_iter()
			.map(|i| std::ffi::CString::new(i.clone()))
			.filter_map(Result::ok)
			.map(std::ffi::CString::into_raw)
			.collect();

		let fs = unsafe { bcachefs::bch2_fs_open(cstrs.as_ptr(), cstrs.len() as u32, opts) };
		unsafe {
			cstrs.into_iter().map(|i| std::ffi::CString::from_raw(i)).last();
		}
		fs
	}

	pub fn try_new(devices: &[Vec<u8>], opts: bcachefs::bch_opts) -> anyhow::Result<&mut Self> {
		crate::rbcachefs::err::ptr_result(Self::new(devices, opts))
	}
}

// impl Drop for bcachefs::bch_fs {
// 	fn drop(&mut self) {
// 		unsafe {
// 			bcachefs::bch2_fs_free(self);
// 		}
// 	}
// }
