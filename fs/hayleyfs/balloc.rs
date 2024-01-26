use crate::defs::*;
use crate::h_dir::*;
use crate::h_inode::*;
use crate::pm::*;
use crate::typestate::*;
use crate::volatile::*;
use core::{
    cmp::Ordering,
    // sync::atomic::{AtomicU64, Ordering},
    ffi,
    marker::PhantomData,
    mem,
    ops::Deref,
    slice,
};
use kernel::prelude::*;
use kernel::{
    io_buffer::{IoBufferReader, IoBufferWriter},
    linked_list::{Cursor, List},
    linked_list::{GetLinks, Links},
    rbtree::RBTree,
    sync::{smutex::Mutex, Arc},
};

pub(crate) trait PageAllocator {
    fn new_from_range(val: u64, dev_pages: u64, cpus: u32) -> Result<Self>
    where
        Self: Sized;
    fn new_from_alloc_vec(
        alloc_pages: List<Box<LinkedPage>>,
        num_alloc_pages: u64,
        start: u64,
        dev_pages: u64,
        cpus: u32,
    ) -> Result<Self>
    where
        Self: Sized;
    fn alloc_page(&self) -> Result<PageNum>;
    fn dealloc_data_page<'a>(&self, page: &DataPageWrapper<'a, Clean, Dealloc>) -> Result<()>;
    fn dealloc_data_page_list(&self, pages: &DataPageListWrapper<Clean, Free>) -> Result<()>;
    fn dealloc_dir_page<'a>(&self, page: &DirPageWrapper<'a, Clean, Dealloc>) -> Result<()>;
    fn dealloc_dir_page_list(&self, pages: &DirPageListWrapper<Clean, Free>) -> Result<()>;
}

// represents one CPU's pool of pages
pub(crate) struct PageFreeList {
    // fields can safely be made public because this structure should always
    // be wrapped in a mutex
    pub(crate) free_pages: u64, // number of free pages in this pool
    pub(crate) list: RBTree<PageNum, ()>,
}

pub(crate) struct PerCpuPageAllocator {
    free_lists: Vec<Arc<Mutex<PageFreeList>>>,
    pages_per_cpu: u64,
    cpus: u32,
    // first page the allocator is allowed to return. used to figure out
    // which cpu deallocated pages belong to
    start: u64,
}

impl PageAllocator for Option<PerCpuPageAllocator> {
    fn new_from_range(val: u64, dev_pages: u64, cpus: u32) -> Result<Self> {
        let total_pages = dev_pages - val;
        let cpus_u64: u64 = cpus.into();
        let pages_per_cpu = total_pages / cpus_u64;
        pr_info!("pages per cpu: {:?}\n", pages_per_cpu);
        let mut current_page = val;
        let mut free_lists = Vec::new();
        for _ in 0..cpus {
            let mut rb_tree = RBTree::new();
            let upper = if (current_page + pages_per_cpu) < dev_pages {
                current_page + pages_per_cpu
            } else {
                dev_pages
            };
            for i in current_page..upper {
                rb_tree.try_insert(i, ())?;
            }
            current_page = current_page + pages_per_cpu;
            let free_list = PageFreeList {
                free_pages: pages_per_cpu,
                list: rb_tree,
            };
            free_lists.try_push(Arc::try_new(Mutex::new(free_list))?)?;
            if upper == dev_pages {
                break;
            }
        }

        Ok(Some(PerCpuPageAllocator {
            free_lists,
            pages_per_cpu,
            cpus,
            start: val,
        }))
    }

    /// alloc_pages must be in sorted order. only pages between start and dev_pages
    /// will be added to the allocator
    fn new_from_alloc_vec(
        alloc_pages: List<Box<LinkedPage>>,
        num_alloc_pages: u64,
        start: u64,
        dev_pages: u64,
        cpus: u32,
    ) -> Result<Self> {
        let total_pages = dev_pages - start;
        let cpus_u64: u64 = cpus.into();
        let pages_per_cpu = total_pages / cpus_u64;
        let mut free_lists = Vec::new();
        let mut page_cursor = alloc_pages.cursor_front();
        let mut current_alloc_page = page_cursor.current();
        let mut current_page = start;
        let mut current_cpu_start = start; // used to keep track of when to move to the next cpu pool
                                           // let mut i = 0;
        let mut rb_tree = RBTree::new();
        if num_alloc_pages > 0 {
            while current_alloc_page.is_some() {
                if let Some(current_alloc_page) = current_alloc_page {
                    let current_alloc_page_no = current_alloc_page.get_page_no();
                    if current_page == current_cpu_start + pages_per_cpu {
                        let free_list = PageFreeList {
                            free_pages: pages_per_cpu,
                            list: rb_tree,
                        };
                        free_lists.try_push(Arc::try_new(Mutex::new(free_list))?)?;
                        rb_tree = RBTree::new();
                        current_cpu_start += pages_per_cpu;
                    }
                    if current_page < current_alloc_page_no {
                        rb_tree.try_insert(current_page, ())?;
                        current_page += 1;
                    } else if current_page == current_alloc_page_no {
                        current_page += 1;
                        page_cursor.move_next();
                    } else {
                        pr_info!(
                            "ERROR: current page is {:?} but current alloc page is {:?}\n",
                            current_page,
                            current_alloc_page_no
                        );
                        return Err(EINVAL);
                    }
                }
                current_alloc_page = page_cursor.current();
            }
        }
        if current_page < dev_pages {
            for current in current_page..dev_pages {
                if current == (current_cpu_start + pages_per_cpu).try_into()? {
                    let free_list = PageFreeList {
                        free_pages: pages_per_cpu,
                        list: rb_tree,
                    };
                    free_lists.try_push(Arc::try_new(Mutex::new(free_list))?)?;
                    rb_tree = RBTree::new();
                    current_cpu_start += pages_per_cpu;
                }
                rb_tree.try_insert(current.try_into()?, ())?;
            }
        }
        // if there is only one cpu, we may not have inserted the free list earlier
        if cpus == 1 && free_lists.len() == 0 {
            let free_list = PageFreeList {
                free_pages: pages_per_cpu,
                list: rb_tree,
            };
            free_lists.try_push(Arc::try_new(Mutex::new(free_list))?)?;
        }

        Ok(Some(PerCpuPageAllocator {
            free_lists,
            pages_per_cpu,
            cpus,
            start,
        }))
    }

    // TODO: allow allocating multiple pages at once
    fn alloc_page(&self) -> Result<PageNum> {
        if let Some(allocator) = self {
            let cpu = get_cpuid(&allocator.cpus);

            let cpu_usize: usize = cpu.try_into()?;
            let free_list = Arc::clone(&allocator.free_lists[cpu_usize]);
            let mut free_list = free_list.lock();

            // does this pool have any free blocks?
            if free_list.free_pages > 0 {
                // TODO: is using an iterator the fastest way to do this?
                let iter = free_list.list.iter().next();
                let page = match iter {
                    None => {
                        pr_info!("ERROR: unable to get free page on CPU {:?}\n", cpu);
                        return Err(ENOSPC);
                    }
                    Some(page) => *page.0,
                };
                free_list.list.remove(&page);
                free_list.free_pages -= 1;
                Ok(page)
            } else {
                // drop the free_list lock so that we can't deadlock with other processes that might
                // be looking for free pages at that CPU
                drop(free_list);
                // find the free list with the most free blocks and allocate from there
                // TODO: can we do this without so much locking?
                let mut num_free_pages = 0;
                let mut cpuid = 0;
                for i in 0..allocator.cpus {
                    // skip the one we've already checked
                    if i != cpu {
                        let i_usize: usize = i.try_into()?;
                        let free_list = Arc::clone(&allocator.free_lists[i_usize]);
                        let free_list = free_list.lock();
                        if free_list.free_pages > num_free_pages {
                            num_free_pages = free_list.free_pages;
                            cpuid = i_usize;
                        }
                    }
                }

                // now grab a page from that cpu's pool
                let free_list = Arc::clone(&allocator.free_lists[cpuid]);
                let mut free_list = free_list.lock();
                if free_list.free_pages == 0 {
                    pr_info!("ERROR: no more pages\n");
                    Err(ENOSPC)
                } else {
                    // TODO: is using an iterator the fastest way to do this?
                    let iter = free_list.list.iter().next();
                    let page = match iter {
                        None => {
                            pr_info!("ERROR: unable to get free page on CPU {:?}\n", cpu);
                            return Err(ENOSPC);
                        }
                        Some(page) => *page.0,
                    };
                    free_list.list.remove(&page);
                    free_list.free_pages -= 1;
                    Ok(page)
                }
            }
        } else {
            pr_info!("ERROR: page allocator is uninitialized\n");
            Err(EINVAL)
        }
    }

