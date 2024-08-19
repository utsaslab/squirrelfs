// SPDX-License-Identifier: GPL-2.0

//! Memory management.
//!
//! C header: [`include/linux/mm.h`](../../../../include/linux/mm.h)

#[allow(unused_imports)]
use crate::{bindings, pages, to_result, Result};
use core::marker;
use macros::vtable;

/// Virtual memory.
pub mod virt {
    use super::*;

    /// A wrapper for the kernel's `struct vm_area_struct`.
    ///
    /// It represents an area of virtual memory.
    ///
    /// # Invariants
    ///
    /// `vma` is always non-null and valid.
    pub struct Area {
        vma: *mut bindings::vm_area_struct,
    }

    impl Area {
        /// Creates a new instance of a virtual memory area.
        ///
        /// # Safety
        ///
        /// Callers must ensure that `vma` is non-null and valid for the duration of the new area's
        /// lifetime.
        #[allow(dead_code)]
        pub(crate) unsafe fn from_ptr(vma: *mut bindings::vm_area_struct) -> Self {
            // INVARIANTS: The safety requirements guarantee the invariants.
            Self { vma }
        }

        /// Returns the flags associated with the virtual memory area.
        ///
        /// The possible flags are a combination of the constants in [`flags`].
        pub fn flags(&self) -> usize {
            // SAFETY: `self.vma` is valid by the type invariants.
            unsafe { (*self.vma).__bindgen_anon_1.vm_flags as _ }
        }

        /// Sets the flags associated with the virtual memory area.
        ///
        /// The possible flags are a combination of the constants in [`flags`].
        pub fn set_flags(&mut self, flags: usize) {
            // TODO: use the new wrapper functions, see commit bc292ab00f6c
            // ("mm: introduce vma->vm_flags wrapper functions").
            // SAFETY: `self.vma` is valid by the type invariants.
            unsafe { (*self.vma).__bindgen_anon_1.__vm_flags = flags as _ };
        }

        /// Returns the start address of the virtual memory area.
        pub fn start(&self) -> usize {
            // SAFETY: `self.vma` is valid by the type invariants.
            unsafe { (*self.vma).vm_start as _ }
        }

        /// Returns the end address of the virtual memory area.
        pub fn end(&self) -> usize {
            // SAFETY: `self.vma` is valid by the type invariants.
            unsafe { (*self.vma).vm_end as _ }
        }

        /// Maps a single page at the given address within the virtual memory area.
        pub fn insert_page(&mut self, address: usize, page: &pages::Pages<0>) -> Result {
            // SAFETY: The page is guaranteed to be order 0 by the type system. The range of
            // `address` is already checked by `vm_insert_page`. `self.vma` and `page.pages` are
            // guaranteed by their repective type invariants to be valid.
            to_result(unsafe { bindings::vm_insert_page(self.vma, address as _, page.pages) })
        }

        /// Sets the VMA's operations to the given structure
        pub fn set_ops(&self, ops: &bindings::vm_operations_struct) {
            unsafe { (*self.vma).vm_ops = ops };
            if unsafe { (*(*self.vma).vm_ops).fault.is_some() } {
                crate::pr_info!("op fault is set\n");
            }
        }
    }

    /// Container for [`Area`] flags.
    pub mod flags {
        use crate::bindings;

        /// No flags are set.
        pub const NONE: usize = bindings::VM_NONE as _;

        /// Mapping allows reads.
        pub const READ: usize = bindings::VM_READ as _;

        /// Mapping allows writes.
        pub const WRITE: usize = bindings::VM_WRITE as _;

        /// Mapping allows execution.
        pub const EXEC: usize = bindings::VM_EXEC as _;

        /// Mapping is shared.
        pub const SHARED: usize = bindings::VM_SHARED as _;

        /// Mapping may be updated to allow reads.
        pub const MAYREAD: usize = bindings::VM_MAYREAD as _;

        /// Mapping may be updated to allow writes.
        pub const MAYWRITE: usize = bindings::VM_MAYWRITE as _;

        /// Mapping may be updated to allow execution.
        pub const MAYEXEC: usize = bindings::VM_MAYEXEC as _;

