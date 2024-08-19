// SPDX-License-Identifier: GPL-2.0

//! File systems.
//!
//! C headers: [`include/linux/fs.h`](../../../../include/linux/fs.h)

use crate::{
    bindings, dir, error::code::*, error::from_kernel_result, inode, str::CStr, to_result,
    types::ForeignOwnable, AlwaysRefCounted, Error, Result, ScopeGuard, ThisModule,
};
use alloc::boxed::Box;
use core::{
    cell::UnsafeCell,
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
    ptr,
};
use macros::vtable;

pub mod param;

/// Type of superblock keying.
///
/// It determines how C's `fs_context_operations::get_tree` is implemented.
pub enum Super {
    /// Only one such superblock may exist.
    Single,

    /// As [`Super::Single`], but reconfigure if it exists.
    SingleReconf,

    /// Superblocks with different data pointers may exist.
    Keyed,

    /// Multiple independent superblocks may exist.
    Independent,

    /// Uses a block device.
    BlockDev,
}

/// A file system context.
///
/// It is used to gather configuration to then mount or reconfigure a file system.
#[vtable]
pub trait Context<T: Type + ?Sized> {
    /// Type of the data associated with the context.
    type Data: ForeignOwnable + Send + Sync + 'static;

    /// The typed file system parameters.
    ///
    /// Users are encouraged to define it using the [`crate::define_fs_params`] macro.
    const PARAMS: param::SpecTable<'static, Self::Data> = param::SpecTable::empty();

    /// Creates a new context.
    fn try_new() -> Result<Self::Data>;

    /// Parses a parameter that wasn't specified in [`Self::PARAMS`].
    fn parse_unknown_param(
        _data: &mut Self::Data,
        _name: &CStr,
        _value: param::Value<'_>,
    ) -> Result {
        Err(ENOPARAM)
    }

    /// Parses the whole parameter block, potentially skipping regular handling for parts of it.
    ///
    /// The return value is the portion of the input buffer for which the regular handling
    /// (involving [`Self::PARAMS`] and [`Self::parse_unknown_param`]) will still be carried out.
    /// If it's `None`, the regular handling is not performed at all.
    fn parse_monolithic<'a>(
        _data: &mut Self::Data,
        _buf: Option<&'a mut [u8]>,
    ) -> Result<Option<&'a mut [u8]>> {
        Ok(None)
    }

    /// Returns the superblock data to be used by this file system context.
    ///
    /// This is only needed when [`Type::SUPER_TYPE`] is [`Super::Keyed`], otherwise it is never
    /// called. In the former case, when the fs is being mounted, an existing superblock is reused
    /// if one can be found with the same data as the returned value; otherwise a new superblock is
    /// created.
    fn tree_key(_data: &mut Self::Data) -> Result<T::Data> {
        Err(ENOTSUPP)
    }
}

struct Tables<T: Type + ?Sized>(T);
impl<T: Type + ?Sized> Tables<T> {
    const CONTEXT: bindings::fs_context_operations = bindings::fs_context_operations {
        free: Some(Self::free_callback),
        parse_param: Some(Self::parse_param_callback),
        get_tree: Some(Self::get_tree_callback),
        reconfigure: Some(Self::reconfigure_callback),
        parse_monolithic: if <T::Context as Context<T>>::HAS_PARSE_MONOLITHIC {
            Some(Self::parse_monolithic_callback)
        } else {
            None
        },
        dup: None,
    };

    unsafe extern "C" fn free_callback(fc: *mut bindings::fs_context) {
        // SAFETY: The callback contract guarantees that `fc` is valid.
        let fc = unsafe { &*fc };

        let ptr = fc.fs_private;
        if !ptr.is_null() {
            // SAFETY: `fs_private` was initialised with the result of a `into_foreign` call in
            // `init_fs_context_callback`, so it's ok to call `from_foreign` here.
            unsafe { <T::Context as Context<T>>::Data::from_foreign(ptr) };
        }

        let ptr = fc.s_fs_info;
        if !ptr.is_null() {
            // SAFETY: `s_fs_info` may be initialised with the result of a `into_foreign` call in
            // `get_tree_callback` when keyed superblocks are used (`get_tree_keyed` sets it), so
            // it's ok to call `from_foreign` here.
            unsafe { T::Data::from_foreign(ptr) };
        }
    }

