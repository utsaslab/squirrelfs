use crate::defs::*;
use core::ffi::c_void;
// use core::{arch::asm, ffi::c_void};
use kernel::bindings;
use kernel::prelude::*;

#[allow(dead_code)]
pub(crate) const CACHELINE_BYTE_SHIFT: usize = 6;

/// Taken from Corundum
/// Flushes cache line back to memory
#[allow(dead_code)]
#[inline(never)]
pub(crate) extern "C" fn hayleyfs_flush_buffer<T: ?Sized>(ptr: *const T, len: usize, fence: bool) {
    // #[cfg(CONFIG_HAYLEYFS_DEBUG)]
    unsafe {
        bindings::flush_buffer(ptr as *const c_void, len.try_into().unwrap(), fence);
    }
    // // #[cfg(not(feature = "no_persist"))]
    // #[cfg(not(CONFIG_HAYLEYFS_DEBUG))]
    // {
    //     let ptr = ptr as *const u8 as *mut u8;
    //     let mut start = ptr as usize;
    //     start = (start >> CACHELINE_BYTE_SHIFT) << CACHELINE_BYTE_SHIFT; // TODO: confirm
    //     let end = start + len;
    //     // TODO: properly check architecture and choose correct cache line flush instruction
    //     while start < end {
    //         unsafe {
    //             // #[cfg(not(any(feature = "use_clflushopt", feature = "use_clwb")))]
    //             // {

    //             //     asm!("clflush [{}]", in(reg) (start as *const u8), options(nostack));
    //             // }
    //             // #[cfg(all(feature = "use_clflushopt", not(feature = "use_clwb")))]
    //             // {
    //             //     asm!("clflushopt [{}]", in(reg) (start as *const u8), options(nostack));
    //             // }
    //             // #[cfg(all(feature = "use_clwb", not(feature = "use_clflushopt")))]
    //             // {
    //             asm!("clwb [{}]", in(reg) (start as *const u8), options(nostack));
    //             // }
    //             // #[cfg(all(feature = "use_clwb", feature = "use_clflushopt"))]
    //             // {
    //             //     compile_error!("Please Select only one from clflushopt and clwb")
    //             // }
    //         }
    //         start += 64;
    //     }
    // }
    // if fence {
    //     sfence();
    // }
}

#[allow(dead_code)]
#[inline(never)]
pub(crate) fn sfence() {
    unsafe {
        // asm!("sfence");
        bindings::sfence();
    }
}

#[allow(dead_code)]
#[inline(never)]
pub(crate) unsafe fn memcpy_nt<T: ?Sized>(
    src: *const T,
    dst: *mut T,
    size: usize,
    fence: bool,
) -> Result<u64> {
    let src = src as *const c_void as *mut c_void;
    let dst = dst as *mut c_void;
    let size: u32 = size.try_into()?;

    let ret = unsafe { bindings::copy_from_user_inatomic_nocache(src, dst, size) };

    // copy_from_user_inatomic_nocache uses __copy_user_nocache
    // (https://elixir.bootlin.com/linux/latest/source/arch/x86/lib/copy_user_64.S#L272)
    // which uses non-temporal stores EXCEPT for non-8-byte-aligned sections at the beginning
    // and end of the buffer. We need to flush the edge cache lines to make sure these
    // regions are persistent.
    unsafe { flush_edge_cachelines(dst, size.try_into()?)? };

    if fence {
        sfence();
    }

    Ok(ret)
}

pub(crate) unsafe fn flush_edge_cachelines(ptr: *mut c_void, size: u64) -> Result<()> {
    let raw_ptr = ptr as u64;
    if raw_ptr & 0x7 != 0 {
        hayleyfs_flush_buffer(ptr, 1, false);
    }
    if (raw_ptr + size) & 0x7 != 0 {
        unsafe { hayleyfs_flush_buffer(ptr.offset(size.try_into()?), 1, false) };
    }

    Ok(())
}