        /// Mapping may be updated to be shared.
        pub const MAYSHARE: usize = bindings::VM_MAYSHARE as _;

        /// Do not copy this vma on fork.
        pub const DONTCOPY: usize = bindings::VM_DONTCOPY as _;

        /// Cannot expand with mremap().
        pub const DONTEXPAND: usize = bindings::VM_DONTEXPAND as _;

        /// Lock the pages covered when they are faulted in.
        pub const LOCKONFAULT: usize = bindings::VM_LOCKONFAULT as _;

        /// Is a VM accounted object.
        pub const ACCOUNT: usize = bindings::VM_ACCOUNT as _;

        /// should the VM suppress accounting.
        pub const NORESERVE: usize = bindings::VM_NORESERVE as _;

        /// Huge TLB Page VM.
        pub const HUGETLB: usize = bindings::VM_HUGETLB as _;

        /// Synchronous page faults.
        pub const SYNC: usize = bindings::VM_SYNC as _;

        /// Architecture-specific flag.
        pub const ARCH_1: usize = bindings::VM_ARCH_1 as _;

        /// Wipe VMA contents in child..
        pub const WIPEONFORK: usize = bindings::VM_WIPEONFORK as _;

        /// Do not include in the core dump.
        pub const DONTDUMP: usize = bindings::VM_DONTDUMP as _;

        /// Not soft dirty clean area.
        pub const SOFTDIRTY: usize = bindings::VM_SOFTDIRTY as _;

        /// Can contain "struct page" and pure PFN pages.
        pub const MIXEDMAP: usize = bindings::VM_MIXEDMAP as _;

        /// MADV_HUGEPAGE marked this vma.
        pub const HUGEPAGE: usize = bindings::VM_HUGEPAGE as _;

        /// MADV_NOHUGEPAGE marked this vma.
        pub const NOHUGEPAGE: usize = bindings::VM_NOHUGEPAGE as _;

        /// KSM may merge identical pages.
        pub const MERGEABLE: usize = bindings::VM_MERGEABLE as _;
    }
}

/// vtable for VMA operations
pub struct OperationsVtable<T: Operations>(marker::PhantomData<T>);

impl<T: Operations> OperationsVtable<T> {
    unsafe extern "C" fn fault_callback(vmf: *mut bindings::vm_fault) -> bindings::vm_fault_t {
        let vmf = unsafe { &mut *vmf.cast() };
        T::fault(vmf)
    }

    unsafe extern "C" fn huge_fault_callback(
        vmf: *mut bindings::vm_fault,
        pe: bindings::page_entry_size,
    ) -> bindings::vm_fault_t {
        let vmf = unsafe { &mut *vmf.cast() };
        T::huge_fault(vmf, pe)
    }

    const VTABLE: bindings::vm_operations_struct = bindings::vm_operations_struct {
        open: None,
        close: None,
        may_split: None,
        mremap: None,
        mprotect: None,
        fault: Some(Self::fault_callback),
        huge_fault: Some(Self::huge_fault_callback),
        map_pages: None,
        page_mkwrite: Some(Self::fault_callback),
        pfn_mkwrite: Some(Self::fault_callback),
        access: None,
        name: None,
        find_special_page: None,
        dax_cow: None,
        get_policy: None,
        set_policy: None,
        pagesize: None,
    };

    /// Builds an instance of [`struct vm_operations_struct`]
    /// Safety: TODO
    pub const unsafe fn build() -> &'static bindings::vm_operations_struct {
        &Self::VTABLE
    }
}

/// Corresponds to the kernel's `struct vm_operations_struct`.
///
/// You implement this trait whenver you would create a `struct vm_operations_struct`.
#[vtable]
pub trait Operations {
    /// Corresponds to `fault` function pointer in `struct vm_operations_struct`
    fn fault(vmf: &mut bindings::vm_fault) -> bindings::vm_fault_t;

    /// Corresponds to `huge_fault` function pointer in `struct vm_operations_struct`
    fn huge_fault(
        vmf: &mut bindings::vm_fault,
        pe: bindings::page_entry_size,
    ) -> bindings::vm_fault_t;
}