    unsafe extern "C" fn parse_param_callback(
        fc: *mut bindings::fs_context,
        param: *mut bindings::fs_parameter,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            // SAFETY: The callback contract guarantees that `fc` is valid.
            let ptr = unsafe { (*fc).fs_private };

            // SAFETY: The value of `ptr` (coming from `fs_private` was initialised in
            // `init_fs_context_callback` to the result of an `into_foreign` call. Since the
            // context is valid, `from_foreign` wasn't called yet, so `ptr` is valid. Additionally,
            // the callback contract guarantees that callbacks are serialised, so it is ok to
            // mutably reference it.
            let mut data =
                unsafe { <<T::Context as Context<T>>::Data as ForeignOwnable>::borrow_mut(ptr) };
            let mut result = bindings::fs_parse_result::default();
            // SAFETY: All parameters are valid at least for the duration of the call.
            let opt =
                unsafe { bindings::fs_parse(fc, T::Context::PARAMS.first, param, &mut result) };

            // SAFETY: The callback contract guarantees that `param` is valid for the duration of
            // the callback.
            let param = unsafe { &*param };
            if opt >= 0 {
                let opt = opt as usize;
                if opt >= T::Context::PARAMS.handlers.len() {
                    return Err(EINVAL);
                }
                T::Context::PARAMS.handlers[opt].handle_param(&mut data, param, &result)?;
                return Ok(0);
            }

            if opt != ENOPARAM.to_kernel_errno() {
                return Err(Error::from_kernel_errno(opt));
            }

            if !T::Context::HAS_PARSE_UNKNOWN_PARAM {
                return Err(ENOPARAM);
            }

            let val = param::Value::from_fs_parameter(param);
            // SAFETY: The callback contract guarantees the parameter key to be valid and last at
            // least the duration of the callback.
            T::Context::parse_unknown_param(
                &mut data,
                unsafe { CStr::from_char_ptr(param.key) },
                val,
            )?;
            Ok(0)
        }
    }

    unsafe extern "C" fn fill_super_callback(
        sb_ptr: *mut bindings::super_block,
        fc: *mut bindings::fs_context,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            // SAFETY: The callback contract guarantees that `fc` is valid. It also guarantees that
            // the callbacks are serialised for a given `fc`, so it is safe to mutably dereference
            // it.
            let fc = unsafe { &mut *fc };
            let ptr = core::mem::replace(&mut fc.fs_private, ptr::null_mut());

            // SAFETY: The value of `ptr` (coming from `fs_private` was initialised in
            // `init_fs_context_callback` to the result of an `into_foreign` call. The context is
            // being used to initialise a superblock, so we took over `ptr` (`fs_private` is set to
            // null now) and call `from_foreign` below.
            let data =
                unsafe { <<T::Context as Context<T>>::Data as ForeignOwnable>::from_foreign(ptr) };

            // SAFETY: The callback contract guarantees that `sb_ptr` is a unique pointer to a
            // newly-created superblock.
            let newsb = unsafe { NewSuperBlock::new(sb_ptr) };
            T::fill_super(data, newsb)?;
            Ok(0)
        }
    }

    unsafe extern "C" fn get_tree_callback(fc: *mut bindings::fs_context) -> core::ffi::c_int {
        // N.B. When new types are added below, we may need to update `kill_sb_callback` to ensure
        // that we're cleaning up properly.
        match T::SUPER_TYPE {
            Super::Single => unsafe {
                // SAFETY: `fc` is valid per the callback contract. `fill_super_callback` also has
                // the right type and is a valid callback.
                bindings::get_tree_single(fc, Some(Self::fill_super_callback))
            },
            Super::SingleReconf => unsafe {
                // SAFETY: `fc` is valid per the callback contract. `fill_super_callback` also has
                // the right type and is a valid callback.
                bindings::get_tree_single_reconf(fc, Some(Self::fill_super_callback))
            },
            Super::Independent => unsafe {
                // SAFETY: `fc` is valid per the callback contract. `fill_super_callback` also has
                // the right type and is a valid callback.
                bindings::get_tree_nodev(fc, Some(Self::fill_super_callback))
            },
            Super::BlockDev => unsafe {
                // SAFETY: `fc` is valid per the callback contract. `fill_super_callback` also has
                // the right type and is a valid callback.
                bindings::get_tree_bdev(fc, Some(Self::fill_super_callback))
            },
            Super::Keyed => {
                from_kernel_result! {
                    // SAFETY: `fc` is valid per the callback contract.
                    let ctx = unsafe { &*fc };
                    let ptr = ctx.fs_private;

                    // SAFETY: The value of `ptr` (coming from `fs_private` was initialised in
                    // `init_fs_context_callback` to the result of an `into_foreign` call. Since
                    // the context is valid, `from_foreign` wasn't called yet, so `ptr` is valid.
                    // Additionally, the callback contract guarantees that callbacks are
                    // serialised, so it is ok to mutably reference it.
                    let mut data = unsafe {
                        <<T::Context as Context<T>>::Data as ForeignOwnable>::borrow_mut(ptr)
                    };
                    let fs_data = T::Context::tree_key(&mut data)?;
                    let fs_data_ptr = fs_data.into_foreign();

                    // `get_tree_keyed` reassigns `ctx.s_fs_info`, which should be ok because
                    // nowhere else is it assigned a non-null value. However, we add the assert
                    // below to ensure that there are no unexpected paths on the C side that may do
                    // this.
                    assert_eq!(ctx.s_fs_info, core::ptr::null_mut());

                    // SAFETY: `fc` is valid per the callback contract. `fill_super_callback` also
                    // has the right type and is a valid callback. Lastly, we just called
                    // `into_foreign` above, so `fs_data_ptr` is also valid.
                    to_result(unsafe {
                        bindings::get_tree_keyed(
                            fc,
                            Some(Self::fill_super_callback),
                            fs_data_ptr as _,
                        )
                    })?;
                    Ok(0)
                }
            }
        }
    }

    unsafe extern "C" fn reconfigure_callback(_fc: *mut bindings::fs_context) -> core::ffi::c_int {
        EINVAL.to_kernel_errno()
    }

    unsafe extern "C" fn parse_monolithic_callback(
        fc: *mut bindings::fs_context,
        buf: *mut core::ffi::c_void,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            // SAFETY: The callback contract guarantees that `fc` is valid.
            let ptr = unsafe { (*fc).fs_private };

            // SAFETY: The value of `ptr` (coming from `fs_private` was initialised in
            // `init_fs_context_callback` to the result of an `into_foreign` call. Since the
            // context is valid, `from_foreign` wasn't called yet, so `ptr` is valid. Additionally,
            // the callback contract guarantees that callbacks are serialised, so it is ok to
            // mutably reference it.
            let mut data =
                unsafe { <<T::Context as Context<T>>::Data as ForeignOwnable>::borrow_mut(ptr) };
            let page = if buf.is_null() {
                None
            } else {
                // SAFETY: This callback is called to handle the `mount` syscall, which takes a
                // page-sized buffer as data.
                Some(unsafe { &mut *ptr::slice_from_raw_parts_mut(buf.cast(), crate::PAGE_SIZE) })
            };
            let regular = T::Context::parse_monolithic(&mut data, page)?;
            if let Some(buf) = regular {
                // SAFETY: Both `fc` and `buf` are guaranteed to be valid; the former because the
                // callback is still ongoing and the latter because its lifefime is tied to that of
                // `page`, which is also valid for the duration of the callback.
                to_result(unsafe {
                    bindings::generic_parse_monolithic(fc, buf.as_mut_ptr().cast())
                })?;
            }
            Ok(0)
        }
    }

    const SUPER_BLOCK: bindings::super_operations = bindings::super_operations {
        alloc_inode: None,
        destroy_inode: None,
        free_inode: None,
        dirty_inode: Some(Self::dirty_inode_callback),
        write_inode: None,
        drop_inode: None,
        evict_inode: Some(Self::evict_inode_callback),
        put_super: Some(Self::put_super_callback),
        sync_fs: None,
        freeze_super: None,
        freeze_fs: None,
        thaw_super: None,
        unfreeze_fs: None,
        statfs: Some(Self::statfs_callback),
        remount_fs: None,
        umount_begin: None,
        show_options: None,
        show_devname: None,
        show_path: None,
        show_stats: None,
        #[cfg(CONFIG_QUOTA)]
        quota_read: None,
        #[cfg(CONFIG_QUOTA)]
        quota_write: None,
        #[cfg(CONFIG_QUOTA)]
        get_dquots: None,
        nr_cached_objects: None,
        free_cached_objects: None,
    };

    // unsafe extern "C" fn alloc_inode_callback(
    //     sb: *mut bindings::super_block,
    // ) -> *mut bindings::inode {
    //     let sb: &SuperBlock<T> = unsafe { &*sb.cast::<SuperBlock<_>>() };
    //     T::alloc_inode(sb)
    // }

    // unsafe extern "C" fn destroy_inode_callback(inode: *mut bindings::inode) {
    //     let inode: &INode = unsafe { &*inode.cast() };
    //     T::destroy_inode(inode);
    // }

    unsafe extern "C" fn put_super_callback(sb: *mut bindings::super_block) {
        let sb: &SuperBlock<T> = unsafe { &*sb.cast() };
        T::put_super(sb);
    }

    unsafe extern "C" fn statfs_callback(
        dentry: *mut bindings::dentry,
        buf: *mut bindings::kstatfs,
    ) -> core::ffi::c_int {
        let sb: &SuperBlock<T> = unsafe { &*(*dentry).d_sb.cast() };
        from_kernel_result! {T::statfs(sb, buf)?; Ok(0)}
    }

    unsafe extern "C" fn dirty_inode_callback(
        inode: *mut bindings::inode,
        flags: core::ffi::c_int,
    ) {
        let inode = unsafe { &*inode.cast() };
        T::dirty_inode(inode, flags);
    }

    unsafe extern "C" fn evict_inode_callback(inode: *mut bindings::inode) {
        let inode = unsafe { &*inode.cast() };
        T::evict_inode(inode);
    }
}