    fn dealloc_data_page<'a>(&self, page: &DataPageWrapper<'a, Clean, Dealloc>) -> Result<()> {
        if let Some(allocator) = self {
            let page_no = page.get_page_no();
            allocator.dealloc_page(page_no)
        } else {
            pr_info!("ERROR: page allocator is uninitialized\n");
            Err(EINVAL)
        }
    }

    fn dealloc_data_page_list(&self, pages: &DataPageListWrapper<Clean, Free>) -> Result<()> {
        if let Some(allocator) = self {
            let mut page_list = pages.get_page_list_cursor();
            let mut page = page_list.current();
            while page.is_some() {
                // janky syntax to deal with the fact that page_list.current() returns an Option
                if let Some(page) = page {
                    // TODO: refactor to avoid acquiring lock on every iteration
                    allocator.dealloc_page(page.get_page_no())?;
                    page_list.move_next();
                } else {
                    unreachable!()
                }
                page = page_list.current();
            }
            Ok(())
        } else {
            pr_info!("ERROR: page allocator is uninitialized\n");
            Err(EINVAL)
        }
    }

    fn dealloc_dir_page<'a>(&self, page: &DirPageWrapper<'a, Clean, Dealloc>) -> Result<()> {
        if let Some(allocator) = self {
            let page_no = page.get_page_no();
            allocator.dealloc_page(page_no)
        } else {
            pr_info!("ERROR: page allocator is uninitialized\n");
            Err(EINVAL)
        }
    }

    fn dealloc_dir_page_list(&self, pages: &DirPageListWrapper<Clean, Free>) -> Result<()> {
        if let Some(allocator) = self {
            let mut page_list = pages.get_page_list_cursor();
            let mut page = page_list.current();
            while page.is_some() {
                if let Some(page) = page {
                    // TODO: refactor to avoid acquiring lock on every iteration
                    allocator.dealloc_page(page.get_page_no())?;
                    page_list.move_next();
                }
                page = page_list.current();
            }
            Ok(())
        } else {
            pr_info!("ERROR: page allocator is uninitialized\n");
            Err(EINVAL)
        }
    }
}

impl PerCpuPageAllocator {
    fn dealloc_page(&self, page_no: PageNum) -> Result<()> {
        // rust division rounds down
        let cpu: usize = ((page_no - self.start) / self.pages_per_cpu).try_into()?;
        let free_list = Arc::clone(&self.free_lists[cpu]);
        let mut free_list = free_list.lock();
        let res = free_list.list.try_insert(page_no, ());
        free_list.free_pages += 1;
        // unwrap the error so we can get at the option
        let res = match res {
            Ok(res) => res,
            Err(e) => {
                pr_info!(
                    "ERROR: failed to insert {:?} into page allocator at CPU {:?}, error {:?}\n",
                    page_no,
                    cpu,
                    e
                );
                return Err(e);
            }
        };
        // check that the page was not already present in the tree
        if res.is_some() {
            pr_info!(
                "ERROR: page {:?} was already in the allocator at CPU {:?}\n",
                page_no,
                cpu
            );
            Err(EINVAL)
        } else {
            Ok(())
        }
    }
}

// placeholder page descriptor that can represent either a dir or data page descriptor
// mainly useful so that we can represent the page descriptor table as a slice and index
// into it. fields cannot be accessed directly - we can only convert this type into a
// narrower page descriptor type
#[derive(Debug)]
#[repr(C)]
pub(crate) struct PageDescriptor {
    page_type: PageType,
    ino: InodeNum,
    offset: u64,
    _padding0: u64,
}

impl PageDescriptor {
    pub(crate) fn is_free(&self) -> bool {
        self.page_type == PageType::NONE && self.ino == 0 && self.offset == 0
    }

    pub(crate) fn get_page_type(&self) -> PageType {
        self.page_type
    }
}

pub(crate) trait PageHeader {
    fn is_initialized(&self) -> bool;
    fn get_ino(&self) -> InodeNum;
    unsafe fn set_backpointer(&mut self, ino: InodeNum);
    unsafe fn alloc<'a>(sbi: &'a SbInfo, offset: Option<u64>) -> Result<(&mut Self, PageNum)>;
    unsafe fn unmap(&mut self);
    unsafe fn dealloc(&mut self, sbi: &SbInfo);
}

#[repr(C)]
#[derive(Debug)]
pub(crate) struct DirPageHeader {
    page_type: PageType,
    ino: InodeNum,
    _padding0: u64,
    _padding1: u64,
}

impl TryFrom<&mut PageDescriptor> for &mut DirPageHeader {
    type Error = Error;

    fn try_from(value: &mut PageDescriptor) -> Result<Self> {
        if value.page_type == PageType::DIR || value.page_type == PageType::NONE {
            Ok(unsafe { &mut *(value as *mut PageDescriptor as *mut DirPageHeader) })
        } else {
            Err(ENOTDIR)
        }
    }
}

impl TryFrom<&PageDescriptor> for &DirPageHeader {
    type Error = Error;

    fn try_from(value: &PageDescriptor) -> Result<Self> {
        if value.page_type == PageType::DIR || value.page_type == PageType::NONE {
            Ok(unsafe { &*(value as *const PageDescriptor as *const DirPageHeader) })
        } else {
            Err(ENOTDIR)
        }
    }
}

// be careful here... slice should have size DENTRIES_PER_PAGE
// i can't figure out how to just make this be an array
#[derive(Debug)]
struct DirPage<'a> {
    dentries: &'a mut [HayleyFsDentry],
}

impl DirPage<'_> {
    pub(crate) fn get_dentry_info_from_dentries(self) -> Result<Vec<DentryInfo>> {
        let mut dentry_vec = Vec::new();
        for d in self.dentries.iter() {
            let ino = d.get_ino();
            if !d.is_free() {
                let name = d.get_name();
                let virt_addr = d as *const HayleyFsDentry as *const ffi::c_void;
                dentry_vec.try_push(DentryInfo::new(ino, Some(virt_addr), name, d.is_dir()))?;
            }
        }
        Ok(dentry_vec)
    }
}

impl PageHeader for DirPageHeader {
    fn is_initialized(&self) -> bool {
        self.page_type != PageType::NONE && self.ino != 0
    }

    fn get_ino(&self) -> InodeNum {
        self.ino
    }

    // Safety: Should only be called on the data page field of a page wrapper type
    /// with <Clean, Alloc> typestate
    unsafe fn set_backpointer(&mut self, ino: InodeNum) {
        self.ino = ino;
    }

    unsafe fn alloc<'a>(sbi: &'a SbInfo, offset: Option<u64>) -> Result<(&mut Self, PageNum)> {
        if offset.is_some() {
            Err(EINVAL)
        } else {
            let page_no = sbi.page_allocator.alloc_page()?;
            let ph = unsafe { unchecked_new_page_no_to_dir_header(sbi, page_no)? };
            ph.page_type = PageType::DIR;
            sbi.inc_blocks_in_use();
            Ok((ph, page_no))
        }
    }

    unsafe fn unmap(&mut self) {
        self.ino = 0;
    }

    unsafe fn dealloc(&mut self, sbi: &SbInfo) {
        self.page_type = PageType::NONE;
        sbi.dec_blocks_in_use();
    }
}

#[allow(dead_code)]
#[derive(Debug)]
pub(crate) struct DirPageWrapper<'a, State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    page_no: PageNum,
    page: CheckedPage<'a, DirPageHeader>,
}

impl<'a, State, Op> PmObjWrapper for DirPageWrapper<'a, State, Op> {}

impl<'a, State, Op> DirPageWrapper<'a, State, Op> {
    pub(crate) fn get_page_no(&self) -> PageNum {
        self.page_no
    }

    // TODO: should this be unsafe?
    // TODO: should this be used somewhere?
    #[allow(dead_code)]
    fn take(&mut self) -> CheckedPage<'a, DirPageHeader> {
        CheckedPage {
            drop_type: self.page.drop_type,
            page: self.page.page.take(),
        }
    }

    fn take_and_make_drop_safe(&mut self) -> CheckedPage<'a, DirPageHeader> {
        let drop_type = self.page.drop_type;
        self.page.drop_type = DropType::Ok;
        CheckedPage {
            drop_type,
            page: self.page.page.take(),
        }
    }
}

impl<'a> DirPageWrapper<'a, Clean, Start> {
    pub(crate) fn from_page_no(sbi: &'a SbInfo, page_no: PageNum) -> Result<Self> {
        let ph = unsafe { page_no_to_dir_header(&sbi, page_no)? };
        if !ph.is_initialized() {
            pr_info!("ERROR: page {:?} is uninitialized\n", page_no);
            Err(EPERM)
        } else {
            Ok(Self {
                state: PhantomData,
                op: PhantomData,
                page_no,
                page: CheckedPage {
                    drop_type: DropType::Ok,
                    page: Some(ph),
                },
            })
        }
    }

