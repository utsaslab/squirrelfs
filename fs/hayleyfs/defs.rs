use crate::balloc::*;
use crate::h_inode::*;
use crate::typestate::*;
use crate::volatile::*;
use core::{
    ptr, slice,
    sync::atomic::{AtomicI64, AtomicU64, Ordering},
};
use kernel::bindings;
use kernel::prelude::*;

// TODO: different magic value
pub(crate) const SUPER_MAGIC: i64 = 0xabcdef;

/// Reserved inodes
pub(crate) const ROOT_INO: InodeNum = 1;

/// Type definitions
pub(crate) type InodeNum = u64;
pub(crate) type PageNum = u64;

pub(crate) const HAYLEYFS_PAGESIZE: u64 = 4096;

pub(crate) const MAX_FILENAME_LEN: usize = 110;
pub(crate) const MAX_PAGES: u64 = u64::MAX;
pub(crate) const MAX_LINKS: u16 = u16::MAX;
pub(crate) const DENTRIES_PER_PAGE: usize = 32;

/// Reserved pages
#[allow(dead_code)]
pub(crate) const SB_PAGE: PageNum = 0;
#[allow(dead_code)]

/// Sizes of persistent objects
/// Update these if they get bigger or are permanently smaller
pub(crate) const INODE_SIZE: u64 = 96;
pub(crate) const PAGE_DESCRIPTOR_SIZE: u64 = 32; // TODO: can we reduce this?
pub(crate) const SB_SIZE: u64 = HAYLEYFS_PAGESIZE;

#[repr(C)]
#[allow(dead_code)]
#[derive(PartialEq, Copy, Clone, Debug)]
pub(crate) enum PageType {
    NONE = 0,
    DIR,
    DATA,
}

#[repr(C)]
#[allow(dead_code)]
#[derive(PartialEq, Debug, Copy, Clone)]
pub(crate) enum InodeType {
    NONE = 0,
    REG,
    DIR,
    SYMLINK,
}

#[derive(Copy, Clone, Debug)]
pub(crate) enum DropType {
    Ok,
    Panic,
}

pub(crate) trait PmObjWrapper {}

/// Persistent super block
#[repr(C)]
pub(crate) struct HayleyFsSuperBlock {
    magic: i64,
    block_size: u64,
    size: i64,
    clean_unmount: bool,
    // TODO: mount and write timestamps
    // TODO: make sure remounted file systems use the page size specified in the superblock
}

impl HayleyFsSuperBlock {
    pub(crate) unsafe fn init_super_block(
        virt_addr: *mut u8,
        size: i64,
    ) -> &'static HayleyFsSuperBlock {
        // we already zeroed out the entire device, so no need to zero out the superblock
        let super_block = unsafe { &mut *(virt_addr as *mut HayleyFsSuperBlock) };
        super_block.size = size;
        super_block.magic = SUPER_MAGIC;
        super_block.block_size = HAYLEYFS_PAGESIZE;
        super_block.clean_unmount = false;
        super_block
    }

    pub(crate) fn get_size(&self) -> i64 {
        self.size
    }

    pub(crate) fn set_clean_unmount(&mut self, clean_unmount: bool) {
        self.clean_unmount = clean_unmount;
    }

    #[allow(dead_code)]
    pub(crate) fn get_clean_unmount(&self) -> bool {
        self.clean_unmount
    }
}

#[derive(Debug)]
pub(crate) enum WriteType {
    SinglePage = 0,
    RuntimeCheck,
    Iterator,
}

#[derive(Default, Debug)]
pub(crate) struct HayleyfsParams {
    pub(crate) init: Option<bool>,
    pub(crate) write_type: Option<WriteType>,
}

/// A volatile structure containing information about the file system superblock.
///
/// It uses typestates to ensure callers use the right sequence of calls.
///
/// # Invariants
/// `dax_dev` is the only active pointer to the dax device in use.
#[repr(C)]
pub(crate) struct SbInfo {
    // FIXME: these should really not be public, but we need to define SbInfo
    // in defs.rs so that it's accessible in other files, but SbInfo's
    // impl depends on HayleyFs definition and fs::Type impl, which logically
    // needs to live in super.rs. These fields aren't available for methods impled
    // in super.rs. Maybe could do something smart with modules or traits to fix this?
    // make get_pm_info (the problematic function right now) part of a trait that SbInfo
    // implements in super.rs? That would probably be a good idea regardless because
    // CXL PM might be obtained in a different way.
    sb: *mut bindings::super_block,
    dax_dev: *mut bindings::dax_device,
    virt_addr: *mut u8,
    pub(crate) size: i64,

