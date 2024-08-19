// SPDX-License-Identifier: GPL-2.0

//! The `kernel` prelude.
//!
//! These are the most common items used by Rust code in the kernel,
//! intended to be imported by all Rust code, for convenience.
//!
//! # Examples
//!
//! ```
//! use kernel::prelude::*;
//! ```

#[doc(no_inline)]
pub use core::pin::Pin;

#[doc(no_inline)]
pub use alloc::{boxed::Box, string::String, vec::Vec};

#[doc(no_inline)]
pub use macros::{module, vtable};

pub use super::build_assert;

// `super::std_vendor` is hidden, which makes the macro inline for some reason.
#[doc(no_inline)]
pub use super::dbg;
pub use super::{
    dev_alert, dev_crit, dev_dbg, dev_emerg, dev_err, dev_info, dev_notice, dev_warn, fmt,
    pr_alert, pr_crit, pr_debug, pr_emerg, pr_err, pr_info, pr_notice, pr_warn,
};

pub use super::{module_fs, module_misc_device};

#[cfg(CONFIG_ARM_AMBA)]
pub use super::module_amba_driver;

pub use super::static_assert;

pub use super::error::{code::*, Error, Result};

pub use super::{str::CStr, ARef, ThisModule};