    pub(crate) fn from_dentry<State, Op>(
        sbi: &'a SbInfo,
        dentry: &DentryWrapper<'a, State, Op>,
    ) -> Result<Self> {
        // get a u64 representing the offset of this dentry into the device
        let dentry_offset = dentry.get_dentry_offset(sbi);
        // then translate that offset into a page number
        let page_no = dentry_offset / HAYLEYFS_PAGESIZE;
        Self::from_page_no(sbi, page_no)
    }
}

/// Safety: Only safe to call on a newly-allocated, free page no. Used only to convert
/// a newly-allocated page no into a header so the header can be set up, since the regular
/// page_no_to_dir_header function fails if the page is not allocated.
unsafe fn unchecked_new_page_no_to_dir_header<'a>(
    sbi: &'a SbInfo,
    page_no: PageNum,
) -> Result<&'a mut DirPageHeader> {
    let page_desc_table = sbi.get_page_desc_table()?;
    let page_index: usize = (page_no - sbi.get_data_pages_start_page()).try_into()?;
    let ph = page_desc_table.get_mut(page_index);
    match ph {
        Some(ph) => {
            let ph: &mut DirPageHeader = ph.try_into()?;
            Ok(ph)
        }
        None => {
            pr_info!(
                "[unchecked_new_page_no_to_dir_header] No space left in page descriptor table - index {:?} out of bounds\n",
                page_index
            );
            Err(ENOSPC)
        }
    }
}

unsafe fn page_no_to_dir_header<'a>(
    sbi: &'a SbInfo,
    page_no: PageNum,
) -> Result<&'a mut DirPageHeader> {
    let page_desc_table = sbi.get_page_desc_table()?;
    let page_index: usize = (page_no - sbi.get_data_pages_start_page()).try_into()?;
    let ph = page_desc_table.get_mut(page_index);
    match ph {
        Some(ph) => {
            if ph.get_page_type() != PageType::DIR {
                pr_info!(
                    "Page {:?} is not a dir page, has type {:?}\n",
                    page_no,
                    ph.get_page_type()
                );
                Err(EINVAL)
            } else {
                let ph: &mut DirPageHeader = ph.try_into()?;
                Ok(ph)
            }
        }
        None => {
            pr_info!(
                "[page_no_to_dir_header] No space left in page descriptor table - index {:?} out of bounds\n",
                page_index
            );
            Err(ENOSPC)
        }
    }
}

// TODO: safety
unsafe fn page_no_to_page(sbi: &SbInfo, page_no: PageNum) -> Result<*mut u8> {
    if page_no > MAX_PAGES {
        pr_info!(
            "ERROR: page no {:?} is higher than max pages {:?}\n",
            page_no,
            MAX_PAGES
        );
        Err(ENOSPC)
    } else {
        let virt_addr: *mut u8 = sbi.get_virt_addr();
        let res = Ok(unsafe { virt_addr.offset((HAYLEYFS_PAGESIZE * page_no).try_into()?) });
        res
    }
}

// TODO: safety
fn get_offset_of_page_no(sbi: &SbInfo, page_no: PageNum) -> Result<u64> {
    if page_no > MAX_PAGES {
        pr_info!(
            "ERROR: page no {:?} is higher than max pages {:?}\n",
            page_no,
            MAX_PAGES
        );
        Err(ENOSPC)
    } else {
        let page_desc = unsafe { unchecked_new_page_no_to_data_header(sbi, page_no)? };
        if page_desc.page_type == PageType::NONE {
            pr_info!("ERROR: page descriptor is uninitialized\n");
            Err(EINVAL)
        } else {
            Ok(page_desc.get_offset())
        }
    }
}

impl<'a> DirPageWrapper<'a, Dirty, Alloc> {
    /// Allocate a new page and set it to be a directory page.
    /// Does NOT flush the allocated page.
    pub(crate) fn alloc_dir_page(sbi: &'a SbInfo) -> Result<Self> {
        let (page, page_no) = unsafe { DirPageHeader::alloc(sbi, None)? };
        Ok(DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no,
            page: CheckedPage {
                drop_type: DropType::Ok,
                page: Some(page),
            },
        })
    }
}

impl<'a> DirPageWrapper<'a, Clean, Free> {
    pub(crate) fn mark_pages_free(
        mut pages: Vec<DirPageWrapper<'a, Clean, Dealloc>>,
    ) -> Result<Vec<Self>> {
        let mut free_vec = Vec::new();
        for mut page in pages.drain(..) {
            let mut inner = page.take_and_make_drop_safe();
            inner.drop_type = DropType::Ok;
            free_vec.try_push(DirPageWrapper {
                state: PhantomData,
                op: PhantomData,
                page_no: page.page_no,
                page: inner,
            })?;
        }
        Ok(free_vec)
    }
}

impl<'a> DirPageWrapper<'a, Clean, Alloc> {
    pub(crate) fn zero_page(mut self, sbi: &SbInfo) -> Result<DirPageWrapper<'a, Clean, Zeroed>> {
        let page_addr = unsafe { page_no_to_page(sbi, self.page_no)? };
        unsafe {
            memset_nt(
                page_addr as *mut ffi::c_void,
                0,
                HAYLEYFS_PAGESIZE.try_into()?,
                true,
            )
        };
        let page = self.take_and_make_drop_safe();
        Ok(DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        })
    }
}

impl<'a> DirPageWrapper<'a, Clean, Zeroed> {
    /// Requires Initialized inode only as proof that the inode number we are setting points
    /// to an initialized inode
    pub(crate) fn set_dir_page_backpointer<InoState: Initialized>(
        mut self,
        inode: &InodeWrapper<'a, Clean, InoState, DirInode>,
    ) -> DirPageWrapper<'a, Dirty, Init> {
        unsafe { self.page.set_backpointer(inode.get_ino()) };
        let page = self.take_and_make_drop_safe();
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a> DirPageWrapper<'a, Clean, ToUnmap> {
    /// Safety: ph must be a valid DataPageHeader reference
    #[allow(dead_code)]
    unsafe fn wrap_page_to_unmap(ph: &'a mut DirPageHeader, page_no: PageNum) -> Result<Self> {
        if !ph.is_initialized() {
            pr_info!("ERROR: page {:?} is uninitialized\n", page_no);
            Err(EPERM)
        } else {
            Ok(Self {
                state: PhantomData,
                op: PhantomData,
                page_no,
                page: CheckedPage {
                    drop_type: DropType::Panic,
                    page: Some(ph),
                },
            })
        }
    }

    // TODO: this should take a ClearIno dentry or Dealloc inode
    pub(crate) fn mark_to_unmap(sbi: &'a SbInfo, info: &DirPageInfo) -> Result<Self> {
        let page_no = info.get_page_no();
        let ph = unsafe { page_no_to_dir_header(sbi, page_no)? };
        unsafe { Self::wrap_page_to_unmap(ph, page_no) }
    }

    #[allow(dead_code)]
    pub(crate) fn unmap(mut self) -> DirPageWrapper<'a, Dirty, ClearIno> {
        unsafe {
            self.page.unmap();
        }
        let page = self.take_and_make_drop_safe();
        // not ok to drop yet since we want to deallocate all of the
        // pages before dropping them
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a> DirPageWrapper<'a, Clean, ClearIno> {
    /// Returns in Dealloc state, not Free state, because it's still not safe
    /// to drop the pages until they are all persisted
    pub(crate) fn dealloc(mut self, sbi: &SbInfo) -> DirPageWrapper<'a, Dirty, Dealloc> {
        unsafe {
            self.page.dealloc(sbi);
        }
        let page = self.take_and_make_drop_safe();
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a, Op: Initialized> DirPageWrapper<'a, Clean, Op> {
    pub(crate) fn get_alloc_dentry_info(&self, sbi: &SbInfo) -> Result<Vec<DentryInfo>> {
        let dir_page = self.get_dir_page(sbi)?;
        dir_page.get_dentry_info_from_dentries()
    }