    pub(crate) uid: bindings::kuid_t, // uid for root dir
    pub(crate) gid: bindings::kgid_t, // gid for root dir

    pub(crate) blocksize: u64,
    pub(crate) num_blocks: u64,
    pub(crate) mode: u16,

    pub(crate) inodes_in_use: AtomicU64,
    pub(crate) blocks_in_use: AtomicU64,

    pub(crate) cpus: u32,

    // volatile index structures
    // we use per-inode structures stored in the VFS inodes for those lookups.
    // however, we can't fill in those structures until a corresponding file
    // is actually accessed. we don't want to do a full disk scan to determine
    // which pages belong to which file when looking up an inode, so instead,
    // we'll scan at mount and store that information here until we perform
    // a lookup.
    pub(crate) ino_data_page_tree: InoDataPageTree,
    pub(crate) ino_dir_page_tree: InoDirPageTree,
    pub(crate) ino_dentry_tree: InoDentryTree,

    // volatile allocators
    // again, these should really be trait objects, but the system won't compile
    // if they are.
    // TODO: fix this.
    // optional because we can't set it up until we know how big the fs is
    pub(crate) page_allocator: Option<PerCpuPageAllocator>,
    pub(crate) inode_allocator: Option<RBInodeAllocator>,

    pub(crate) inodes_to_free: InodeToFreeList,

    pub(crate) mount_opts: HayleyfsParams,

    pub(crate) num_inodes: u64,
    pub(crate) inode_table_size: u64,
    pub(crate) inode_table_pages: u64,
    pub(crate) num_pages: u64,
    pub(crate) page_desc_table_size: u64,
    pub(crate) page_desc_table_pages: u64,
}

// SbInfo must be Send and Sync for it to be used as the Context's data.
// However, raw pointers are not Send or Sync because they are not safe to
// access across threads. This is a lint - they aren't safe to access within a
// single thread either - and we know that the raw pointer will be immutable,
// so it's ok to mark it Send + Sync here
unsafe impl Send for SbInfo {}
unsafe impl Sync for SbInfo {}

impl SbInfo {
    // TODO: the constructor should either not leave a bunch of pointers NULL
    // or it should make it clear in its name that that is what it does
    pub(crate) fn new() -> Self {
        SbInfo {
            sb: ptr::null_mut(),
            dax_dev: ptr::null_mut(),
            virt_addr: ptr::null_mut(),
            size: 0, // total size of the PM device
            uid: unsafe { bindings::current_fsuid() },
            gid: unsafe { bindings::current_fsgid() },
            mode: 0o755,
            blocksize: HAYLEYFS_PAGESIZE.try_into().unwrap(),
            num_blocks: 0,
            inodes_in_use: AtomicU64::new(1),
            blocks_in_use: AtomicU64::new(0), // TODO: mark reserved pages as in use
            cpus: unsafe { bindings::num_online_cpus() },
            ino_data_page_tree: InoDataPageTree::new().unwrap(),
            ino_dir_page_tree: InoDirPageTree::new().unwrap(),
            ino_dentry_tree: InoDentryTree::new().unwrap(),
            page_allocator: None,
            inode_allocator: None,
            inodes_to_free: InodeToFreeList::new().unwrap(),
            // inode_allocator: InodeAllocator::new(ROOT_INO + 1).unwrap(),
            mount_opts: HayleyfsParams::default(),
            num_inodes: 0,
            inode_table_size: 0,
            inode_table_pages: 0,
            num_pages: 0,
            page_desc_table_size: 0,
            page_desc_table_pages: 0,
        }
    }

    pub(crate) fn get_inode_table_start_page(&self) -> PageNum {
        1
    }

    pub(crate) fn get_page_desc_table_start_page(&self) -> PageNum {
        self.get_inode_table_start_page() + self.inode_table_pages
    }

    pub(crate) fn get_data_pages_start_page(&self) -> PageNum {
        self.get_page_desc_table_start_page() + self.page_desc_table_pages
    }

    // TODO: do these really need to be SeqCst?
    pub(crate) fn inc_inodes_in_use(&self) {
        self.inodes_in_use.fetch_add(1, Ordering::SeqCst);
    }

    pub(crate) fn get_inodes_in_use(&self) -> u64 {
        self.inodes_in_use.load(Ordering::SeqCst)
    }