/// A file system type.
pub trait Type {
    /// The context used to build fs configuration before it is mounted or reconfigured.
    type Context: Context<Self> + ?Sized;

    /// Data associated with each file system instance.
    type Data: ForeignOwnable + Send + Sync = ();

    /// Used to define `struct inode_operations` for this file system.
    type InodeOps: inode::Operations;

    /// Used to define `struct file_operations` **for directories** for this file system.
    type DirOps: dir::Operations;

    /// Determines how superblocks for this file system type are keyed.
    const SUPER_TYPE: Super;

    /// The name of the file system type.
    const NAME: &'static CStr;

    /// The flags of this file system type.
    ///
    /// It is a combination of the flags in the [`flags`] module.
    const FLAGS: i32;

    /// Initialises a super block for this file system type.
    fn fill_super(
        data: <Self::Context as Context<Self>>::Data,
        sb: NewSuperBlock<'_, Self>,
    ) -> Result<&SuperBlock<Self>>;

    /// Clean up the superblock at unmount
    fn put_super(sb: &SuperBlock<Self>);

    /// Returns metadata about this file system.
    fn statfs(sb: &SuperBlock<Self>, buf: *mut bindings::kstatfs) -> Result<()>;

    /// Called by VFS when an inode is marked dirty.
    fn dirty_inode(inode: &INode, flags: i32);