    fn get_dir_page(&self, sbi: &SbInfo) -> Result<DirPage<'a>> {
        let page_addr = unsafe { page_no_to_page(sbi, self.get_page_no())? as *mut HayleyFsDentry };
        let dentries = unsafe { slice::from_raw_parts_mut(page_addr, DENTRIES_PER_PAGE) };
        Ok(DirPage { dentries })
    }

    pub(crate) fn has_free_space(&self, sbi: &SbInfo) -> Result<bool> {
        let page = self.get_dir_page(&sbi)?;

        for dentry in page.dentries.iter_mut() {
            if dentry.is_free() {
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// Obtains a wrapped pointer to a free dentry.
    /// This does NOT allocate the dentry - just obtains a pointer to a free dentry
    /// This requires a mutable reference to self because we need to acquire a
    /// mutable reference to a dentry, but it doesn't actually modify the DirPageWrapper
    pub(crate) fn get_free_dentry(self, sbi: &SbInfo) -> Result<DentryWrapper<'a, Clean, Free>> {
        let page = self.get_dir_page(&sbi)?;
        // iterate until we find a free dentry
        // VFS *should* have locked the parent, so there is no possibility of
        // this racing with another operation trying to create in the same directory
        // TODO: confirm that
        // TODO: safety notes based on VFS locking.
        for (_i, dentry) in page.dentries.iter_mut().enumerate() {
            // if any part of a dentry is NOT zeroed out, that dentry is allocated; we need
            // an unallocated dentry
            if dentry.is_free() {
                return Ok(unsafe { DentryWrapper::wrap_free_dentry(dentry) });
            }
        }
        // if we can't find a free dentry in this page, return an error
        pr_info!("could not find a free dentry in this page\n");
        Err(ENOSPC)
    }

    pub(crate) fn is_empty(mut self, sbi: &SbInfo) -> Result<DirPageWrapper<'a, Clean, ToUnmap>> {
        let page = self.get_dir_page(&sbi)?;
        // TODO: store something in the wrapper so we don't have to iterate
        for dentry in page.dentries.iter() {
            if !dentry.is_free() {
                return Err(ENOTEMPTY);
            }
        }
        let page = self.take_and_make_drop_safe();
        Ok(DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        })
    }
}

impl<'a, Op> DirPageWrapper<'a, Dirty, Op> {
    pub(crate) fn flush(mut self) -> DirPageWrapper<'a, InFlight, Op> {
        match &self.page.page {
            Some(page) => hayleyfs_flush_buffer(page, mem::size_of::<DirPageHeader>(), false),
            None => panic!("ERROR: Wrapper does not have a page"),
        };
        let page = self.take_and_make_drop_safe();
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a, Op> DirPageWrapper<'a, InFlight, Op> {
    pub(crate) fn fence(mut self) -> DirPageWrapper<'a, Clean, Op> {
        sfence();
        let page = self.take_and_make_drop_safe();
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }

    /// Safety: this is only safe to use if it is immediately preceded or
    /// followed by an sfence call. The ONLY place it should be used is in the
    /// macros to fence all objects in a vector.
    pub(crate) unsafe fn fence_unsafe(mut self) -> DirPageWrapper<'a, Clean, Op> {
        let page = self.take_and_make_drop_safe();
        DirPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a, State, Op> Drop for DirPageWrapper<'a, State, Op> {
    fn drop(&mut self) {
        match self.page.drop_type {
            DropType::Ok => {}
            DropType::Panic => panic!("ERROR: attempted to drop an undroppable object"),
        };
    }
}

/// represents a typestate-ful section of a directory's pages in no
/// particular order
pub(crate) struct DirPageListWrapper<State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    pages: List<Box<LinkedPage>>,
}

impl<State, Op> PmObjWrapper for DirPageListWrapper<State, Op> {}

impl<State, Op> DirPageListWrapper<State, Op> {
    // pub(crate) fn get_page_list(&self) -> &List<Box<LinkedPage>> {
    //     &self.pages
    // }

    pub(crate) fn get_page_list_cursor(&self) -> Cursor<'_, Box<LinkedPage>> {
        self.pages.cursor_front()
    }
}

impl DirPageListWrapper<Clean, ToUnmap> {
    // TODO: this should require an inode in the proper state
    pub(crate) fn get_dir_pages_to_unmap(pi_info: &HayleyFsDirInodeInfo) -> Result<Self> {
        let pages = pi_info.get_all_pages()?;
        let iter = pages.keys();
        let mut v = List::new();
        for page in iter {
            v.push_back(Box::try_new(LinkedPage::new(page.get_page_no()))?);
        }
        Ok(Self {
            state: PhantomData,
            op: PhantomData,
            pages: v,
        })
    }

    pub(crate) fn unmap(self, sbi: &SbInfo) -> Result<DirPageListWrapper<InFlight, ClearIno>> {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                let ph = unsafe { page_no_to_dir_header(sbi, page.get_page_no())? };
                ph.ino = 0;
                hayleyfs_flush_buffer(ph, mem::size_of::<DirPageHeader>(), false);
            }
            pages.move_next();
            page = pages.current();
        }
        Ok(DirPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            pages: self.pages,
        })
    }
}

impl DirPageListWrapper<Clean, ClearIno> {
    pub(crate) fn dealloc(self, sbi: &SbInfo) -> Result<DirPageListWrapper<InFlight, Dealloc>> {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                let ph = unsafe { page_no_to_dir_header(sbi, page.get_page_no())? };
                unsafe { ph.dealloc(sbi) };
                hayleyfs_flush_buffer(ph, mem::size_of::<DirPageHeader>(), false);
            }
            pages.move_next();
            page = pages.current();
        }
        Ok(DirPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            pages: self.pages,
        })
    }
}

impl DirPageListWrapper<Clean, Dealloc> {
    pub(crate) fn mark_free(self) -> DirPageListWrapper<Clean, Free> {
        DirPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            pages: self.pages,
        }
    }
}

impl<Op> DirPageListWrapper<InFlight, Op> {
    pub(crate) fn fence(self) -> DirPageListWrapper<Clean, Op> {
        sfence();
        DirPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            pages: self.pages,
        }
    }
}

#[allow(dead_code)]
#[repr(C)]
#[derive(Debug)]
pub(crate) struct DataPageHeader {
    page_type: PageType,
    ino: InodeNum,
    offset: u64,
    _padding: u64,
}

impl TryFrom<&mut PageDescriptor> for &mut DataPageHeader {
    type Error = Error;

    fn try_from(value: &mut PageDescriptor) -> Result<Self> {
        if value.page_type == PageType::DATA || value.page_type == PageType::NONE {
            Ok(unsafe { &mut *(value as *mut PageDescriptor as *mut DataPageHeader) })
        } else {
            Err(EISDIR)
        }
    }
}

impl TryFrom<&PageDescriptor> for &DataPageHeader {
    type Error = Error;

    fn try_from(value: &PageDescriptor) -> Result<Self> {
        if value.page_type == PageType::DATA || value.page_type == PageType::NONE {
            Ok(unsafe { &*(value as *const PageDescriptor as *const DataPageHeader) })
        } else {
            Err(EISDIR)
        }
    }
}

/// Given the offset into a file, returns the offset of the
/// DataPageHeader that includes that offset
#[allow(dead_code)]
pub(crate) fn page_offset(offset: u64) -> Result<u64> {
    // integer division removes the remainder; multiplying by HAYLEYFS_PAGESIZE
    // gives us the offset of the page to read/write
    Ok((offset / HAYLEYFS_PAGESIZE) * HAYLEYFS_PAGESIZE)
}

pub(crate) fn get_page_addr(sbi: &SbInfo, page_no: PageNum) -> Result<*mut u8> {
    let page_addr = unsafe { page_no_to_page(sbi, page_no)? };
    Ok(page_addr)
}

impl PageHeader for DataPageHeader {
    fn is_initialized(&self) -> bool {
        self.page_type != PageType::NONE && self.ino != 0
    }

    fn get_ino(&self) -> InodeNum {
        self.ino
    }

    // Safety: Should only be called on the data page field of a page wrapper type
    /// with <Clean, Alloc> typestate
    unsafe fn set_backpointer(&mut self, ino: InodeNum) {
        self.ino = ino;
    }

    /// Allocates a data page by setting its type and offset. Returns its page number.
    /// Safety: Should only be called in the context of a DataPageWrapper allocation
    /// function that returns a <Dirty, Alloc> wrapper.
    unsafe fn alloc<'a>(sbi: &'a SbInfo, offset: Option<u64>) -> Result<(&mut Self, PageNum)> {
        let offset = offset.ok_or(EINVAL)?;
        let page_no = sbi.page_allocator.alloc_page()?;
        let ph = unsafe { unchecked_new_page_no_to_data_header(sbi, page_no)? };
        ph.page_type = PageType::DATA;
        ph.offset = offset.try_into()?;
        sbi.inc_blocks_in_use();
        Ok((ph, page_no))
    }

    /// Safety: Should only be called on the data page field of a DataPage wrapper
    /// with <Clean, ToUnmap> typestate
    unsafe fn unmap(&mut self) {
        self.ino = 0;
    }

    /// Safety: Should only be called on the data page field of a DataPage wrapper
    /// with <Clean, ClearIno> typestate
    unsafe fn dealloc(&mut self, sbi: &SbInfo) {
        self.page_type = PageType::NONE;
        self.offset = 0;
        sbi.dec_blocks_in_use();
    }
}

#[allow(dead_code)]
impl DataPageHeader {
    pub(crate) fn get_offset(&self) -> u64 {
        self.offset
    }
}