    pub(crate) fn dec_inodes_in_use(&self) {
        self.inodes_in_use.fetch_sub(1, Ordering::SeqCst);
    }

    pub(crate) fn inc_blocks_in_use(&self) {
        self.blocks_in_use.fetch_add(1, Ordering::SeqCst);
    }

    pub(crate) fn dec_blocks_in_use(&self) {
        self.blocks_in_use.fetch_sub(1, Ordering::SeqCst);
    }

    pub(crate) fn get_blocks_in_use(&self) -> u64 {
        self.blocks_in_use.load(Ordering::SeqCst)
    }

    pub(crate) fn get_size(&self) -> i64 {
        self.size
    }

    /// obtaining the virtual address is safe - dereferencing it is not
    pub(crate) fn get_virt_addr(&self) -> *mut u8 {
        self.virt_addr
    }

    pub(crate) unsafe fn set_virt_addr(&mut self, virt_addr: *mut u8) {
        self.virt_addr = virt_addr;
    }

    pub(crate) unsafe fn set_dax_dev(&mut self, dax_dev: *mut bindings::dax_device) {
        self.dax_dev = dax_dev;
    }

    pub(crate) fn get_dax_dev(&mut self) -> *mut bindings::dax_device {
        self.dax_dev
    }

    pub(crate) fn get_super_block(&mut self) -> Result<&HayleyFsSuperBlock> {
        let super_block = unsafe { &*(self.virt_addr as *mut HayleyFsSuperBlock) };
        // assume for now that if the magic is fine, the rest of the super block is fine
        if super_block.magic != SUPER_MAGIC {
            pr_err!(
                "Magic should be {:?} but found {:?}\n",
                SUPER_MAGIC,
                super_block.magic
            );
            return Err(EINVAL);
        }
        if super_block.size != self.size {
            pr_err!(
                "Device size should be {:?} but found {:?}\n",
                self.size,
                super_block.size
            );
            return Err(EINVAL);
        }
        self.blocksize = super_block.block_size;
        Ok(super_block)
    }

    pub(crate) fn get_super_block_mut(&self) -> Result<&mut HayleyFsSuperBlock> {
        let super_block = unsafe { &mut *(self.virt_addr as *mut HayleyFsSuperBlock) };
        // assume for now that if the magic is fine, the rest of the super block is fine
        if super_block.magic != SUPER_MAGIC {
            pr_err!(
                "Magic should be {:?} but found {:?}\n",
                SUPER_MAGIC,
                super_block.magic
            );
            return Err(EINVAL);
        }
        if super_block.size != self.size {
            pr_err!(
                "Device size should be {:?} but found {:?}\n",
                self.size,
                super_block.size
            );
            return Err(EINVAL);
        }
        Ok(super_block)
    }

    // TODO: why is this safe?
    pub(crate) fn get_page_desc_table<'a>(&self) -> Result<&'a mut [PageDescriptor]> {
        let page_desc_table_addr = unsafe {
            self.virt_addr
                .offset((HAYLEYFS_PAGESIZE * self.get_page_desc_table_start_page()).try_into()?)
        } as *mut PageDescriptor;
        let table =
            unsafe { slice::from_raw_parts_mut(page_desc_table_addr, self.num_pages.try_into()?) };
        Ok(table)
    }

    pub(crate) fn get_inode_table<'a>(&self) -> Result<&'a mut [HayleyFsInode]> {
        let inode_table_addr: *mut HayleyFsInode = unsafe {
            self.virt_addr
                .offset((HAYLEYFS_PAGESIZE * self.get_inode_table_start_page()).try_into()?)
                as *mut HayleyFsInode
        };
        let table =
            unsafe { slice::from_raw_parts_mut(inode_table_addr, self.num_inodes.try_into()?) };
        Ok(table)
    }

    pub(crate) fn get_inode_by_ino<'a>(&self, ino: InodeNum) -> Result<&'a HayleyFsInode> {
        // we don't use inode 0
        if ino >= self.num_inodes || ino == 0 {
            return Err(EINVAL);
        }

        let table = self.get_inode_table()?;
        let ino_usize: usize = ino.try_into()?;
        let inode = &table[ino_usize];
        Ok(inode)
    }

