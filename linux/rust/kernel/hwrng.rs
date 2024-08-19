// SPDX-License-Identifier: GPL-2.0

//! Hardware Random Number Generator.
//!
//! C header: [`include/linux/hw_random.h`](../../../../include/linux/hw_random.h)

use alloc::{boxed::Box, slice::from_raw_parts_mut};

use crate::{
    bindings, error::code::*, error::from_kernel_result, str::CString, to_result,
    types::ForeignOwnable, Result, ScopeGuard,
};
use macros::vtable;

use core::{cell::UnsafeCell, fmt, marker::PhantomData, pin::Pin};

/// This trait is implemented in order to provide callbacks to `struct hwrng`.
#[vtable]
pub trait Operations {
    /// The pointer type that will be used to hold user-defined data type.
    type Data: ForeignOwnable + Send + Sync = ();

    /// Initialization callback, can be left undefined.
    fn init(_data: <Self::Data as ForeignOwnable>::Borrowed<'_>) -> Result {
        Err(EINVAL)
    }

    /// Cleanup callback, can be left undefined.
    fn cleanup(_data: Self::Data) {}

    /// Read data into the provided buffer.
    /// Drivers can fill up to max bytes of data into the buffer.
    /// The buffer is aligned for any type and its size is a multiple of 4 and >= 32 bytes.
    fn read(
        data: <Self::Data as ForeignOwnable>::Borrowed<'_>,
        buffer: &mut [u8],
        wait: bool,
    ) -> Result<u32>;
}

/// Registration structure for Hardware Random Number Generator driver.
pub struct Registration<T: Operations> {
    hwrng: UnsafeCell<bindings::hwrng>,
    name: Option<CString>,
    registered: bool,
    _p: PhantomData<T>,
}

impl<T: Operations> Registration<T> {
    /// Creates new instance of registration.
    ///
    /// The data must be registered.
    pub fn new() -> Self {
        Self {
            hwrng: UnsafeCell::new(bindings::hwrng::default()),
            name: None,
            registered: false,
            _p: PhantomData,
        }
    }

    /// Returns a registered and pinned, heap-allocated representation of the registration.
    pub fn new_pinned(
        name: fmt::Arguments<'_>,
        quality: u16,
        data: T::Data,
    ) -> Result<Pin<Box<Self>>> {
        let mut reg = Pin::from(Box::try_new(Self::new())?);
        reg.as_mut().register(name, quality, data)?;
        Ok(reg)
    }

    /// Registers a hwrng device within the rest of the kernel.
    ///
    /// It must be pinned because the memory block that represents
    /// the registration may be self-referential.
    pub fn register(
        self: Pin<&mut Self>,
        name: fmt::Arguments<'_>,
        quality: u16,
        data: T::Data,
    ) -> Result {
        // SAFETY: We never move out of `this`.
        let this = unsafe { self.get_unchecked_mut() };

        if this.registered {
            return Err(EINVAL);
        }

        let data_pointer = data.into_foreign();

        // SAFETY: `data_pointer` comes from the call to `data.into_foreign()` above.
        let guard = ScopeGuard::new(|| unsafe {
            T::Data::from_foreign(data_pointer);
        });

        let name = CString::try_from_fmt(name)?;

        // SAFETY: Registration is pinned and contains allocated and set to zero
        // `bindings::hwrng` structure.
        Self::init_hwrng(
            unsafe { &mut *this.hwrng.get() },
            &name,
            quality,
            data_pointer,
        );

        // SAFETY: `bindings::hwrng` is initialized above which guarantees safety.
        to_result(unsafe { bindings::hwrng_register(this.hwrng.get()) })?;

        this.registered = true;
        this.name = Some(name);
        guard.dismiss();
        Ok(())
    }

    fn init_hwrng(
        hwrng: &mut bindings::hwrng,
        name: &CString,
        quality: u16,
        data: *const core::ffi::c_void,
    ) {
        hwrng.name = name.as_char_ptr();

        hwrng.init = if T::HAS_INIT {
            Some(Self::init_callback)
        } else {
            None
        };
        hwrng.cleanup = if T::HAS_CLEANUP {
            Some(Self::cleanup_callback)
        } else {
            None
        };
        hwrng.data_present = None;
        hwrng.data_read = None;
        hwrng.read = Some(Self::read_callback);

        hwrng.priv_ = data as _;
        hwrng.quality = quality;

        // SAFETY: All fields are properly initialized as
        // remaining fields `list`, `ref` and `cleanup_done` are already
        // zeroed by `bindings::hwrng::default()` call.
    }

    unsafe extern "C" fn init_callback(rng: *mut bindings::hwrng) -> core::ffi::c_int {
        from_kernel_result! {
            // SAFETY: `priv` private data field was initialized during creation of
            // the `bindings::hwrng` in `Self::init_hwrng` method. This callback is only
            // called once the driver is registered.
            let data = unsafe { T::Data::borrow((*rng).priv_ as *const _) };
            T::init(data)?;
            Ok(0)
        }
    }

    unsafe extern "C" fn cleanup_callback(rng: *mut bindings::hwrng) {
        // SAFETY: `priv` private data field was initialized during creation of
        // the `bindings::hwrng` in `Self::init_hwrng` method. This callback is only
        // called once the driver is registered.
        let data = unsafe { T::Data::from_foreign((*rng).priv_ as *const _) };
        T::cleanup(data);
    }

    unsafe extern "C" fn read_callback(
        rng: *mut bindings::hwrng,
        data: *mut core::ffi::c_void,
        max: usize,
        wait: bindings::bool_,
    ) -> core::ffi::c_int {
        from_kernel_result! {
            // SAFETY: `priv` private data field was initialized during creation of
            // the `bindings::hwrng` in `Self::init_hwrng` method. This callback is only
            // called once the driver is registered.
            let drv_data = unsafe { T::Data::borrow((*rng).priv_ as *const _) };

            // SAFETY: Slice is created from `data` and `max` arguments that are C's buffer
            // along with its size in bytes that are safe for this conversion.
            let buffer = unsafe { from_raw_parts_mut(data as *mut u8, max) };
            let ret = T::read(drv_data, buffer, wait)?;
            Ok(ret as _)
        }
    }
}

impl<T: Operations> Default for Registration<T> {
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: `Registration` does not expose any of its state across threads.
unsafe impl<T: Operations> Sync for Registration<T> {}

// SAFETY: `Registration` is not restricted to a single thread,
// its `T::Data` is also `Send` so it may be moved to different threads.
#[allow(clippy::non_send_fields_in_send_ty)]
unsafe impl<T: Operations> Send for Registration<T> {}

impl<T: Operations> Drop for Registration<T> {
    /// Removes the registration from the kernel if it has completed successfully before.
    fn drop(&mut self) {
        // SAFETY: The instance of Registration<T> is unregistered only
        // after being initialized and registered before.
        if self.registered {
            unsafe { bindings::hwrng_unregister(self.hwrng.get()) };
        }
    }
}