/// Safety: ptr must be a valid pointer to a live page obtained from
/// a readable wrapper type
pub(crate) unsafe fn read_from_page(
    writer: &mut impl IoBufferWriter,
    ptr: *mut u8,
    offset: u64,
    len: u64,
) -> Result<u64> {
    let ptr = unsafe { ptr.offset(offset.try_into()?) };
    // FIXME: same problem as write_to_page - write_raw returns an error if
    // the bytes are not all written, which is not what we want.
    unsafe { writer.write_raw(ptr, len.try_into()?) }?;

    Ok(len)
}

/// Safety: ptr must be a valid pointer to a live page obtained
/// from a Writeable wrapper type
pub(crate) unsafe fn write_to_page(
    reader: &mut impl IoBufferReader,
    ptr: *mut u8,
    offset: u64,
    len: u64,
) -> Result<u64> {
    let ptr = unsafe { ptr.offset(offset.try_into()?) };
    // FIXME: read_raw and read_raw_nt return a Result that does NOT include the
    // number of bytes actually read. they return an error if all bytes are not
    // read. this is not the behavior we expect or want here. It does return an
    // error if all bytes are not written so we can safely return len if
    // the read does succeed though
    unsafe { reader.read_raw_nt(ptr, len.try_into()?) }?;
    unsafe { flush_edge_cachelines(ptr as *mut ffi::c_void, len) }?;
    Ok(len)
}

/// Safety: Only safe to call in the context of a DataPage wrapper type that stores the page_no arg
/// TODO: is this really unsafe? could you define it in a different way so that it doesn't have to be?
#[allow(dead_code)]
unsafe fn page_no_to_data_header(sbi: &SbInfo, page_no: PageNum) -> Result<&mut DataPageHeader> {
    let page_desc_table = sbi.get_page_desc_table()?;
    let page_index: usize = (page_no - sbi.get_data_pages_start_page()).try_into()?;
    let ph = page_desc_table.get_mut(page_index);

    match ph {
        Some(ph) => {
            if ph.get_page_type() != PageType::DATA {
                pr_info!("Page {:?} is not a data page\n", page_no);
                Err(EINVAL)
            } else {
                let ph: &mut DataPageHeader = ph.try_into()?;
                Ok(ph)
            }
        }
        None => {
            pr_info!(
                "[page_no_to_data_header] No space left in page descriptor table - index {:?} out of bounds\n",
                page_index
            );
            Err(ENOSPC)
        }
    }
}

/// Safety: Only safe to call on a newly-allocated, free page no. Used only to convert
/// a newly-allocated page no into a header so the header can be set up, since the regular
/// page_no_to_data_header function fails if the page is not allocated.
unsafe fn unchecked_new_page_no_to_data_header(
    sbi: &SbInfo,
    page_no: PageNum,
) -> Result<&mut DataPageHeader> {
    let page_desc_table = sbi.get_page_desc_table()?;
    let page_index: usize = (page_no - sbi.get_data_pages_start_page()).try_into()?;
    let ph = page_desc_table.get_mut(page_index);
    match ph {
        Some(ph) => {
            let ph: &mut DataPageHeader = ph.try_into()?;
            Ok(ph)
        }
        None => {
            pr_info!(
                "[unchecked_new_page_no_to_data_header] No space left in page descriptor table - index {:?} out of bounds\n",
                page_index
            );
            Err(ENOSPC)
        }
    }
}

#[allow(dead_code)]
#[derive(Debug)]
pub(crate) struct StaticDataPageWrapper<'a, State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    page_no: PageNum,
    page: &'a mut DataPageHeader,
}

impl<'a, State, Op> PmObjWrapper for StaticDataPageWrapper<'a, State, Op> {}

// TODO: we may be able to combine some DataPageWrapper methods with DirPageWrapper methods
// by making them implement some shared trait - but need to be careful of dynamic dispatch.
// dynamic dispatch may or may not be safe for us there
#[allow(dead_code)]
impl<'a, State, Op> StaticDataPageWrapper<'a, State, Op> {
    pub(crate) fn get_page_no(&self) -> PageNum {
        self.page_no
    }

    pub(crate) fn get_offset(&self) -> u64 {
        self.page.offset
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        self.page.ino
    }
}

#[allow(dead_code)]
impl<'a> StaticDataPageWrapper<'a, Dirty, Alloc> {
    /// Allocate a new page and set it to be a directory page.
    /// Does NOT flush the allocated page.
    pub(crate) fn alloc_data_page(sbi: &'a SbInfo, offset: u64) -> Result<Self> {
        let (ph, page_no) = unsafe { DataPageHeader::alloc(sbi, Some(offset))? };
        Ok(StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no,
            page: ph,
        })
    }
}

impl<'a> StaticDataPageWrapper<'a, Clean, Writeable> {
    /// This method returns a DataPageWrapper ONLY if the page is initialized
    /// Otherwise it returns an error
    #[allow(dead_code)]
    pub(crate) fn from_page_no(sbi: &'a SbInfo, page_no: PageNum) -> Result<Self> {
        let ph = unsafe { page_no_to_data_header(&sbi, page_no)? };
        if !ph.is_initialized() {
            pr_info!("ERROR: page {:?} is uninitialized\n", page_no);
            Err(EPERM)
        } else {
            Ok(Self {
                state: PhantomData,
                op: PhantomData,
                page_no,
                page: ph,
            })
        }
    }

    #[allow(dead_code)]
    pub(crate) fn read_from_page(
        &self,
        sbi: &SbInfo,
        writer: &mut impl IoBufferWriter,
        offset: u64,
        len: u64,
    ) -> Result<u64> {
        let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
        unsafe { read_from_page(writer, ptr, offset, len) }
    }

    // TODO: define an internal implementation of IoBufferWriter that does not use
    // user-compatible functions to read data
    #[allow(dead_code)]
    pub(crate) fn read_from_page_raw(&self, sbi: &SbInfo, offset: u64, len: u64) -> Result<&[u8]> {
        if len + offset > HAYLEYFS_PAGESIZE {
            Err(ENOSPC)
        } else {
            // TODO: safety notes
            let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
            let ptr = unsafe { ptr.offset(offset.try_into()?) };
            Ok(unsafe { slice::from_raw_parts(ptr, len.try_into()?) })
        }
    }

    #[allow(dead_code)]
    pub(crate) fn write_to_page(
        self,
        sbi: &SbInfo,
        reader: &mut impl IoBufferReader,
        offset: u64,
        len: u64,
    ) -> Result<(u64, StaticDataPageWrapper<'a, InFlight, Written>)> {
        let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
        let len = unsafe { write_to_page(reader, ptr, offset, len)? };
        Ok((
            len,
            StaticDataPageWrapper {
                state: PhantomData,
                op: PhantomData,
                page_no: self.page_no,
                page: self.page,
            },
        ))
    }
}

impl<'a> StaticDataPageWrapper<'a, Clean, Alloc> {
    #[allow(dead_code)]
    pub(crate) fn set_data_page_backpointer<S: StartOrAlloc>(
        self,
        inode: &InodeWrapper<'a, Clean, S, RegInode>,
    ) -> StaticDataPageWrapper<'a, Dirty, Writeable> {
        unsafe { self.page.set_backpointer(inode.get_ino()) };
        StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page: self.page,
        }
    }
}

impl<'a, Op> StaticDataPageWrapper<'a, Dirty, Op> {
    #[allow(dead_code)]
    pub(crate) fn flush(self) -> StaticDataPageWrapper<'a, InFlight, Op> {
        hayleyfs_flush_buffer(self.page, mem::size_of::<DataPageHeader>(), false);
        StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page: self.page,
        }
    }
}

impl<'a, Op> StaticDataPageWrapper<'a, InFlight, Op> {
    #[allow(dead_code)]
    pub(crate) fn fence(self) -> StaticDataPageWrapper<'a, Clean, Op> {
        sfence();
        StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page: self.page,
        }
    }

    /// Safety: this is only safe to use if it is immediately preceded or
    /// followed by an sfence call. The ONLY place it should be used is in the
    /// macros to fence all objects in a vector.
    #[allow(dead_code)]
    pub(crate) unsafe fn fence_unsafe(self) -> StaticDataPageWrapper<'a, Clean, Op> {
        StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page: self.page,
        }
    }
}

impl<'a> StaticDataPageWrapper<'a, Clean, Recovery> {
    // SAFETY: this function is only safe to call on orphaned pages during recovery.
    // this function is missing validity checks because it needs to be useable with invalid
    // inodes after a crash
    pub(crate) unsafe fn get_recovery_page(sbi: &'a SbInfo, page_no: PageNum) -> Result<Self> {
        // use the unchecked variant because the pages may be invalid
        let ph = unsafe { unchecked_new_page_no_to_data_header(sbi, page_no)? };
        Ok(StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: page_no,
            page: ph,
        })
    }

    pub(crate) fn recovery_dealloc(self, sbi: &SbInfo) -> StaticDataPageWrapper<'a, Dirty, Free> {
        unsafe { self.page.dealloc(sbi) };
        StaticDataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page: self.page,
        }
    }
}

