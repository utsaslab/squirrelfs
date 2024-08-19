// SPDX-License-Identifier: GPL-2.0

//! Symlink operations
//!
//! C headers: [`include/linux/fs.h`](../../../../include/linux/fs.h) and
//! [`include/linux/file.h`](../../../../include/linux/file.h)

use crate::{
    bindings,
    fs::{DEntry, INode},
    Result,
};
use core::{ffi, marker};
use macros::vtable;

/// Vtable for symlink operations
pub struct OperationsVtable<T: Operations>(marker::PhantomData<T>);

impl<T: Operations> OperationsVtable<T> {
    unsafe extern "C" fn get_link_callback(
        dentry: *mut bindings::dentry,
        inode: *mut bindings::inode,
        callback: *mut bindings::delayed_call,
    ) -> *const ffi::c_char {
        // TODO: safety notes
        let inode = unsafe { &mut *inode.cast() };
        let dentry = unsafe { &mut *dentry.cast() };
        let result = T::get_link(dentry, inode, callback);
        match result {
            Err(e) => unsafe {
                bindings::ERR_PTR(e.to_kernel_errno().into()) as *const ffi::c_char
            },
            Ok(link) => link,
        }
    }

    const VTABLE: bindings::inode_operations = bindings::inode_operations {
        lookup: None,
        get_link: Some(Self::get_link_callback),
        permission: None,
        get_inode_acl: None,
        get_acl: None,
        readlink: None,
        create: None,
        link: None,
        unlink: None,
        symlink: None,
        mkdir: None,
        rmdir: None,
        mknod: None,
        rename: None,
        setattr: None,
        getattr: None,
        listxattr: None,
        fiemap: None,
        update_time: None,
        atomic_open: None,
        tmpfile: None,
        set_acl: None,
        fileattr_set: None,
        fileattr_get: None,
    };

    /// Builds an instance of [`struct inode_operations`].
    ///
    /// # Safety
    /// TODO: safety
    pub const unsafe fn build() -> &'static bindings::inode_operations {
        &Self::VTABLE
    }
}

/// Corresponds to the kernel's `struct inode_operations`.
///
/// You implement this trait whenever you would create a a`struct inode_operations`
/// for a symlink.
#[vtable]
pub trait Operations {
    /// Corresponds to the `get_link` function pointer in `struct inode_operations`.
    fn get_link(
        dentry: &DEntry,
        inode: &mut INode,
        callback: *mut bindings::delayed_call,
    ) -> Result<*const ffi::c_char>;
}