    /// Called at the last iput() if i_nlink is 0.
    fn evict_inode(inode: &INode);

    /// Initializes i_private field for the root inode.
    fn init_private(inode: *mut bindings::inode) -> Result<()>;
}

/// File system flags.
pub mod flags {
    use crate::bindings;

    /// The file system requires a device.
    pub const REQUIRES_DEV: i32 = bindings::FS_REQUIRES_DEV as _;

    /// The options provided when mounting are in binary form.
    pub const BINARY_MOUNTDATA: i32 = bindings::FS_BINARY_MOUNTDATA as _;

    /// The file system has a subtype. It is extracted from the name and passed in as a parameter.
    pub const HAS_SUBTYPE: i32 = bindings::FS_HAS_SUBTYPE as _;

    /// The file system can be mounted by userns root.
    pub const USERNS_MOUNT: i32 = bindings::FS_USERNS_MOUNT as _;

    /// Disables fanotify permission events.
    pub const DISALLOW_NOTIFY_PERM: i32 = bindings::FS_DISALLOW_NOTIFY_PERM as _;

    /// The file system has been updated to handle vfs idmappings.
    pub const ALLOW_IDMAP: i32 = bindings::FS_ALLOW_IDMAP as _;

    /// The file systen will handle `d_move` during `rename` internally.
    pub const RENAME_DOES_D_MOVE: i32 = bindings::FS_RENAME_DOES_D_MOVE as _;
}

/// A file system registration.
#[derive(Default)]
pub struct Registration {
    is_registered: bool,
    fs: UnsafeCell<bindings::file_system_type>,
    _pin: PhantomPinned,
}

// SAFETY: `Registration` doesn't really provide any `&self` methods, so it is safe to pass
// references to it around.
unsafe impl Sync for Registration {}

// SAFETY: Both registration and unregistration are implemented in C and safe to be performed from
// any thread, so `Registration` is `Send`.
unsafe impl Send for Registration {}

impl Registration {
    /// Creates a new file system registration.
    ///
    /// It is not visible or accessible yet. A successful call to [`Registration::register`] needs
    /// to be made before users can mount it.
    pub fn new() -> Self {
        Self {
            is_registered: false,
            fs: UnsafeCell::new(bindings::file_system_type::default()),
            _pin: PhantomPinned,
        }
    }

    /// Registers a file system so that it can be mounted by users.
    ///
    /// The file system is described by the [`Type`] argument.
    ///
    /// It is automatically unregistered when the registration is dropped.
    pub fn register<T: Type + ?Sized>(self: Pin<&mut Self>, module: &'static ThisModule) -> Result {
        // SAFETY: We never move out of `this`.
        let this = unsafe { self.get_unchecked_mut() };

        if this.is_registered {
            return Err(EINVAL);
        }

        let mut fs = this.fs.get_mut();
        fs.owner = module.0;
        fs.name = T::NAME.as_char_ptr();
        fs.fs_flags = T::FLAGS;
        fs.parameters = T::Context::PARAMS.first;
        fs.init_fs_context = Some(Self::init_fs_context_callback::<T>);
        fs.kill_sb = Some(Self::kill_sb_callback::<T>);

        // SAFETY: This block registers all fs type keys with lockdep. We just need the memory
        // locations to be owned by the caller, which is the case.
        unsafe {
            bindings::lockdep_register_key(&mut fs.s_lock_key);
            bindings::lockdep_register_key(&mut fs.s_umount_key);
            bindings::lockdep_register_key(&mut fs.s_vfs_rename_key);
            bindings::lockdep_register_key(&mut fs.i_lock_key);
            bindings::lockdep_register_key(&mut fs.i_mutex_key);
            bindings::lockdep_register_key(&mut fs.invalidate_lock_key);
            bindings::lockdep_register_key(&mut fs.i_mutex_dir_key);
            for key in &mut fs.s_writers_key {
                bindings::lockdep_register_key(key);
            }
        }

        let ptr = this.fs.get();

        // SAFETY: `ptr` as valid as it points to the `self.fs`.
        let key_guard = ScopeGuard::new(|| unsafe { Self::unregister_keys(ptr) });

        // SAFETY: Pointers stored in `fs` are either static so will live for as long as the
        // registration is active (it is undone in `drop`).
        to_result(unsafe { bindings::register_filesystem(ptr) })?;
        key_guard.dismiss();
        this.is_registered = true;
        Ok(())
    }