#[allow(dead_code)]
#[derive(Debug)]
pub(crate) struct DataPageWrapper<'a, State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    page_no: PageNum,
    page: CheckedPage<'a, DataPageHeader>,
}

#[derive(Debug)]
struct CheckedPage<'a, P: PageHeader> {
    drop_type: DropType,
    page: Option<&'a mut P>,
}

impl<'a, P: PageHeader> CheckedPage<'a, P> {
    unsafe fn set_backpointer(&mut self, ino: InodeNum) {
        match &mut self.page {
            Some(page) => unsafe { page.set_backpointer(ino) },
            None => panic!("ERROR: wrapper does not have a page"),
        }
    }

    unsafe fn unmap(&mut self) {
        match &mut self.page {
            Some(page) => unsafe { page.unmap() },
            None => panic!("ERROR: wrapper does not have a page"),
        }
    }

    unsafe fn dealloc(&mut self, sbi: &SbInfo) {
        match &mut self.page {
            Some(page) => unsafe { page.dealloc(sbi) },
            None => panic!("ERROR: wrapper does not have a page"),
        }
    }
}

impl<'a, State, Op> PmObjWrapper for DataPageWrapper<'a, State, Op> {}

// TODO: we may be able to combine some DataPageWrapper methods with DirPageWrapper methods
// by making them implement some shared trait - but need to be careful of dynamic dispatch.
// dynamic dispatch may or may not be safe for us there
#[allow(dead_code)]
impl<'a, State, Op> DataPageWrapper<'a, State, Op> {
    pub(crate) fn get_page_no(&self) -> PageNum {
        self.page_no
    }

    pub(crate) fn get_offset(&self) -> u64 {
        match &self.page.page {
            Some(page) => page.offset,
            None => panic!("ERROR: wrapper does not have a page"),
        }
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        match &self.page.page {
            Some(page) => page.ino,
            None => panic!("ERROR: wrapper does not have a page"),
        }
    }

    // TODO: should this be unsafe?
    fn take(&mut self) -> CheckedPage<'a, DataPageHeader> {
        CheckedPage {
            drop_type: self.page.drop_type,
            page: self.page.page.take(),
        }
    }

    fn take_and_make_drop_safe(&mut self) -> CheckedPage<'a, DataPageHeader> {
        let drop_type = self.page.drop_type;
        self.page.drop_type = DropType::Ok;
        CheckedPage {
            drop_type,
            page: self.page.page.take(),
        }
    }
}

#[allow(dead_code)]
impl<'a> DataPageWrapper<'a, Dirty, Alloc> {
    /// Allocate a new page and set it to be a directory page.
    /// Does NOT flush the allocated page.
    pub(crate) fn alloc_data_page(sbi: &'a SbInfo, offset: u64) -> Result<Self> {
        let (page, page_no) = unsafe { DataPageHeader::alloc(sbi, Some(offset))? };
        Ok(DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no,
            page: CheckedPage {
                drop_type: DropType::Panic,
                page: Some(page),
            },
        })
    }
}

impl<'a> DataPageWrapper<'a, Clean, Writeable> {
    /// This method returns a DataPageWrapper ONLY if the page is initialized
    /// Otherwise it returns an error
    pub(crate) fn from_page_no(sbi: &'a SbInfo, page_no: PageNum) -> Result<Self> {
        let ph = unsafe { page_no_to_data_header(&sbi, page_no)? };
        if !ph.is_initialized() {
            pr_info!("ERROR: page {:?} is uninitialized\n", page_no);
            Err(EPERM)
        } else {
            Ok(Self {
                state: PhantomData,
                op: PhantomData,
                page_no,
                page: CheckedPage {
                    drop_type: DropType::Ok,
                    page: Some(ph),
                },
            })
        }
    }

    #[allow(dead_code)]
    pub(crate) fn read_from_page(
        &self,
        sbi: &SbInfo,
        writer: &mut impl IoBufferWriter,
        offset: u64,
        len: u64,
    ) -> Result<u64> {
        let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
        unsafe { read_from_page(writer, ptr, offset, len) }
    }

    // TODO: define an internal implementation of IoBufferWriter that does not use
    // user-compatible functions to read data
    pub(crate) fn read_from_page_raw(&self, sbi: &SbInfo, offset: u64, len: u64) -> Result<&[u8]> {
        if len + offset > HAYLEYFS_PAGESIZE {
            Err(ENOSPC)
        } else {
            // TODO: safety notes
            let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
            let ptr = unsafe { ptr.offset(offset.try_into()?) };
            Ok(unsafe { slice::from_raw_parts(ptr, len.try_into()?) })
        }
    }

    #[allow(dead_code)]
    pub(crate) fn write_to_page(
        mut self,
        sbi: &SbInfo,
        reader: &mut impl IoBufferReader,
        offset: u64,
        len: u64,
    ) -> Result<(u64, DataPageWrapper<'a, InFlight, Written>)> {
        let ptr = get_page_addr(sbi, self.page_no)? as *mut u8;
        let len = unsafe { write_to_page(reader, ptr, offset, len)? };
        let page = self.take_and_make_drop_safe();
        Ok((
            len,
            DataPageWrapper {
                state: PhantomData,
                op: PhantomData,
                page_no: self.page_no,
                page,
            },
        ))
    }
}

impl<'a> DataPageWrapper<'a, Clean, ToUnmap> {
    /// Safety: ph must be a valid DataPageHeader reference
    #[allow(dead_code)]
    unsafe fn wrap_page_to_unmap(ph: &'a mut DataPageHeader, page_no: PageNum) -> Result<Self> {
        if !ph.is_initialized() {
            pr_info!("ERROR: page {:?} is uninitialized\n", page_no);
            Err(EPERM)
        } else {
            Ok(Self {
                state: PhantomData,
                op: PhantomData,
                page_no,
                page: CheckedPage {
                    drop_type: DropType::Panic,
                    page: Some(ph),
                },
            })
        }
    }

    // TODO: this should take a ClearIno dentry or Dealloc inode
    #[allow(dead_code)]
    pub(crate) fn mark_to_unmap(sbi: &'a SbInfo, page_no: PageNum) -> Result<Self> {
        let ph = unsafe { page_no_to_data_header(sbi, page_no)? };
        unsafe { Self::wrap_page_to_unmap(ph, page_no) }
    }

    #[allow(dead_code)]
    pub(crate) fn unmap(mut self) -> DataPageWrapper<'a, Dirty, ClearIno> {
        unsafe {
            self.page.unmap();
        }
        let page = self.take_and_make_drop_safe();
        // not ok to drop yet since we want to deallocate all of the
        // pages before dropping them
        // page.drop_type = DropType::Panic; // TODO: is this necessary?
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a> DataPageWrapper<'a, Clean, ClearIno> {
    /// Returns in Dealloc state, not Free state, because it's still not safe
    /// to drop the pages until they are all persisted
    pub(crate) fn dealloc(mut self, sbi: &SbInfo) -> DataPageWrapper<'a, Dirty, Dealloc> {
        unsafe {
            self.page.dealloc(sbi);
        }
        let page = self.take_and_make_drop_safe();
        // page.drop_type = DropType::Panic; // TODO: is this necessary?
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a> DataPageWrapper<'a, Clean, Free> {
    pub(crate) fn mark_pages_free(
        mut pages: Vec<DataPageWrapper<'a, Clean, Dealloc>>,
    ) -> Result<Vec<Self>> {
        let mut free_vec = Vec::new();
        for mut page in pages.drain(..) {
            let mut inner = page.take_and_make_drop_safe();
            inner.drop_type = DropType::Ok;
            free_vec.try_push(DataPageWrapper {
                state: PhantomData,
                op: PhantomData,
                page_no: page.page_no,
                page: inner,
            })?;
        }
        Ok(free_vec)
    }
}

impl<'a> DataPageWrapper<'a, Clean, Alloc> {
    #[allow(dead_code)]
    pub(crate) fn set_data_page_backpointer<S: StartOrAlloc>(
        mut self,
        inode: &InodeWrapper<'a, Clean, S, RegInode>,
    ) -> DataPageWrapper<'a, Dirty, Writeable> {
        unsafe { self.page.set_backpointer(inode.get_ino()) };
        let page = self.take_and_make_drop_safe();
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a, Op> DataPageWrapper<'a, Dirty, Op> {
    pub(crate) fn flush(mut self) -> DataPageWrapper<'a, InFlight, Op> {
        match &self.page.page {
            Some(page) => hayleyfs_flush_buffer(page, mem::size_of::<DataPageHeader>(), false),
            None => panic!("ERROR: Wrapper does not have a page"),
        };

