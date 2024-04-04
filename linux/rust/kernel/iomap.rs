// SPDX-License-Identifier: GPL-2.0

//! Iomap operations
//!
//! C headers: [`include/linux/iomap.h`](../../../../include/linux/iomap.h)
//!

use crate::{
    bindings,
    error::{from_kernel_result, Result},
    fs::INode,
};
use core::marker;
use macros::vtable;

/// vtable for iomap operations
pub struct OperationsVtable<T: Operations>(marker::PhantomData<T>);

impl<T: Operations> OperationsVtable<T> {
    unsafe extern "C" fn iomap_begin_callback(
        inode: *mut bindings::inode,
        pos: i64,
        length: i64,
        flags: u32,
        iomap: *mut bindings::iomap,
        srcmap: *mut bindings::iomap,
    ) -> i32 {
        from_kernel_result! {
            let inode = unsafe { &mut *inode.cast() };
            T::iomap_begin(inode, pos, length, flags, iomap, srcmap)
        }
    }

    unsafe extern "C" fn iomap_end_callback(
        inode: *mut bindings::inode,
        pos: i64,
        length: i64,
        written: isize,
        flags: u32,
        iomap: *mut bindings::iomap,
    ) -> i32 {
        from_kernel_result! {
            let inode = unsafe { &mut *inode.cast() };
            T::iomap_end(inode, pos, length, written, flags, iomap)
        }
    }

    const VTABLE: bindings::iomap_ops = bindings::iomap_ops {
        iomap_begin: Some(Self::iomap_begin_callback),
        iomap_end: Some(Self::iomap_end_callback),
    };

    /// Build an instance of [`struct iomap_ops`]
    /// TODO: safety
    pub const unsafe fn build() -> &'static bindings::iomap_ops {
        &Self::VTABLE
    }
}

/// Corresponds to the kernel's `struct iomap_ops`.
#[vtable]
pub trait Operations {
    /// Corresponds to `iomap_begin` function pointer in `struct iomap_ops`
    fn iomap_begin(
        inode: &INode,
        pos: i64,
        length: i64,
        flags: u32,
        iomap: *mut bindings::iomap,
        srcmap: *mut bindings::iomap,
    ) -> Result<i32>;
    /// Corresponds to `iomap_end` function pointer in `struct iomap_ops`
    fn iomap_end(
        inode: &INode,
        pos: i64,
        length: i64,
        written: isize,
        flags: u32,
        iomap: *mut bindings::iomap,
    ) -> Result<i32>;
}