    /// Unregisters the lockdep keys in the file system type.
    ///
    /// # Safety
    ///
    /// `fs` must be non-null and valid.
    unsafe fn unregister_keys(fs: *mut bindings::file_system_type) {
        // SAFETY: This block unregisters all fs type keys from lockdep. They must have been
        // registered before.
        unsafe {
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).s_lock_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).s_umount_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).s_vfs_rename_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).i_lock_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).i_mutex_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).invalidate_lock_key));
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).i_mutex_dir_key));
            for i in 0..(*fs).s_writers_key.len() {
                bindings::lockdep_unregister_key(ptr::addr_of_mut!((*fs).s_writers_key[i]));
            }
        }
    }

    unsafe extern "C" fn init_fs_context_callback<T: Type + ?Sized>(
        fc_ptr: *mut bindings::fs_context,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            let data = T::Context::try_new()?;
            // SAFETY: The callback contract guarantees that `fc_ptr` is the only pointer to a
            // newly-allocated fs context, so it is safe to mutably reference it.
            let fc = unsafe { &mut *fc_ptr };
            fc.fs_private = data.into_foreign() as _;
            fc.ops = &Tables::<T>::CONTEXT;
            Ok(0)
        }
    }

    unsafe extern "C" fn kill_sb_callback<T: Type + ?Sized>(sb_ptr: *mut bindings::super_block) {
        if let Super::BlockDev = T::SUPER_TYPE {
            // SAFETY: When the superblock type is `BlockDev`, we have a block device so it's safe
            // to call `kill_block_super`. Additionally, the callback contract guarantees that
            // `sb_ptr` is valid.
            unsafe { bindings::kill_block_super(sb_ptr) }
        } else {
            // SAFETY: We always call a `get_tree_nodev` variant from `get_tree_callback` without a
            // device when `T::SUPER_TYPE` is not `BlockDev`, so we never have a device in such
            // cases, therefore it is ok to call the function below. Additionally, the callback
            // contract guarantees that `sb_ptr` is valid.
            unsafe { bindings::kill_anon_super(sb_ptr) }
        }

        // SAFETY: The callback contract guarantees that `sb_ptr` is valid.
        let sb = unsafe { &*sb_ptr };

        // SAFETY: The `kill_sb` callback being called implies that the `s_type` field is valid.
        unsafe { Self::unregister_keys(sb.s_type) };

        let ptr = sb.s_fs_info;
        if !ptr.is_null() {
            // SAFETY: The only place where `s_fs_info` is assigned is `NewSuperBlock::init`, where
            // it's initialised with the result of a `into_foreign` call. We checked above that ptr
            // is non-null because it would be null if we never reached the point where we init the
            // field.
            unsafe { T::Data::from_foreign(ptr) };
        }
    }
}

impl Drop for Registration {
    fn drop(&mut self) {
        if self.is_registered {
            // SAFETY: When `is_registered` is `true`, a previous call to `register_filesystem` has
            // succeeded, so it is safe to unregister here.
            unsafe { bindings::unregister_filesystem(self.fs.get()) };
        }
    }
}

/// State of [`NewSuperBlock`] that indicates that [`NewSuperBlock::init`] needs to be called
/// eventually.
pub struct NeedsInit;

/// State of [`NewSuperBlock`] that indicates that [`NewSuperBlock::init_root`] needs to be called
/// eventually.
pub struct NeedsRoot;

/// Required superblock parameters.
///
/// This is used in [`NewSuperBlock::init`].
pub struct SuperParams {
    /// The magic number of the superblock.
    pub magic: u32,

    /// The size of a block in powers of 2 (i.e., for a value of `n`, the size is `2^n`.
    pub blocksize_bits: u8,

    /// Maximum size of a file.
    pub maxbytes: i64,

    /// Granularity of c/m/atime in ns (cannot be worse than a second).
    pub time_gran: u32,
}

impl SuperParams {
    /// Default value for instances of [`SuperParams`].
    pub const DEFAULT: Self = Self {
        magic: 0,
        blocksize_bits: crate::PAGE_SIZE as _,
        maxbytes: bindings::MAX_LFS_FILESIZE,
        time_gran: 1,
    };
}

/// A superblock that is still being initialised.
///
/// It uses type states to ensure that callers use the right sequence of calls.
///
/// # Invariants
///
/// The superblock is a newly-created one and this is the only active pointer to it.
pub struct NewSuperBlock<'a, T: Type + ?Sized, S = NeedsInit> {
    sb: *mut bindings::super_block,
    _p: PhantomData<(&'a T, S)>,
}

impl<'a, T: Type + ?Sized> NewSuperBlock<'a, T, NeedsInit> {
    /// Creates a new instance of [`NewSuperBlock`].
    ///
    /// # Safety
    ///
    /// `sb` must point to a newly-created superblock and it must be the only active pointer to it.
    unsafe fn new(sb: *mut bindings::super_block) -> Self {
        // INVARIANT: The invariants are satisfied by the safety requirements of this function.
        Self {
            sb,
            _p: PhantomData,
        }
    }