        let page = self.take_and_make_drop_safe();
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a, Op> DataPageWrapper<'a, InFlight, Op> {
    #[allow(dead_code)]
    pub(crate) fn fence(mut self) -> DataPageWrapper<'a, Clean, Op> {
        sfence();
        let page = self.take_and_make_drop_safe();
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }

    /// Safety: this is only safe to use if it is immediately preceded or
    /// followed by an sfence call. The ONLY place it should be used is in the
    /// macros to fence all objects in a vector.
    pub(crate) unsafe fn fence_unsafe(mut self) -> DataPageWrapper<'a, Clean, Op> {
        let page = self.take_and_make_drop_safe();
        DataPageWrapper {
            state: PhantomData,
            op: PhantomData,
            page_no: self.page_no,
            page,
        }
    }
}

impl<'a> DataPageWrapper<'a, Clean, Written> {
    pub(crate) fn make_drop_safe(&mut self) {
        self.page.drop_type = DropType::Ok;
    }
}

impl<'a, State, Op> Drop for DataPageWrapper<'a, State, Op> {
    fn drop(&mut self) {
        match self.page.drop_type {
            DropType::Ok => {}
            DropType::Panic => panic!("ERROR: attempted to drop an undroppable object"),
        };
    }
}

pub(crate) struct LinkedPage {
    page_no: PageNum,
    links: Links<LinkedPage>,
}

impl PartialEq for LinkedPage {
    fn eq(&self, other: &Self) -> bool {
        self.page_no == other.page_no
    }
}

impl Eq for LinkedPage {}

impl Ord for LinkedPage {
    fn cmp(&self, other: &Self) -> Ordering {
        self.page_no.cmp(&other.page_no)
    }
}

impl PartialOrd for LinkedPage {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Deref for LinkedPage {
    type Target = PageNum;
    fn deref(&self) -> &Self::Target {
        &self.page_no
    }
}

impl GetLinks for Box<LinkedPage> {
    type EntryType = LinkedPage;
    fn get_links(data: &Self::EntryType) -> &Links<Self::EntryType> {
        &data.links
    }
}

impl LinkedPage {
    pub(crate) fn new(page_no: PageNum) -> Self {
        Self {
            page_no,
            links: Links::new(),
        }
    }

    pub(crate) fn get_page_no(&self) -> PageNum {
        self.page_no
    }
}

/// Represents a typestate-ful section of a file.
#[allow(dead_code)]
pub(crate) struct DataPageListWrapper<State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    offset: u64, // offset at which the contiguous chunk of pages starts
    // TODO: what structure should we use here? vec has a size limit
    num_pages: u64,
    pages: List<Box<LinkedPage>>,
}

impl<State, Op> PmObjWrapper for DataPageListWrapper<State, Op> {}

impl<State, Op> DataPageListWrapper<State, Op> {
    pub(crate) fn len(&self) -> u64 {
        self.num_pages
    }

    pub(crate) fn get_page_list_cursor(&self) -> Cursor<'_, Box<LinkedPage>> {
        self.pages.cursor_front()
    }

    // returns the number of PHYSICALLY contiguous pages from the initial offset
    // in this list to use when handling mmap faults
    pub(crate) fn num_contiguous_pages_from_start(&self) -> u64 {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        let mut prev_page_no;
        let mut num_pages = 0;
        if let Some(page) = page {
            prev_page_no = page.get_page_no();
            num_pages += 1;
        } else {
            return num_pages;
        }
        pages.move_next();
        page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                if page.get_page_no() > prev_page_no {
                    break;
                } else {
                    num_pages += 1;
                    prev_page_no = page.get_page_no();
                }
            }
            pages.move_next();
            page = pages.current();
        }
        num_pages
    }

    pub(crate) fn first_page_virt_addr(&self) -> Option<*mut ffi::c_void> {
        // get first page's page no
        let pages = self.pages.cursor_front();
        let page = pages.current();
        if let Some(page) = page {
            let page_no = page.get_page_no();
            let addr = page_no * HAYLEYFS_PAGESIZE;
            Some(addr as *mut ffi::c_void)
        } else {
            None
        }
    }

    pub(crate) fn get_offset(&self) -> u64 {
        self.offset
    }
}

impl DataPageListWrapper<Clean, Start> {
    // allocates pages to represent a contiguous chunk of a file (the pages
    // themselves may not be physically contiguous)
    pub(crate) fn allocate_pages<'a>(
        mut self,
        sbi: &'a SbInfo,
        pi_info: &HayleyFsRegInodeInfo,
        no_pages: usize,
        mut offset: u64,
    ) -> Result<DataPageListWrapper<InFlight, Alloc>> {
        for _ in 0..no_pages {
            let (ph, page_no) = unsafe { DataPageHeader::alloc(sbi, Some(offset))? };
            let boxed_page_info = Box::try_new(LinkedPage::new(page_no))?;
            self.pages.push_back(boxed_page_info);
            hayleyfs_flush_buffer(ph, mem::size_of::<DataPageHeader>(), false);
            // TODO: don't do this on every iteration - lot of lock acquisition
            pi_info.insert_page_iterator(offset, page_no)?;
            offset += HAYLEYFS_PAGESIZE;
        }
        let no_pages_u64: u64 = no_pages.try_into()?;
        Ok(DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages + no_pages_u64,
            pages: self.pages,
        })
    }
}

impl DataPageListWrapper<Clean, Alloc> {
    pub(crate) fn set_backpointers<'a>(
        self,
        sbi: &'a SbInfo,
        ino: InodeNum,
    ) -> Result<DataPageListWrapper<InFlight, Writeable>> {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                let ph = unsafe { page_no_to_data_header(sbi, page.get_page_no())? };
                if ph.get_ino() == 0 {
                    unsafe { ph.set_backpointer(ino) };
                    // TODO: only flush the cache line we changed
                    hayleyfs_flush_buffer(ph, mem::size_of::<DataPageHeader>(), false);
                }
            }
            pages.move_next();
            page = pages.current();
        }
        Ok(DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        })
    }
}

impl DataPageListWrapper<Clean, Writeable> {
    /// Obtain a list of pages representing a contiguous section of a file
    /// starting at offset and with size len
    pub(crate) fn get_data_page_list(
        pi_info: &HayleyFsRegInodeInfo,
        len: u64,
        mut offset: u64,
    ) -> Result<core::result::Result<Self, DataPageListWrapper<Clean, Start>>> {
        let mut bytes = 0;
        let mut pages = List::new();
        let mut num_pages = 0;
        let first_page_offset = page_offset(offset)?;
        while bytes < len {
            // get offset of the next page in the file
            let page_offset = page_offset(offset)?;
            // determine if the file actually has the page
            let result = pi_info.find(page_offset);
            let data_page_no = match result {
                Some(data_page_no) => data_page_no,
                None => {
                    // if we reach the end of the file before getting all of the pages we
                    // want, use the error case to indicate that the pages are not writeable
                    return Ok(Err(DataPageListWrapper {
                        state: PhantomData,
                        op: PhantomData,
                        offset: first_page_offset,
                        num_pages,
                        pages,
                    }));
                }
            };

            pages.push_back(Box::try_new(LinkedPage::new(data_page_no))?);
            if offset % HAYLEYFS_PAGESIZE == 0 {
                bytes += HAYLEYFS_PAGESIZE;
                offset = page_offset + HAYLEYFS_PAGESIZE;
            } else {
                let bytes_in_page = HAYLEYFS_PAGESIZE - (offset % HAYLEYFS_PAGESIZE);
                bytes += bytes_in_page;
                offset += bytes_in_page;
            }
            num_pages += 1;
        }
        Ok(Ok(Self {
            state: PhantomData,
            op: PhantomData,
            offset: first_page_offset,
            num_pages,
            pages,
        }))
    }