    pub(crate) unsafe fn get_inode_by_ino_mut<'a>(
        &self,
        ino: InodeNum,
    ) -> Result<&'a mut HayleyFsInode> {
        // we don't use inode 0
        if ino >= self.num_inodes || ino == 0 {
            return Err(EINVAL);
        }

        let table = self.get_inode_table()?;
        let ino_usize: usize = ino.try_into()?;
        let inode = &mut table[ino_usize];
        Ok(inode)
    }

    pub(crate) fn check_inode_type_by_vfs_inode(
        &self,
        inode: *mut bindings::inode,
    ) -> Result<InodeType> {
        let pi = unsafe { self.get_inode_by_ino((*inode).i_ino)? };
        Ok(pi.get_type())
    }

    pub(crate) fn check_inode_type_by_inode_num(&self, ino: InodeNum) -> Result<InodeType> {
        let pi = self.get_inode_by_ino(ino)?;
        Ok(pi.get_type())
    }

    // TODO: this should be in h_inode.rs
    pub(crate) fn get_init_reg_inode_by_vfs_inode<'a>(
        &self,
        inode: *mut bindings::inode,
    ) -> Result<InodeWrapper<'a, Clean, Start, RegInode>> {
        // TODO: use &fs::INode to avoid unsafely dereferencing the inode here
        let ino = unsafe { (*inode).i_ino };
        // we don't use inode 0
        if ino >= self.num_inodes || ino == 0 {
            return Err(EINVAL);
        }

        let pi = unsafe { self.get_inode_by_ino_mut(ino)? };
        if pi.get_type() != InodeType::REG && pi.get_type() != InodeType::SYMLINK {
            pr_info!("ERROR: inode {:?} is not a regular inode\n", ino);
            pr_info!("{:?}\n", pi);
            return Err(EPERM);
        }
        if pi.is_initialized() {
            Ok(InodeWrapper::wrap_inode(inode, pi))
        } else {
            pr_info!(
                "ERROR: inode {:?} is not initialized in get_init_reg\n",
                ino
            );
            Err(EPERM)
        }
    }

    pub(crate) fn get_init_dir_inode_by_vfs_inode<'a>(
        &self,
        inode: *mut bindings::inode,
    ) -> Result<InodeWrapper<'a, Clean, Start, DirInode>> {
        // TODO: use &fs::INode to avoid unsafely dereferencing the inode here
        let ino = unsafe { (*inode).i_ino };
        if self.inodes_to_free.find(ino) {
            return Err(ENOENT);
        }
        // we don't use inode 0
        if ino >= self.num_inodes || ino == 0 {
            return Err(EINVAL);
        }
        let pi = unsafe { self.get_inode_by_ino_mut(ino)? };
        if pi.get_type() != InodeType::DIR {
            pr_info!("ERROR: inode {:?} is not a directory\n", ino);
            pr_info!("pi: {:?}\n", pi);
            return Err(EPERM);
        }
        if pi.is_initialized() {
            Ok(InodeWrapper::wrap_inode(inode, pi))
        } else {
            pr_info!(
                "ERROR: inode {:?} is not initialized in get_init_dir\n",
                ino
            );
            Err(EPERM)
        }
    }

    pub(crate) fn alloc_ino(&self) -> Result<InodeNum> {
        match &self.inode_allocator {
            Some(inode_allocator) => inode_allocator.alloc_ino(&self),
            None => {
                pr_info!("ERROR: inode allocator does not exist");
                Err(EPERM)
            }
        }
    }

    pub(crate) fn dealloc_ino(&self, ino: InodeNum) -> Result<()> {
        match &self.inode_allocator {
            Some(inode_allocator) => inode_allocator.dealloc_ino(ino, &self),
            None => {
                pr_info!("ERROR: inode allocator does not exist");
                Err(EPERM)
            }
        }
    }
}

pub(crate) fn cstr_to_filename_array(cstr: &CStr) -> [u8; MAX_FILENAME_LEN] {
    let mut full_filename = [0; MAX_FILENAME_LEN];
    full_filename[..cstr.len()].copy_from_slice(cstr.as_bytes());
    full_filename
}

pub(crate) fn get_cpuid(cpus: &u32) -> u32 {
    (unsafe { bindings::smp_processor_id() }) % cpus
}

// Timing and stats-related functions and macros

/// Initialize a timespec for a timing measurement
#[macro_export]
macro_rules! init_timing {
    ($t:ident) => {
        let mut $t = bindings::timespec64 {
            tv_sec: 0,
            tv_nsec: 0,
        };
    };
}

/// Record start time for an operation
#[macro_export]
macro_rules! start_timing {
    ($t:ident) => {
        if TIMING {
            #[allow(unused_unsafe)]
            unsafe {
                bindings::ktime_get_real_ts64(&mut $t as *mut bindings::timespec64)
            };
        }
    };
}