    /// Initialises the superblock so that it transitions to the [`NeedsRoot`] type state.
    pub fn init(
        self,
        data: T::Data,
        params: &SuperParams,
    ) -> Result<NewSuperBlock<'a, T, NeedsRoot>> {
        // SAFETY: The type invariant guarantees that `self.sb` is the only pointer to a
        // newly-allocated superblock, so it is safe to mutably reference it.
        let sb = unsafe { &mut *self.sb };

        sb.s_magic = params.magic as _;
        sb.s_op = &Tables::<T>::SUPER_BLOCK;
        sb.s_maxbytes = params.maxbytes;
        sb.s_time_gran = params.time_gran;
        sb.s_blocksize_bits = params.blocksize_bits;
        sb.s_blocksize = 1;
        if sb.s_blocksize.leading_zeros() < params.blocksize_bits.into() {
            return Err(EINVAL);
        }
        sb.s_blocksize = 1 << sb.s_blocksize_bits;

        // Keyed file systems already have `s_fs_info` initialised.
        let info = data.into_foreign() as *mut _;
        if let Super::Keyed = T::SUPER_TYPE {
            // SAFETY: We just called `into_foreign` above.
            unsafe { T::Data::from_foreign(info) };

            if sb.s_fs_info != info {
                return Err(EINVAL);
            }
        } else {
            sb.s_fs_info = info;
        }

        Ok(NewSuperBlock {
            sb: self.sb,
            _p: PhantomData,
        })
    }

    /// Obtains the DAX device associated with the superblock's block device.
    pub fn get_dax_dev(&self) -> Result<*mut bindings::dax_device> {
        if let Super::BlockDev = T::SUPER_TYPE {
            let mut start_off: u64 = 0;

            // SAFETY: The type invariant guarantees that `self.sb` is the only pointer to a
            // newly-allocated superblock, so it is safe to mutably reference it. The caller must
            // only call `get_dax_dev` once on a given superblock to ensure there is only one active
            // pointer to the `dax_device` structure associated with that superblock.
            let dax_dev = unsafe {
                bindings::fs_dax_get_by_bdev(
                    (*self.sb).s_bdev,
                    &mut start_off,
                    ptr::null_mut(),
                    ptr::null_mut(),
                )
            };
            if dax_dev.is_null() {
                return Err(EINVAL);
            }

            Ok(dax_dev)
        } else {
            Err(ENOTBLK)
        }
    }

    /// Returns the inner `struct super_block`
    pub unsafe fn get_inner(&self) -> *mut bindings::super_block {
        self.sb
    }
}

impl<'a, T: Type + ?Sized> NewSuperBlock<'a, T, NeedsRoot> {
    /// Initialises the root of the superblock.
    pub fn init_root(self) -> Result<&'a SuperBlock<T>> {
        // The following is temporary code to create the root inode and dentry. It will be replaced
        // once we allow inodes and dentries to be created directly from Rust code.

        // SAFETY: `sb` is initialised (`NeedsRoot` typestate implies it), so it is safe to pass it
        // to `new_inode`.
        let inode = unsafe { bindings::new_inode(self.sb) };
        if inode.is_null() {
            return Err(ENOMEM);
        }

        {
            // SAFETY: This is a newly-created inode. No other references to it exist, so it is
            // safe to mutably dereference it.
            let inode = unsafe { &mut *inode };

            // SAFETY: `current_time` requires that `inode.sb` be valid, which is the case here
            // since we allocated the inode through the superblock.
            let time = unsafe { bindings::current_time(inode) };
            inode.i_ino = 1;
            inode.i_mode = (bindings::S_IFDIR | 0o755) as _;
            inode.i_mtime = time;
            inode.i_atime = time;
            inode.i_ctime = time;
            // TODO: set based on the system/device block size
            inode.i_size = 4096;
            inode.i_blkbits = unsafe { bindings::blksize_bits(4096).try_into()? };

            // SAFETY: `simple_dir_operations` never changes, it's safe to reference it.
            // TODO: update safety
            inode.__bindgen_anon_3.i_fop = unsafe { dir::OperationsVtable::<T::DirOps>::build() };

            // SAFETY: `simple_dir_inode_operations` never changes, it's safe to reference it.
            // TODO: update safety
            inode.i_op = unsafe { inode::OperationsVtable::<T::InodeOps>::build() };

            // SAFETY: `inode` is valid for write.
            unsafe { bindings::set_nlink(inode, 2) };

            T::init_private(inode)?;
        }

        // SAFETY: `d_make_root` requires that `inode` be valid and referenced, which is the
        // case for this call.
        //
        // It takes over the inode, even on failure, so we don't need to clean it up.
        let dentry = unsafe { bindings::d_make_root(inode) };
        if dentry.is_null() {
            return Err(ENOMEM);
        }

        // SAFETY: The typestate guarantees that `self.sb` is valid.
        unsafe { (*self.sb).s_root = dentry };

