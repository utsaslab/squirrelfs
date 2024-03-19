use crate::defs::*;
use crate::pm::*;
use core::{ffi, slice};
use kernel::prelude::*;
use kernel::{
    rbtree::RBTree,
    sync::{smutex::Mutex, Arc},
};

/// This file contains structures to store index data durably, so that
/// we don't have to keep all of it in volatile memory all of the time.
/// Note that we make NO ATTEMPT to keep this structure crash consistent,
/// and we only write out updates when a file handle is released.
/// It is not safe to use information found in these structures after a crash.

// maps an inode to the first node in its linked list of pages
// This doesn't actually have to store the ino because its index
// is its ino
// TODO: what is the actual size of this structure?
#[repr(C)]
struct InodeIndex {
    in_use: u8,
    list_head: usize,
    list_tail: usize,
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
    inode_array: &'a mut [InodeIndex],
    page_area: &'a mut [PageNode],
}

impl<'a> DurableIndex<'a> {
    fn check_ino_valid(&self, ino: InodeNum) -> Result<()> {
        let ino_usize: usize = ino.try_into()?;
        if ino_usize == 0 || ino_usize > self.inode_array.len() {
            pr_info!("invalid inode {:?} passed to durable index\n", ino);
            return Err(EINVAL);
        }
        Ok(())
    }

    // Note: you should use `check_ino_valid` before calling this function.
    // this function does NOT do a validity check and returns false for all
    // invalid inodes.
    fn check_ino_in_use(&self, ino: usize) -> bool {
        let index_entry = self.inode_array.get(ino);
        if let Some(index_entry) = index_entry {
            if index_entry.in_use == 0 {
                false
            } else {
                true
            }
        } else {
            false
        }
    }

    fn init_ino_entry(&mut self, ino: usize) -> Result<()> {
        if ino == 0 {
            pr_info!("Inode 0 is not valid in durable index\n");
            return Err(EINVAL);
        }
        let index_entry = self.inode_array.get_mut(ino);
        if let Some(index_entry) = index_entry {
            index_entry.in_use = 1;
            hayleyfs_flush_buffer(index_entry, INODE_INDEX_SIZE.try_into()?, false);
            Ok(())
        } else {
            pr_info!("Attempted to set list head for invalid ino {:?}\n", ino);
            return Err(EINVAL);
        }
    }

    // this function appends pages in the given RB tree to the list associated with the
    // given inode. it does not check if these pages are already in the list.
    // the ino should also be first checked for validity and be in use
    fn append_pages_to_list(
        &mut self,
        ino: usize,
        pages: RBTree<u64, PageNum>,
        page_slots: Vec<usize>, // indexes of free slots in the page area; should be obtained from wrapper-level allocator
    ) -> Result<()> {
        if ino == 0 {
            pr_info!("Inode 0 is not valid in durable index\n");
            return Err(EINVAL);
        }
        let index_entry = self.inode_array.get_mut(ino);
        if let Some(index_entry) = index_entry {
            let mut current_node = index_entry.list_tail;
            let mut current_page_slot_index = 0;
            for (offset, page_num) in pages.iter() {
                if current_node == 0 {
                    // the list is empty; need to set the index entry's head and tail
                    index_entry.list_head = page_slots[current_page_slot_index];
                    index_entry.list_tail = page_slots[current_page_slot_index];
                    current_page_slot_index += 1;
                    current_node = index_entry.list_tail;
                } else {
                    // the current node exists
                    // TODO!!
                }
            }
            Ok(())
        } else {
            pr_info!("Attempted to append pages for invalid ino {:?}\n", ino);
            return Err(EINVAL);
        }
    }
}

pub(crate) struct DurableIndexWrapper<'a> {
    index: DurableIndex<'a>,
    free_page_area_indices: Arc<Mutex<Vec<usize>>>, // TODO: use a better structure for this
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
            let inode_array_virt_addr = inode_array_virt_addr as *mut InodeIndex;
            let inode_array =
                slice::from_raw_parts_mut(inode_array_virt_addr, sbi.num_inodes.try_into()?);
            let inode_index_size_usize: usize = INODE_INDEX_SIZE.try_into()?;
            let inode_array_size: usize = inode_array.len() * inode_index_size_usize;
            let page_area_virt_addr = inode_array_virt_addr.offset(inode_array_size.try_into()?);
            memset_nt(
                page_area_virt_addr as *mut ffi::c_void,
                0,
                (sbi.num_pages * PAGE_NODE_SIZE).try_into()?,
                true,
            );
            let page_area_virt_addr = page_area_virt_addr as *mut PageNode;
            let page_area =
                slice::from_raw_parts_mut(page_area_virt_addr, sbi.num_pages.try_into()?);
            DurableIndex {
                inode_array,
                page_area,
            }
        };

        // set up the free list to keep track of which slots in the page area are free
        // TODO: will a vec be big enough for larger devices?
        let mut free_list = Vec::try_with_capacity(sbi.num_pages.try_into()?)?;
        // build the free list. 0 is never used
        for i in 1..durable_index.page_area.len() {
            free_list.try_push(i)?;
        }

        Ok(DurableIndexWrapper {
            index: durable_index,
            free_page_area_indices: Arc::try_new(Mutex::new(free_list))?,
        })
    }

    // TODO: probably need a guard around the pages list
    // TODO: either need to use interior mutability (and reason about why it's safe)
    // or put a lock around this whole thing. could divide amongst cpus also
    pub(crate) fn insert(&mut self, ino: InodeNum, pages: RBTree<u64, PageNum>) -> Result<()> {
        // first, sanity check -- is the inode we passed in valid and does it already exist
        // in the durable index?
        self.index.check_ino_valid(ino)?;
        if !self.index.check_ino_in_use(ino.try_into()?) {
            self.index.init_ino_entry(ino.try_into()?)?;
        } else {
            pr_info!("Ino slot {:?} is already in use\n", ino);
            return Err(EEXIST);
        }
        Ok(())
    }

    fn allocate_page_slot(&mut self) -> Result<usize> {
        // lock the free list, obtain the last element, and
        // then remove it from the list. we remove the last element
        // to avoid the vector internally moving any other elements around
        let free_list = self.free_page_area_indices.clone();
        let mut free_list = free_list.lock();
        let len = free_list.len();
        if len == 0 {
            pr_info!("index page area has no free slots\n");
            return Err(ENOSPC);
        }
        let free_slot = free_list[len - 1];
        free_list.remove(len - 1);
        Ok(free_slot)
    }

    // allocates n slots from the page area while only acquiring the lock once
    fn allocate_n_slots(&mut self, n: usize) -> Result<Vec<usize>> {
        let free_list = self.free_page_area_indices.clone();
        let mut free_list = free_list.lock();
        let len = free_list.len();
        if len < n {
            pr_info!(
                "index page area does not have {:?} free slots (has {:?})\n",
                n,
                len
            );
            return Err(ENOSPC);
        }
        let free_slots = free_list.drain((len - 1) - n..len - 1);
        let free_slots = free_slots.as_slice();
        let mut free_slots_vec = Vec::new();
        free_slots_vec.try_extend_from_slice(free_slots)?;
        Ok(free_slots_vec)
    }
}
