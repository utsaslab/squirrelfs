use crate::balloc::*;
use crate::defs::*;
use crate::volatile::*;
use core::ffi;
use kernel::prelude::*;
use kernel::{bindings, fs, symlink};

pub(crate) struct SymlinkOps;
#[vtable]
impl symlink::Operations for SymlinkOps {
    fn get_link(
        dentry: &fs::DEntry,
        inode: &mut fs::INode,
        _callback: *mut bindings::delayed_call,
    ) -> Result<*const ffi::c_char> {
        // unimplemented!()
        let sb = inode.i_sb();
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        hayleyfs_get_link(sbi, dentry, inode)
    }
}

// TODO: ideally this would return a CString or nicer owned type
fn hayleyfs_get_link<'a>(
    sbi: &'a SbInfo,
    _dentry: &fs::DEntry,
    inode: &mut fs::INode,
) -> Result<*const ffi::c_char> {
    // TODO: update timestamps

    let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
    let pi_info = pi.get_inode_info()?;
    let size: u64 = inode.i_size_read().try_into()?;
    // look up the page containing the symlink path
    let page = pi_info.find(0);
    if let Some(page) = page {
        let page_wrapper = DataPageWrapper::from_page_no(sbi, page)?;
        let link = page_wrapper.read_from_page_raw(sbi, 0, size);
        match link {
            Ok(link) => Ok(link.as_ptr() as *const ffi::c_char),
            Err(e) => Err(e),
        }
    } else {
        Err(ENOENT)
    }
}