        // SAFETY: The typestate guarantees that `self.sb` is initialised and we just finished
        // setting its root, so it's a fully ready superblock.
        Ok(unsafe { &mut *self.sb.cast() })
    }

    /// Initializes the root of the superblock from a given inode
    /// TODO: safer approach that does not require passing in a raw pointer to an inode
    /// that the caller is supposed to set up
    pub fn init_root_from_inode(self, inode: *mut bindings::inode) -> Result<&'a SuperBlock<T>> {
        // SAFETY: `simple_dir_operations` never changes, it's safe to reference it.

        unsafe {
            // (*inode).__bindgen_anon_3.i_fop = &bindings::simple_dir_operations;
            (*inode).__bindgen_anon_3.i_fop = dir::OperationsVtable::<T::DirOps>::build();

            // TODO: safety
            (*inode).i_op = inode::OperationsVtable::<T::InodeOps>::build();

            T::init_private(inode)?;
        }

        // TODO: safety
        //
        // It takes over the inode, even on failure, so we don't need to clean it up.
        let dentry = unsafe { bindings::d_make_root(inode) };
        if dentry.is_null() {
            return Err(ENOMEM);
        }

        // SAFETY: The typestate guarantees that `self.sb` is valid.
        unsafe { (*self.sb).s_root = dentry };

        // SAFETY: The typestate guarantees that `self.sb` is initialised and we just finished
        // setting its root, so it's a fully ready superblock.
        Ok(unsafe { &mut *self.sb.cast() })
    }

    /// Returns the inner `struct super_block`
    pub unsafe fn get_inner(&self) -> *mut bindings::super_block {
        self.sb
    }
}

/// A file system super block.
///
/// Wraps the kernel's `struct super_block`.
#[repr(transparent)]
pub struct SuperBlock<T: Type + ?Sized>(
    pub(crate) UnsafeCell<bindings::super_block>,
    PhantomData<T>,
);

impl<T: Type + ?Sized> SuperBlock<T> {
    /// Returns a mutable reference to the value stored in superblock's `s_fs_info`
    ///
    /// # Safety
    /// The kernel will let us get as many mutable references to s_fs_info as we want.
    /// Caller is responsible for making sure the actual resources there are protected.
    pub unsafe fn s_fs_info(&self) -> *mut core::ffi::c_void {
        unsafe { (*self.0.get()).s_fs_info }
    }

    /// Returns a raw pointer to the `struct super_block`
    pub fn get_inner(&self) -> *mut bindings::super_block {
        self.0.get()
    }
}

/// Wraps the kernel's `struct inode`.
///
/// # Invariants
///
/// Instances of this type are always ref-counted, that is, a call to `ihold` ensures that the
/// allocation remains valid at least until the matching call to `iput`.
#[repr(transparent)]
pub struct INode(pub(crate) UnsafeCell<bindings::inode>);

// SAFETY: The type invariants guarantee that `INode` is always ref-counted.
unsafe impl AlwaysRefCounted for INode {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::ihold(self.0.get()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::iput(obj.cast().as_ptr()) }
    }
}

#[allow(dead_code)]
impl INode {
    /// Returns the inode's inode number.
    ///
    /// # Safety
    /// TODO: safety
    pub fn i_ino(&self) -> core::ffi::c_ulong {
        unsafe { (*self.0.get()).i_ino }
    }

    /// Returns a reference to the file system's (volatile) super block
    ///
    /// # Safety
    /// TODO: safety
    pub fn i_sb(&self) -> *mut bindings::super_block {
        unsafe { (*self.0.get()).i_sb }
    }

    /// Returns a raw pointer to the `struct inode`.
    pub fn get_inner(&self) -> *mut bindings::inode {
        self.0.get()
    }

    /// Calls the kernel's `inc_nlink` function on the inode
    pub fn inc_nlink(&mut self) {
        unsafe { bindings::inc_nlink(self.0.get()) }
    }

    /// Reads the inode's size using `i_size_read`
    pub fn i_size_read(&self) -> i64 {
        unsafe { bindings::i_size_read(self.0.get()) }
    }

    /// Updates the inode's size using `i_size_write`
    pub fn i_size_write(&mut self, pos: i64) {
        unsafe { bindings::i_size_write(self.0.get(), pos) };
    }

    /// Sets the inode's `i_ctime` field to the current time.
    pub fn update_ctime(&mut self) {
        unsafe {
            let ctime = bindings::current_time(self.0.get());
            self.0.get_mut().i_ctime = ctime;
        }
    }

    /// Sets the inode's `i_ctime` and `i_mtime` fields to the current time.
    pub fn update_ctime_and_mtime(&mut self) {
        unsafe {
            let time = bindings::current_time(self.0.get());
            self.0.get_mut().i_ctime = time;
            self.0.get_mut().i_mtime = time;
        }
    }

    /// Sets the inode's `i_atime` field to the current time.
    pub fn update_atime(&mut self) {
        unsafe {
            let time = bindings::current_time(self.0.get());
            self.0.get_mut().i_atime = time;
        }
    }

    /// Returns the inode's `i_atime` field
    pub fn get_atime(&self) -> bindings::timespec64 {
        unsafe { (*self.0.get()).i_atime }
    }

    /// Returns the inode's `i_mtime` field
    pub fn get_mtime(&self) -> bindings::timespec64 {
        unsafe { (*self.0.get()).i_mtime }
    }

