//! Inodes
//!
//! C headers: [`include/linux/fs.h`](../../../../include/linux/fs.h) and
//! [`include/linux/file.h`](../../../../include/linux/file.h)

use crate::{
    error::{code::*, from_kernel_result, Result},
    file::{File, IoctlCommand},
    types::ForeignOwnable,
};
use core::{marker, ptr};
use macros::vtable;

/// Vtable for dir operations
/// TODO: should this be pub(crate) and only accessible via some other
/// function/module like file::OperationsVtable?
pub struct OperationsVtable<T: Operations>(marker::PhantomData<T>);

#[allow(dead_code)]
impl<T: Operations> OperationsVtable<T> {
    unsafe extern "C" fn read_callback(
        file: *mut bindings::file,
        buf: *mut core::ffi::c_char,
        siz: usize,
        ppos: *mut core::ffi::c_long,
    ) -> isize {
        unsafe { bindings::generic_read_dir(file, buf, siz, ppos) }
    }

    unsafe extern "C" fn llseek_callback(
        file: *mut bindings::file,
        offset: core::ffi::c_long,
        whence: core::ffi::c_int,
    ) -> core::ffi::c_long {
        unsafe { bindings::generic_file_llseek(file, offset, whence) }
    }

    unsafe extern "C" fn fsync_callback(
        file: *mut bindings::file,
        start: bindings::loff_t,
        end: bindings::loff_t,
        datasync: core::ffi::c_int,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            let start = start.try_into()?;
            let end = end.try_into()?;
            let datasync = datasync != 0;
            // SAFETY: `private_data` was initialised by `open_callback` with a value returned by
            // `T::Data::into_foreign`. `T::Data::from_foreign` is only called by the
            // `release` callback, which the C API guarantees that will be called only when all
            // references to `file` have been released, so we know it can't be called while this
            // function is running.
            let f = unsafe { T::Data::borrow((*file).private_data) };
            let res = T::fsync(f, unsafe { File::from_ptr(file) }, start, end, datasync)?;
            Ok(res.try_into().unwrap())
        }
    }

    unsafe extern "C" fn unlocked_ioctl_callback(
        file: *mut bindings::file,
        cmd: core::ffi::c_uint,
        arg: core::ffi::c_ulong,
    ) -> core::ffi::c_long {
        from_kernel_result! {
            // SAFETY: `private_data` was initialised by `open_callback` with a value returned by
            // `T::Data::into_foreign`. `T::Data::from_foreign` is only called by the
            // `release` callback, which the C API guarantees that will be called only when all
            // references to `file` have been released, so we know it can't be called while this
            // function is running.
            let f = unsafe { T::Data::borrow((*file).private_data) };
            let mut cmd = IoctlCommand::new(cmd as _, arg as _);
            let ret = T::ioctl(f, unsafe { File::from_ptr(file) }, &mut cmd)?;
            Ok(ret as _)
        }
    }

    unsafe extern "C" fn iterate_callback(
        file: *mut bindings::file,
        ctx: *mut bindings::dir_context,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            // TODO: don't pass the raw dir_context pointer
            let res = T::iterate(unsafe { File::from_ptr(file) }, ctx)?;
            Ok(res.try_into().unwrap())
        }
    }

    const VTABLE: bindings::file_operations = bindings::file_operations {
        open: None,
        release: None,
        read: Some(Self::read_callback),
        write: None,
        llseek: Some(Self::llseek_callback),
        check_flags: None,
        compat_ioctl: None,
        copy_file_range: None,
        fallocate: None,
        fadvise: None,
        fasync: None,
        flock: None,
        flush: None,
        fsync: Some(Self::fsync_callback),
        get_unmapped_area: None,
        iterate: Some(Self::iterate_callback),
        iterate_shared: Some(Self::iterate_callback),
        iopoll: None,
        lock: None,
        mmap: None,
        mmap_supported_flags: 0,
        owner: ptr::null_mut(),
        poll: None,
        read_iter: None,
        remap_file_range: None,
        sendpage: None,
        setlease: None,
        show_fdinfo: None,
        splice_read: None,
        splice_write: None,
        unlocked_ioctl: if T::HAS_IOCTL {
            Some(Self::unlocked_ioctl_callback)
        } else {
            None
        },
        uring_cmd: None,
        uring_cmd_iopoll: None,
        write_iter: None,
    };

    /// Builds an instance of [`struct file_operations`].
    ///
    /// # Safety
    /// TODO: safety
    pub const unsafe fn build() -> &'static bindings::file_operations {
        &Self::VTABLE
    }
}

/// Corresponds to the kernel's `struct file_operations`. Specifically applied to
/// directory inodes.
#[vtable]
pub trait Operations {
    /// The type of the context data returned by [`Operations::open`] and made available to
    /// other methods.
    type Data: ForeignOwnable + Send + Sync = ();

    /// The type of the context data passed to [`Operations::open`].
    type OpenData: Sync = ();

    /// Read a directory file.
    fn read(
        _file: *mut bindings::file,
        _buf: *mut core::ffi::c_char,
        _siz: core::ffi::c_uint,
        _ppos: *mut core::ffi::c_long,
    ) -> Result<isize> {
        Err(EINVAL)
    }

    /// Syncs pending changes to this file.
    ///
    /// Corresponds to the `fsync` function pointer in `struct file_operations`.
    fn fsync(
        _data: <Self::Data as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _start: u64,
        _end: u64,
        _datasync: bool,
    ) -> Result<u32> {
        Err(EINVAL)
    }

    /// Performs IO control operations that are specific to the file.
    ///
    /// Corresponds to the `unlocked_ioctl` function pointer in `struct file_operations`.
    fn ioctl(
        _data: <Self::Data as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: &mut IoctlCommand,
    ) -> Result<i32> {
        Err(ENOTTY)
    }

    /// Iterate over directory entries
    ///
    /// Corresponds to the `iterate` function pointer in `struct file_operations`.
    fn iterate(_file: &File, _ctx: *mut bindings::dir_context) -> Result<u32> {
        Err(EINVAL)
    }
}
