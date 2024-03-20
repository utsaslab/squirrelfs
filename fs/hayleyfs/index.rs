use crate::defs::*;
use kernel::prelude::*;
use kernel::{
    rbtree::RBTree,
    sync::{smutex::Mutex, Arc},
};

#[allow(dead_code)]
pub(crate) struct DurablePageNode {
    page_nos: [PageNum; PAGES_PER_INDEX_NODE],
    next: usize,
}

impl DurablePageNode {
    fn free(&self) -> bool {
        for page_no in self.page_nos.iter() {
            if *page_no != 0 {
                return false;
            }
        }
        true
    }
}

// TODO: per cpu?
pub(crate) struct RBIndexAllocator {
    free_list: Arc<Mutex<RBTree<usize, ()>>>,
}

impl RBIndexAllocator {
    // create a new allocator where all of the entries are free.
    // we always exclude 0 as a valid entry to allocate so that we
    // can interpret it as a null next pointer
    pub(crate) fn new(num_inodes: u64) -> Result<Self> {
        let mut rb: RBTree<usize, ()> = RBTree::new();
        for i in 1..num_inodes {
            rb.try_insert(i.try_into()?, ())?;
        }
        Ok(Self {
            free_list: Arc::try_new(Mutex::new(rb))?,
        })
    }

    // treats any node with all zero contents as free. that may change
    // depending on how we end up handling these lists
    pub(crate) fn rebuild(durable_index: &[DurablePageNode]) -> Result<Self> {
        let mut rb: RBTree<usize, ()> = RBTree::new();
        for i in 1..durable_index.len() {
            if durable_index[i].free() {
                rb.try_insert(i.try_into()?, ())?;
            }
        }
        Ok(Self {
            free_list: Arc::try_new(Mutex::new(rb))?,
        })
    }

    #[allow(dead_code)]
    fn allocate(&self) -> Result<usize> {
        let free_list = Arc::clone(&self.free_list);
        let mut free_list = free_list.lock();
        let iter = free_list.iter().next();
        let slot = match iter {
            None => {
                return Err(ENOSPC);
            }
            Some(slot) => *slot.0,
        };
        free_list.remove(&slot);
        Ok(slot)
    }

    // NOTE: the node in this slot should already be zeroed out!
    #[allow(dead_code)]
    fn deallocate(&self, slot: usize) -> Result<()> {
        let free_list = Arc::clone(&self.free_list);
        let mut free_list = free_list.lock();
        let res = free_list.try_insert(slot, ())?;
        if res.is_some() {
            pr_info!(
                "slot {:?} was deallocated but is already in allocator\n",
                slot
            );
            Err(EINVAL)
        } else {
            Ok(())
        }
    }
}