    // TODO: refactor with write pages. the only difference is whether we use a reader or just memset 0s
    pub(crate) fn zero_pages(
        self,
        sbi: &SbInfo,
        mut len: u64,
        offset: u64,
    ) -> Result<(u64, DataPageListWrapper<InFlight, Zeroed>)> {
        // this is the value of the `offset` field of the page that
        // we want to write to
        let mut page_offset = page_offset(offset)?;
        let mut offset_within_page = offset - page_offset;

        let mut bytes_written = 0;
        let write_size = len;

        let mut page_list = self.get_page_list_cursor();
        let mut page = page_list.current();
        while page.is_some() {
            if let Some(page) = page {
                if bytes_written >= write_size {
                    break;
                }

                let page_no = page.get_page_no();

                // skip over pages at the head of the list that we are not writing to
                // this should pretty much never happen so it won't hurt us performance-wise
                if get_offset_of_page_no(sbi, page_no)? < page_offset {
                    page_list.move_next();
                } else {
                    // TODO: safe wrapper
                    let ptr = unsafe { page_no_to_page(sbi, page_no)? };
                    let bytes_to_write = if len < HAYLEYFS_PAGESIZE - offset_within_page {
                        len
                    } else {
                        HAYLEYFS_PAGESIZE - offset_within_page
                    };
                    // let bytes_to_write =
                    //     unsafe { write_to_page(reader, ptr, offset_within_page, bytes_to_write)? };
                    unsafe {
                        memset_nt(
                            (ptr as *mut u8).offset(offset_within_page.try_into()?)
                                as *mut ffi::c_void,
                            0,
                            bytes_to_write.try_into()?,
                            false,
                        );
                    }

                    bytes_written += bytes_to_write;
                    page_offset += HAYLEYFS_PAGESIZE;
                    len -= bytes_to_write;
                    offset_within_page = 0;
                    page_list.move_next();
                }
            }
            page = page_list.current();
        }

        Ok((
            bytes_written,
            DataPageListWrapper {
                state: PhantomData,
                op: PhantomData,
                offset: self.offset,
                num_pages: self.num_pages,
                pages: self.pages,
            },
        ))
    }

    // persistence typestate breaks down a little here, since someone could have
    // (and likely has) written to the msync'ed pages and left them in a dirty
    // and/or inflight state. we can't represent individual page typestates and
    // the pages are originally obtained as Clean. since we're going to flush
    // and fence them all anyway the initial persistence typestate is not
    // important.
    // TODO: this is done in a kind of unsafe way with direct flush and fence. make it safer
    pub(crate) fn msync_pages(self, sbi: &SbInfo) -> Result<DataPageListWrapper<Clean, Msynced>> {
        // go through the list, obtain each page, flush its cache lines
        let mut page_list = self.get_page_list_cursor();
        let mut page = page_list.current();
        while page.is_some() {
            if let Some(page) = page {
                let page_no = page.get_page_no();
                let ptr = unsafe { page_no_to_page(sbi, page_no)? };
                hayleyfs_flush_buffer(ptr, HAYLEYFS_PAGESIZE.try_into()?, false);
            } else {
                unreachable!();
            }
            page_list.move_next();
            page = page_list.current();
        }
        // fence at the end
        sfence();
        Ok(DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        })
    }
}

impl<S: CanWrite> DataPageListWrapper<Clean, S> {
    pub(crate) fn write_pages(
        self,
        sbi: &SbInfo,
        reader: &mut impl IoBufferReader,
        mut len: u64,
        offset: u64, // the raw offset provided by the user
    ) -> Result<(u64, DataPageListWrapper<InFlight, Written>)> {
        // this is the value of the `offset` field of the page that
        // we want to write to
        let mut page_offset = page_offset(offset)?;
        let mut offset_within_page = offset - page_offset;

        let mut bytes_written = 0;
        let write_size = len;

        let mut page_list = self.get_page_list_cursor();
        let mut page = page_list.current();
        while page.is_some() {
            if let Some(page) = page {
                if bytes_written >= write_size {
                    break;
                }

                let page_no = page.get_page_no();

                // skip over pages at the head of the list that we are not writing to
                // this should pretty much never happen so it won't hurt us performance-wise
                if get_offset_of_page_no(sbi, page_no)? < page_offset {
                    page_list.move_next();
                } else {
                    // TODO: safe wrapper
                    let ptr = unsafe { page_no_to_page(sbi, page_no)? };
                    let bytes_to_write = if len < HAYLEYFS_PAGESIZE - offset_within_page {
                        len
                    } else {
                        HAYLEYFS_PAGESIZE - offset_within_page
                    };
                    let bytes_to_write =
                        unsafe { write_to_page(reader, ptr, offset_within_page, bytes_to_write)? };
                    bytes_written += bytes_to_write;
                    page_offset += HAYLEYFS_PAGESIZE;
                    len -= bytes_to_write;
                    offset_within_page = 0;
                    page_list.move_next();
                }
            }
            page = page_list.current();
        }

        Ok((
            bytes_written,
            DataPageListWrapper {
                state: PhantomData,
                op: PhantomData,
                offset: self.offset,
                num_pages: self.num_pages,
                pages: self.pages,
            },
        ))
    }
}

impl DataPageListWrapper<Clean, ToUnmap> {
    pub(crate) fn get_data_pages_to_truncate<'a>(
        pi: &InodeWrapper<'a, Clean, DecSize, RegInode>,
        mut offset: u64,
        len: u64,
    ) -> Result<Self> {
        // basically the same as get_data_page_list, except that we need to SKIP the
        // first page in the range unless the offset to start at is page-aligned
        // TODO: better error handling if we try to look up a page that isn't there

        let pi_info = pi.get_inode_info()?;
        let mut bytes = 0;
        let mut pages = List::new();
        let mut num_pages = 0;
        let first_page_offset = page_offset(offset)?;

        if first_page_offset % HAYLEYFS_PAGESIZE == 0 {
            let result = pi_info.find(first_page_offset);
            let data_page_no = match result {
                Some(data_page_no) => data_page_no,
                None => {
                    return Err(EINVAL);
                }
            };
            pages.push_back(Box::try_new(LinkedPage::new(data_page_no))?);
            offset = first_page_offset + HAYLEYFS_PAGESIZE;
            bytes += HAYLEYFS_PAGESIZE;
            num_pages += 1;
        } else {
            let bytes_at_end = HAYLEYFS_PAGESIZE - (first_page_offset % HAYLEYFS_PAGESIZE);
            offset += bytes_at_end;
            bytes += bytes_at_end;
        }

        while bytes < len {
            // get offset of the next page in the file
            let page_offset = page_offset(offset)?;
            // determine if the file actually has the page
            let result = pi_info.find(page_offset);
            let data_page_no = match result {
                Some(data_page_no) => data_page_no,
                None => {
                    return Err(EINVAL);
                }
            };
            pages.push_back(Box::try_new(LinkedPage::new(data_page_no))?);
            offset += HAYLEYFS_PAGESIZE;
            bytes += HAYLEYFS_PAGESIZE;
            num_pages += 1
        }

        Ok(Self {
            state: PhantomData,
            op: PhantomData,
            offset: first_page_offset,
            num_pages,
            pages,
        })
    }

    // inode try_complete_unlink_runtime can return a list of pages in the ToUnmap state
    // probably just need a different version that returns the wrapper
    // instead of straight vector of wrapped stateful pages
    // TODO: guard this better
    pub(crate) fn get_data_pages_to_unmap(pi_info: &HayleyFsRegInodeInfo) -> Result<Self> {
        let pages = pi_info.get_all_pages()?;
        let mut new_page_list = List::new();
        for page in pages.values() {
            new_page_list.push_back(Box::try_new(LinkedPage::new(*page))?);
        }
        Ok(Self {
            state: PhantomData,
            op: PhantomData,
            offset: 0,
            num_pages: pi_info.get_num_pages(),
            pages: new_page_list,
        })
    }

    pub(crate) fn unmap(self, sbi: &SbInfo) -> Result<DataPageListWrapper<InFlight, ClearIno>> {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                let ph = unsafe { page_no_to_data_header(sbi, page.get_page_no())? };
                ph.ino = 0;
                hayleyfs_flush_buffer(ph, mem::size_of::<DataPageHeader>(), false);
            }
            pages.move_next();
            page = pages.current();
        }
        Ok(DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        })
    }
}

impl DataPageListWrapper<Clean, ClearIno> {
    pub(crate) fn dealloc(self, sbi: &SbInfo) -> Result<DataPageListWrapper<InFlight, Dealloc>> {
        let mut pages = self.pages.cursor_front();
        let mut page = pages.current();
        while page.is_some() {
            if let Some(page) = page {
                let ph = unsafe { page_no_to_data_header(sbi, page.get_page_no())? };
                unsafe { ph.dealloc(sbi) };
                hayleyfs_flush_buffer(ph, mem::size_of::<DataPageHeader>(), false);
            }
            pages.move_next();
            page = pages.current();
        }
        Ok(DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        })
    }
}

impl DataPageListWrapper<Clean, Dealloc> {
    pub(crate) fn mark_free(self) -> DataPageListWrapper<Clean, Free> {
        DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        }
    }
}

impl<Op> DataPageListWrapper<InFlight, Op> {
    pub(crate) fn fence(self) -> DataPageListWrapper<Clean, Op> {
        sfence();
        DataPageListWrapper {
            state: PhantomData,
            op: PhantomData,
            offset: self.offset,
            num_pages: self.num_pages,
            pages: self.pages,
        }
    }
}
