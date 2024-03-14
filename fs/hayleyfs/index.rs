use crate::defs::*;
use crate::pm::*;
use core::{ffi, slice};
use kernel::prelude::*;

/// This file contains structures to store index data durably, so that
/// we don't have to keep all of it in volatile memory all of the time.
/// Note that we make NO ATTEMPT to keep this structure crash consistent,
/// and we only write out updates when a file handle is released.
/// It is not safe to use information found in these structures after a crash.

// maps an inode to the first node in its linked list of pages
#[repr(C)]
struct InodeIndex {
    ino: InodeNum,
    list_head: u64,
}

// A node in a linked list of pages. Note that the pages are not sorted
// in any particular order
#[repr(C)]
struct PageNode {
    page_no: PageNum,
    next: u64,
}

#[repr(C)]
struct DurableIndex<'a> {
    inode_array: &'a [InodeIndex],
    page_area: &'a [PageNode],
}

pub(crate) struct DurableIndexWrapper<'a> {
    index: DurableIndex<'a>,
    free_page_area_indices: Vec<u64>, // TODO: use a better structure for this
}

// public methods that this needs to have:
// - add an inode (potentially along with a list of pages)
// - add page(s) to an existing inode
// - remove page(s) from an existing inode (do we support holes?)
// - remove an inode (and all of its pages)
// private methods:
// - allocate/deallocate a linked list slot

impl<'a> DurableIndexWrapper<'a> {
    pub(crate) fn initialize(sbi: &SbInfo) -> Result<Self> {
        let start_page = sbi.get_index_region_start_page();
        let index_region_offset: u64 = start_page * HAYLEYFS_PAGESIZE;
        let durable_index = unsafe {
            let inode_array_virt_addr = sbi.get_virt_addr().offset(index_region_offset.try_into()?);
            memset_nt(
                inode_array_virt_addr as *mut ffi::c_void,
                0,
                (sbi.num_inodes * INODE_INDEX_SIZE).try_into()?,
                false,
            );
            let inode_array_virt_addr = inode_array_virt_addr as *const InodeIndex;
            let inode_array =
                slice::from_raw_parts(inode_array_virt_addr, sbi.num_inodes.try_into()?);
            let inode_array_size: usize = inode_array.len() * INODE_INDEX_SIZE.try_into()?;
            let page_area_virt_addr = inode_array_virt_addr.offset(inode_array_size.try_into()?);
            memset_nt(
                page_area_virt_addr as *mut ffi::c_void,
                0,
                (sbi.num_pages * PAGE_NODE_SIZE).try_into()?,
                true,
            );
            let page_area_virt_addr = page_area_virt_addr as *const PageNode;
            let page_area = slice::from_raw_parts(page_area_virt_addr, sbi.num_pages.try_into()?);

            DurableIndex {
                inode_array,
                page_area,
            }
        };

        // set up the free list to keep track of which slots in the page area are free
        let free_list = Vec::try_with_capacity(sbi.num_pages.try_into()?)?;
        // TODO: add to the free list
    }
}