/// Finish recording an operation and record its elapsed wall-clock time
#[macro_export]
macro_rules! end_timing {
    ($name:ident, $start:ident) => {
        #[allow(unused_unsafe)]
        if TIMING {
            unsafe {
                let mut end = bindings::timespec64 {
                    tv_sec: 0,
                    tv_nsec: 0,
                };
                bindings::ktime_get_real_ts64(&mut end as *mut bindings::timespec64);
                TIMING_STATS[TimingCategory::$name as usize].fetch_add(
                    ((end.tv_sec - $start.tv_sec) * 1000000000) + end.tv_nsec - $start.tv_nsec,
                    Ordering::Relaxed,
                );
                COUNT_STATS[TimingCategory::$name as usize].fetch_add(1, Ordering::Relaxed);
            }
        }
    };
}

pub(crate) fn print_timing_stats() {
    use TimingCategory::*;
    pr_info!("op,avg latency (ns), ops recorded\n");
    for i in 0..TimingNum as usize {
        unsafe {
            let count = COUNT_STATS[i].load(Ordering::Relaxed);
            if count > 0 {
                let time: u64 = TIMING_STATS[i].load(Ordering::Relaxed).try_into().unwrap();
                pr_info!(
                    "{:?},{:?},{:?}\n",
                    match_timing_category(i),
                    time / count,
                    count
                );
            } else {
                pr_info!("{:?},0,0\n", match_timing_category(i));
            }
        }
    }
}

pub(crate) fn clear_timing_stats() {
    use TimingCategory::*;
    for i in 0..TimingNum as usize {
        unsafe {
            COUNT_STATS[i].store(0, Ordering::SeqCst);
            TIMING_STATS[i].store(0, Ordering::SeqCst);
        }
    }
}

// TODO: is there a nicer way to do this without an external crate?
#[allow(dead_code)]
pub(crate) fn match_timing_category(val: usize) -> TimingCategory {
    use TimingCategory::*;
    match val {
        0 => LookupDataPage,
        1 => ReadDataPage,
        2 => ReadLoop,
        3 => FullRead,
        4 => ReadInodeLookup,
        5 => IgetInodeExists,
        6 => FullIget,
        7 => InitRegInode,
        8 => InitDirInode,
        9 => FullVfsInode,
        10 => InitRegVfsInode,
        11 => InitDirVfsInode,
        12 => FullWrite,
        13 => WriteInodeLookup,
        14 => WriteLookupPage,
        15 => WriteAllocPage,
        16 => WriteToPage,
        17 => UnlinkFullDecLink,
        18 => UnlinkFullDelete,
        19 => UnlinkLookup,
        20 => DecLinkCount,
        21 => DeallocPages,
        22 => EvictInodeFull,
        23 => EvictDirInodePages,
        24 => EvictRegInodePages,

        25 => TimingNum,
        _ => panic!("Unrecognized timing category {:?}", val),
    }
}

#[allow(dead_code)]
#[derive(Debug)]
pub(crate) enum TimingCategory {
    LookupDataPage = 0,
    ReadDataPage,
    ReadLoop,
    FullRead,
    ReadInodeLookup,
    IgetInodeExists,
    FullIget,
    InitRegInode,
    InitDirInode,
    FullVfsInode,
    InitRegVfsInode,
    InitDirVfsInode,
    FullWrite,
    WriteInodeLookup,
    WriteLookupPage,
    WriteAllocPage,
    WriteToPage,

    UnlinkFullDecLink,
    UnlinkFullDelete,
    UnlinkLookup,
    DecLinkCount,
    DeallocPages,
    EvictInodeFull,
    EvictDirInodePages,
    EvictRegInodePages,

    TimingNum,
}

const ATOMIC_ZERO_I64: AtomicI64 = AtomicI64::new(0);
const ATOMIC_ZERO_U64: AtomicU64 = AtomicU64::new(0);
pub(crate) static mut TIMING_STATS: [AtomicI64; TimingCategory::TimingNum as usize] =
    [ATOMIC_ZERO_I64; TimingCategory::TimingNum as usize];
pub(crate) static mut COUNT_STATS: [AtomicU64; TimingCategory::TimingNum as usize] =
    [ATOMIC_ZERO_U64; TimingCategory::TimingNum as usize];
pub(crate) const TIMING: bool = false; // TODO: compiler directive
