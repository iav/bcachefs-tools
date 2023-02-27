use anyhow::anyhow;
use crate::bcachefs;
use crate::bcachefs::*;
use crate::errcode::bch_errcode;

pub fn read_super_opts(
    path: &std::path::Path,
    mut opts: bch_opts,
) -> anyhow::Result<bch_sb_handle> {
    use std::os::unix::ffi::OsStrExt;
    let path = std::ffi::CString::new(path.as_os_str().as_bytes()).unwrap();

    let mut sb = std::mem::MaybeUninit::zeroed();

    let ret =
        unsafe { crate::bcachefs::bch2_read_super(path.as_ptr(), &mut opts, sb.as_mut_ptr()) };

    if ret != 0 {
        let err: bch_errcode = unsafe { ::std::mem::transmute(ret) };
        Err(anyhow!(err))
    } else {
        Ok(unsafe { sb.assume_init() })
    }
}

pub fn read_super(path: &std::path::Path) -> anyhow::Result<bch_sb_handle> {
    let opts = bcachefs::bch_opts::default();
    read_super_opts(path, opts)
}