    /// Returns the inode's `i_ctime` field
    pub fn get_ctime(&self) -> bindings::timespec64 {
        unsafe { (*self.0.get()).i_ctime }
    }
}

/// Wraps the kernel's `struct dentry`.
///
/// # Invariants
///
/// Instances of this type are always ref-counted, that is, a call to `dget` ensures that the
/// allocation remains valid at least until the matching call to `dput`.
#[repr(transparent)]
pub struct DEntry(pub(crate) UnsafeCell<bindings::dentry>);

// SAFETY: The type invariants guarantee that `DEntry` is always ref-counted.
unsafe impl AlwaysRefCounted for DEntry {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference means that the refcount is nonzero.
        unsafe { bindings::dget(self.0.get()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is nonzero.
        unsafe { bindings::dput(obj.cast().as_ptr()) }
    }
}

#[allow(dead_code)]
impl DEntry {
    /// Convert the dentry's d_name field to a &CStr and return it
    ///
    /// # Safety
    /// TODO: safety
    pub fn d_name(&self) -> &CStr {
        unsafe { CStr::from_char_ptr((*self.0.get()).d_name.name as *const i8) }
    }

    /// Returns the inode number associated with this dentry
    ///
    /// # Safety
    /// TODO: safety
    pub fn d_ino(&self) -> u64 {
        unsafe { (*(*self.0.get()).d_inode).i_ino }
    }

    /// Returns a raw pointer to the inode associated with this dentry
    ///
    /// # Safety
    /// TODO: safety
    pub fn d_inode(&self) -> *mut bindings::inode {
        unsafe { (*self.0.get()).d_inode }
    }

    /// Returns a raw pointer to the `struct dentry`
    /// ///
    /// # Safety
    /// TODO: safety
    pub fn get_inner(&self) -> *mut bindings::dentry {
        self.0.get()
    }
}

/// Wraps the kernel's `struct filename`.
#[repr(transparent)]
pub struct Filename(pub(crate) UnsafeCell<bindings::filename>);

impl Filename {
    /// Creates a reference to a [`Filename`] from a valid pointer.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `ptr` is valid and remains valid for the lifetime of the
    /// returned [`Filename`] instance.
    pub(crate) unsafe fn from_ptr<'a>(ptr: *const bindings::filename) -> &'a Filename {
        // SAFETY: The safety requirements guarantee the validity of the dereference, while the
        // `Filename` type being transparent makes the cast ok.
        unsafe { &*ptr.cast() }
    }
}

/// Wraps the kernel's `struct user_namespace`.
#[repr(transparent)]
pub struct UserNamespace(pub(crate) UnsafeCell<bindings::user_namespace>);

impl UserNamespace {
    /// Returns a raw pointer to the `struct user_namespace`.
    pub fn get_inner(&self) -> *mut bindings::user_namespace {
        self.0.get()
    }
}

/// Kernel module that exposes a single file system implemented by `T`.
pub struct Module<T: Type> {
    _fs: Pin<Box<Registration>>,
    _p: PhantomData<T>,
}

impl<T: Type + Sync> crate::Module for Module<T> {
    fn init(_name: &'static CStr, module: &'static ThisModule) -> Result<Self> {
        let mut reg = Pin::from(Box::try_new(Registration::new())?);
        reg.as_mut().register::<T>(module)?;
        Ok(Self {
            _fs: reg,
            _p: PhantomData,
        })
    }
}

/// Declares a kernel module that exposes a single file system.
///
/// The `type` argument must be a type which implements the [`Type`] trait. Also accepts various
/// forms of kernel metadata.
///
/// # Examples
///
/// ```ignore
/// use kernel::prelude::*;
/// use kernel::{c_str, fs};
///
/// module_fs! {
///     type: MyFs,
///     name: "my_fs_kernel_module",
///     author: "Rust for Linux Contributors",
///     description: "My very own file system kernel module!",
///     license: "GPL",
/// }
///
/// struct MyFs;
///
/// #[vtable]
/// impl fs::Context<Self> for MyFs {
///     type Data = ();
///     fn try_new() -> Result {
///         Ok(())
///     }
/// }
///
/// impl fs::Type for MyFs {
///     type Context = Self;
///     const SUPER_TYPE: fs::Super = fs::Super::Independent;
///     const NAME: &'static CStr = c_str!("example");
///     const FLAGS: i32 = 0;
///
///     fn fill_super(_data: (), sb: fs::NewSuperBlock<'_, Self>) -> Result<&fs::SuperBlock<Self>> {
///         let sb = sb.init(
///             (),
///             &fs::SuperParams {
///                 magic: 0x6578616d,
///                 ..fs::SuperParams::DEFAULT
///             },
///         )?;
///         let sb = sb.init_root()?;
///         Ok(sb)
///     }
/// }
/// ```
#[macro_export]
macro_rules! module_fs {
    (type: $type:ty, $($f:tt)*) => {
        type ModuleType = $crate::fs::Module<$type>;
        $crate::macros::module! {
            type: ModuleType,
            $($f)*
        }
    }
}