/// Adapted from PMFS. Uses non-temporal stores to memset a region.
///
/// # Safety
/// Assumes length and dst+length to be 4-byte aligned. Truncates the region to the
/// last 4-byte boundary. dst does not have to be 4-byte aligned. dst must be the only
/// active pointer to the specified region of memory.
#[inline(never)]
pub(crate) unsafe fn memset_nt(dst: *mut c_void, dword: u32, size: usize, fence: bool) {
    unsafe {
        bindings::memset_nt(dst, dword, size);
    }
    // let qword: u64 = ((dword as u64) << 32) | dword as u64;

    // unsafe {
    //     asm!(
    //         "movq %rdx, %rcx",
    //         "andq $63, %rdx",
    //         "shrq $6, %rcx",
    //         "jz 9f",
    //         "1:",
    //         "movnti %rax, (%rdi)",
    //         "2:",
    //         "movnti %rax, 1*8(%rdi)",
    //         "3:",
    //         "movnti %rax, 2*8(%rdi)",
    //         "4:",
    //         "movnti %rax, 3*8(%rdi)",
    //         "5:",
    //         "movnti %rax, 4*8(%rdi)",
    //         "6:",
    //         "movnti %rax, 5*8(%rdi)",
    //         "7:",
    //         "movnti %rax, 6*8(%rdi)",
    //         "8:",
    //         "movnti %rax, 7*8(%rdi)",
    //         "leaq 64(%rdi), %rdi",
    //         "decq %rcx",
    //         "jnz 1b",
    //         "9:",
    //         "movq %rdx, %rcx",
    //         "andq $7, %rdx",
    //         "shrq $3, %rcx",
    //         "jz 11f",
    //         "10:",
    //         "movnti %rax, (%rdi)",
    //         "leaq 8(%rdi), %rdi",
    //         "decq %rcx",
    //         "jnz 10b",
    //         "11:",
    //         "movq %rdx, %rcx",
    //         "shrq $2, %rcx",
    //         "jz 12f",
    //         "movnti %rax, (%rdi)",
    //         "12:",
    //         in("rdi") dst,
    //         in("rax") qword,
    //         in("rdx") size,
    //         lateout("rdi") _,
    //         lateout("rdx") _,
    //         out("rcx") _,
    //         options(att_syntax)
    //     );
    // }

    if fence {
        sfence();
    }
}

pub(crate) trait Flatten {
    type Output;

    fn flatten_tuple(self) -> Self::Output;
}

impl<A, B> Flatten for (A, B)
where
    A: PmObjWrapper,
    B: PmObjWrapper,
{
    type Output = (A, B);

    fn flatten_tuple(self) -> Self::Output {
        self
    }
}

impl<A, B, C> Flatten for (A, (B, C))
where
    A: PmObjWrapper,
    B: PmObjWrapper,
    C: PmObjWrapper,
{
    type Output = (A, B, C);

    fn flatten_tuple(self) -> Self::Output {
        let (a, (b, c)) = self;
        (a, b, c)
    }
}

impl<A, B, C, D> Flatten for (A, (B, (C, D)))
where
    A: PmObjWrapper,
    B: PmObjWrapper,
    C: PmObjWrapper,
    D: PmObjWrapper,
{
    type Output = (A, B, C, D);

    fn flatten_tuple(self) -> Self::Output {
        let (a, (b, (c, d))) = self;
        (a, b, c, d)
    }
}

// vectors need to be flattenable to work with the macros
// TODO: just remove the ability to fence multiple vectors at once
// and then we can remove this too
impl<A> Flatten for Vec<A>
where
    A: PmObjWrapper,
{
    type Output = Vec<A>;

    fn flatten_tuple(self) -> Self::Output {
        self
    }
}

/// uses a single store fence to persist all arguments and
/// returns their Clean versions in the same order as they were
/// passed in
#[macro_export]
macro_rules! fence_all {
    ($($args:tt),+) => { {
        sfence();
        fence_obj!($($args),+).flatten_tuple()
    }
    }
}

/// helper macro for fence_all
#[macro_export]
macro_rules! fence_obj {
    ($p_obj:ident) => {
        $p_obj.fence()
    };

    ($p_obj0:ident, $($p_obj1:ident),+) => {
        (fence_obj!{$p_obj0}, fence_obj!{$($p_obj1),+})
    };
}

/// uses a single fence call to mark all objects in one or more vectors clean
#[macro_export]
macro_rules! fence_all_vecs {
    ($($args:tt),+) => { {
        sfence();
        fence_vec!($($args),+).flatten_tuple()
    }
    }
}

/// helper macro for fence_all_vecs
#[macro_export]
macro_rules! fence_vec {
    ($p_vec:ident) => {{
        let mut fence_vec = Vec::new();
        for p in $p_vec {
            let p = unsafe { p.fence_unsafe() };
            fence_vec.try_push(p)?;
        }
        fence_vec
    }};

    ($p_vec0:ident, $($p_vec1:ident),+) => {
        (fence_vec!{$p_vec0}, fence_vec!{$($p_vec1),+})
    }
}
