use crate::balloc::*;
use crate::defs::*;
use crate::pm::*;
use crate::h_dir::*;
use crate::typestate::*;
use crate::volatile::*;
use core::{
    marker::PhantomData,
    mem,
    ops::Deref,
};
use kernel::prelude::*;
use kernel::{bindings, linked_list::{List, Links, GetLinks}, ForeignOwnable, fs, sync::{Arc, smutex::Mutex}, rbtree::RBTree};

// ZSTs for representing inode types
// These are not typestate since they don't change, but they are a generic
// parameter for inodes so that the compiler can check that we are using
// the right kind of inode
#[derive(Debug)]
pub(crate) struct RegInode {}
#[derive(Debug)]
pub(crate) struct DirInode {}

pub(crate) trait AnyInode {}
impl AnyInode for RegInode {}
impl AnyInode for DirInode {}

/// Persistent inode structure
/// It is always unsafe to access this structure directly
/// TODO: add the rest of the fields
#[repr(C)]
pub(crate) struct HayleyFsInode {
    inode_type: InodeType, // TODO: currently 2 bytes? could be 1
    link_count: u16,
    mode: u16,
    uid: u32,
    gid: u32,
    ctime: bindings::timespec64,
    atime: bindings::timespec64,
    mtime: bindings::timespec64,
    blocks: u64, // TODO: not properly updated right now. do we even need to store this persistently?
    size: u64,
    ino: InodeNum,
    _padding: u64,
}

impl core::fmt::Debug for HayleyFsInode {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("HayleyFsInode")
            .field("inode_type", &self.inode_type)
            .field("link_count", &self.link_count)
            .field("mode", &self.mode)
            .field("uid", &self.uid)
            .field("gid", &self.gid)
            .field("ctime", &self.ctime.tv_nsec)
            .field("atime", &self.atime.tv_nsec)
            .field("mtime", &self.mtime.tv_nsec)
            .field("blocks", &self.blocks)
            .field("size", &self.size)
            .field("ino", &self.ino)
            .field("padding", &self._padding)
            .finish()
    }
}

pub(crate) struct LinkedInode {
    inode: InodeNum,
    links: Links<LinkedInode>,
}

impl PartialEq for LinkedInode {
    fn eq(&self, other: &Self) -> bool {
        self.inode == other.inode
    }
}

impl Eq for LinkedInode {}

impl Deref for LinkedInode {
    type Target = InodeNum;
    fn deref(&self) -> &Self::Target {
        &self.inode
    }
}

impl GetLinks for Box<LinkedInode> {
    type EntryType = LinkedInode;
    fn get_links(data: &Self::EntryType) -> &Links<Self::EntryType> {
        &data.links
    }
}

impl LinkedInode {
    pub(crate) fn new(inode: InodeNum) -> Self {
        Self {
            inode,
            links: Links::new(),
        }
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        self.inode
    }
}

#[allow(dead_code)]
pub(crate) struct InodeWrapper<'a, State, Op, Type> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    inode_type: PhantomData<Type>,
    ino: InodeNum,
    vfs_inode: Option<*mut bindings::inode>, // TODO: make this an fs::INode? or point it directly to the inode info structure?
    inode: &'a mut HayleyFsInode,
}

impl<'a, State, Op, Type> core::fmt::Debug for InodeWrapper<'a, State, Op, Type> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("InodeWrapper")
            .field("ino", &self.ino)
            .field("inode", &self.inode)
            .finish()
    }
}


impl<'a, State, Op, Type> PmObjWrapper for InodeWrapper<'a, State, Op, Type> {}

impl HayleyFsInode {
    /// Unsafe inode constructor for temporary use with init_fs only
    /// Does not flush the root inode
    pub(crate) unsafe fn init_root_inode(sbi: &SbInfo, inode: *mut bindings::inode) -> Result<&HayleyFsInode> {
        let mut root_ino = unsafe { sbi.get_inode_by_ino_mut(ROOT_INO)? };
        root_ino.ino = ROOT_INO;
        root_ino.link_count = 2;
        root_ino.size = 4096; // dir size always set to 4KB
        root_ino.inode_type = InodeType::DIR;
        root_ino.uid = unsafe {
            bindings::from_kuid(
                &mut bindings::init_user_ns as *mut bindings::user_namespace,
                sbi.uid,
            )
        };
        root_ino.gid = unsafe {
            bindings::from_kgid(
                &mut bindings::init_user_ns as *mut bindings::user_namespace,
                sbi.gid,
            )
        };
        root_ino.blocks = 0;
        root_ino.mode = sbi.mode | bindings::S_IFDIR as u16;

        let time = unsafe { bindings::current_time(inode) };
        root_ino.ctime = time;
        root_ino.atime = time;
        root_ino.mtime = time;

        Ok(root_ino)
    }

    pub(crate) fn get_link_count(&self) -> u16 {
        self.link_count
    }

    pub(crate) fn get_type(&self) -> InodeType {
        self.inode_type
    }

    pub(crate) fn get_size(&self) -> u64 {
        self.size
    }

    pub(crate) fn get_mode(&self) -> u16 {
        self.mode
    }

    pub(crate) fn get_uid(&self) -> u32 {
        self.uid
    }

    pub(crate) fn get_gid(&self) -> u32 {
        self.gid
    }

    pub(crate) fn get_mtime(&self) -> bindings::timespec64 {
        self.mtime
    }

    pub(crate) fn get_ctime(&self) -> bindings::timespec64 {
        self.ctime
    }

    pub(crate) fn get_atime(&self) -> bindings::timespec64 {
        self.atime
    }

    pub(crate) fn get_blocks(&self) -> u64 {
        self.blocks
    }

    pub(crate) unsafe fn inc_link_count(&mut self) {
        self.link_count += 1
    }

    pub(crate) unsafe fn dec_link_count(&mut self) {
        self.link_count -= 1
    }

    pub(crate) unsafe fn update_atime(&mut self, atime: bindings::timespec64) {
        self.atime = atime;
    }

    // TODO: update as fields are added
    pub(crate) fn is_initialized(&self) -> bool {
        self.inode_type != InodeType::NONE && 
        // self.link_count != 0 && // link count may be 0 if the file has been completely unlinked but is still open
        self.mode != 0 &&
        // uid/gid == 0 is root
        // TODO: check timestamps?
        self.ino != 0
    }

    // TODO: update as fields are added
    pub(crate) fn is_free(&self) -> bool {
        // if ANY field is non-zero, the inode is not free
        self.inode_type == InodeType::NONE &&
        self.link_count == 0 &&
        self.mode == 0 &&
        self.uid == 0 &&
        self.gid == 0 &&
        self.ctime.tv_sec == 0 &&
        self.ctime.tv_nsec == 0 &&
        self.atime.tv_sec == 0 &&
        self.atime.tv_nsec == 0 &&
        self.atime.tv_sec == 0 &&
        self.atime.tv_nsec == 0 &&
        self.blocks == 0 &&
        self.size == 0 &&
        self.ino == 0
    }
}

impl<'a, State, Op, Type> InodeWrapper<'a, State, Op, Type> {
    pub(crate) fn get_ino(&self) -> InodeNum {
        self.ino
    }

    #[allow(dead_code)]
    pub(crate) fn get_link_count(&self) -> u16 {
        self.inode.get_link_count()
    }

    pub(crate) fn get_size(&self) -> u64 {
        self.inode.get_size()
    }

    pub(crate) fn get_uid(&self) -> u32 {
        self.inode.get_uid()
    }

    pub(crate) fn get_gid(&self) -> u32 {
        self.inode.get_gid()
    }

    pub(crate) fn get_mtime(&self) -> bindings::timespec64 {
        self.inode.get_mtime()
    }

    pub(crate) fn get_ctime(&self) -> bindings::timespec64 {
        self.inode.get_ctime()
    }

    pub(crate) fn get_atime(&self) -> bindings::timespec64 {
        self.inode.get_atime()
    }

    pub(crate) fn get_blocks(&self) -> u64 {
        self.inode.get_blocks()
    }
}

impl<'a, State, Op, Type> InodeWrapper<'a, State, Op, Type> {
    // TODO: this needs to be handled specially for types so that type generic cannot be incorrect
    pub(crate) fn wrap_inode(
        vfs_inode: *mut bindings::inode,
        pi: &'a mut HayleyFsInode,
    ) -> InodeWrapper<'a, State, Op, Type> {
        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            inode_type: PhantomData,
            vfs_inode: Some(vfs_inode),
            ino: unsafe {(*vfs_inode).i_ino},
            inode: pi,
        }
    }

    pub(crate) fn new<NewState, NewOp>(
        i: InodeWrapper<'a, State, Op, Type>,
    ) -> InodeWrapper<'a, NewState, NewOp, Type> {
        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            ino: i.ino,
            inode_type: i.inode_type,
            vfs_inode: i.vfs_inode,
            inode: i.inode,
        }
    }

    pub(crate) fn get_type(&self) -> InodeType {
        self.inode.get_type()
    }

    // TODO: safety
    pub(crate) fn get_vfs_inode(&self) -> Result<*mut bindings::inode> {
        match self.vfs_inode {
            Some(vfs_inode) => Ok(vfs_inode),
            None => {pr_info!("ERROR: inode {:?} is uninitialized\n", self.ino); Err(EPERM)}
        }
    }
}

impl<'a, State, Op> InodeWrapper<'a, State, Op, RegInode> {
    /// Safety: Everything in the InodeInfo structs is protected by an Arc Mutex, except for 
    /// the inode number which is immutable. 
    pub(crate) fn get_inode_info(&self) -> Result<&HayleyFsRegInodeInfo> {
        match self.vfs_inode {
            Some(vfs_inode) => unsafe {Ok(<Box::<HayleyFsRegInodeInfo> as ForeignOwnable>::borrow((*vfs_inode).i_private))},
            None => {pr_info!("ERROR: inode {:?} is uninitialized\n", self.ino); Err(EPERM)}
        }
    }
}

impl<'a, State, Op: Initialized> InodeWrapper<'a, State, Op, DirInode> {
    // TODO: safety
    pub(crate) fn get_inode_info(&self) -> Result<&HayleyFsDirInodeInfo> {
        match self.vfs_inode {
            Some(vfs_inode) => unsafe {Ok(<Box::<HayleyFsDirInodeInfo> as ForeignOwnable>::borrow((*vfs_inode).i_private))},
            None => {pr_info!("ERROR: inode does not have vfs info attached\n"); Err(EPERM)}
        }
    }
}

impl<'a, Type> InodeWrapper<'a, Clean, Start, Type> {
    // this is only called in dirty_inode, so it consumes itself
    // the inode is flushed later in dirty_inode
    pub(crate) fn update_atime_consume(self, atime: bindings::timespec64) {
        unsafe { self.inode.update_atime(atime) };
    }

    pub(crate) fn inc_link_count(self) -> Result<InodeWrapper<'a, Dirty, IncLink, Type>> {
        if self.inode.get_link_count() == MAX_LINKS {
            Err(EMLINK)
        } else {
            unsafe { self.inode.inc_link_count() };
            // also update the inode's ctime. the time update may be reordered with the link change 
            // we make no guarantees about ordering of these two updates
            if let Some(vfs_inode) = self.vfs_inode {
                self.inode.ctime = unsafe { bindings::current_time(vfs_inode)};
            } else {
                pr_info!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
                return Err(EINVAL);
            }
            Ok(Self::new(self))
        }
    }

    #[allow(dead_code)]
    pub(crate) fn dec_link_count(self, _dentry: &DentryWrapper<'a, Clean, ClearIno>) -> Result<InodeWrapper<'a, Dirty, DecLink, Type>> {
        if self.inode.get_link_count() == 0 {
            Err(ENOENT)
        } else {
            unsafe { self.inode.dec_link_count() };
            // also update the inode's ctime. the time update may be reordered with the link change 
            // we make no guarantees about ordering of these two updates
            if let Some(vfs_inode) = self.vfs_inode {
                self.inode.ctime = unsafe { bindings::current_time(vfs_inode)};
            } else {
                pr_info!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
                return Err(EINVAL);
            }
            Ok(Self::new(self))
        }
    }

    // TODO: combine with regular dec link count
    pub(crate) fn dec_link_count_rename(self, _dentry: &DentryWrapper<'a, Clean, InitRenamePointer>) -> Result<InodeWrapper<'a, Dirty, DecLink, Type>> {
        if self.inode.get_link_count() == 0 {
            Err(ENOENT)
        } else {
            unsafe { self.inode.dec_link_count() };
            // also update the inode's ctime. the time update may be reordered with the link change 
            // we make no guarantees about ordering of these two updates
            if let Some(vfs_inode) = self.vfs_inode {
                self.inode.ctime = unsafe { bindings::current_time(vfs_inode)};
            } else {
                pr_info!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
                return Err(EINVAL);
            }
            Ok(Self::new(self))
        }
    }

    // TODO: get the number of bytes written from the page itself, somehow?
    #[allow(dead_code)]
    pub(crate) fn inc_size_single_page(
        self,
        bytes_written: u64,
        current_offset: u64,
        _page: StaticDataPageWrapper<'a, Clean, Written>,
    ) -> (u64, InodeWrapper<'a, Clean, IncSize, Type>) {
        let total_size = bytes_written + current_offset;
        // also update the inode's ctime and mtime. the time update may be reordered with the size change
        // we make no guarantees about ordering of these two updates
        if let Some(vfs_inode) = self.vfs_inode {
            let time = unsafe { bindings::current_time(vfs_inode) };
            self.inode.ctime = time;
            self.inode.mtime = time;
        } else {
            panic!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
        }
        if self.inode.size < total_size {
            self.inode.size = total_size;
            hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), true);
        }
        (
            self.inode.size,
            InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            },
        )
    }

    // TODO: use the same impl for all inc_size ops with a trait to reconcile 
    // different write types
    pub(crate) fn inc_size_runtime_check(
        self,
        bytes_written: u64,
        current_offset: u64,
        _pages: Vec<DataPageWrapper<'a, Clean, Written>>,
    ) -> (u64, InodeWrapper<'a, Clean, IncSize, Type>) {
        let total_size = bytes_written + current_offset;
        // also update the inode's ctime and mtime. the time update may be reordered with the size change
        // we make no guarantees about ordering of these two updates
        if let Some(vfs_inode) = self.vfs_inode {
            let time = unsafe { bindings::current_time(vfs_inode) };
            self.inode.ctime = time;
            self.inode.mtime = time;
        } else {
            panic!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
        }
        if self.inode.size < total_size {
            self.inode.size = total_size;
            hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), true);
        }
        (
            self.inode.size,
            InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            },
        )
    }

    pub(crate) fn inc_size_iterator<S: WrittenTo>(
        self, 
        bytes_written: u64,
        current_offset: u64,
        _page_list: &DataPageListWrapper<Clean, S>,
    ) -> (u64, InodeWrapper<'a, Clean, IncSize, Type>) {
        let total_size = bytes_written + current_offset;
        // also update the inode's ctime and mtime. the time update may be reordered with the size change
        // we make no guarantees about ordering of these two updates
        if let Some(vfs_inode) = self.vfs_inode {
            let time = unsafe { bindings::current_time(vfs_inode) };
            self.inode.ctime = time;
            self.inode.mtime = time;
        } else {
            panic!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
        }
        if self.inode.size < total_size {
            self.inode.size = total_size;
            hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), true);
        }
        (
            self.inode.size,
            InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            },
        )
    }

    pub(crate) fn dec_size(
        self, 
        new_size: u64,
    ) -> (u64, InodeWrapper<'a, Clean, DecSize, Type>) {
        let old_size = self.inode.size;
        // also update the inode's ctime and mtime. the time update may be reordered with the size change
        // we make no guarantees about ordering of these two updates
        if let Some(vfs_inode) = self.vfs_inode {
            let time = unsafe { bindings::current_time(vfs_inode) };
            self.inode.ctime = time;
            self.inode.mtime = time;
        } else {
            panic!("ERROR: no vfs inode for inode {:?} in dec_link_count\n", self.ino);
        }
        if self.inode.size > new_size {
            self.inode.size = new_size;
            hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), true);
        }
        (
            old_size, 
            InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            }
        )
        
    }
}

impl<'a> InodeWrapper<'a, Clean, Alloc, RegInode> {
    // TODO: this must be modeled in Alloy - not clear if it is safe to 
    // do this or not. Basically we can set an allocated inode's size
    // to something other than zero if we write to a page
    // 
    // Taking reference to the page is potentially risky because the page's typestate 
    // does not change. Maybe you could use a page-based transition that calls 
    // the inode set_size method? ensure both of them end up in the correct state
    pub(crate) fn set_size(
        self,
        bytes_written: u64,
        current_offset: u64,
        _pages: &DataPageListWrapper<Clean, Written>,
    ) -> (u64, InodeWrapper<'a, Clean, Alloc, RegInode>) {
        let total_size = bytes_written + current_offset;
        if self.inode.size < total_size {
            self.inode.size = total_size;
            hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), true);
        }
        (
            self.inode.size,
            InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            }
        )
    }
}

impl<'a> InodeWrapper<'a, Clean, DecLink, RegInode> {
    pub(crate) fn get_unlinked_ino(sbi: &'a SbInfo, ino: InodeNum, inode: *mut bindings::inode) -> Result<Self> {
        let pi = unsafe { sbi.get_inode_by_ino_mut(ino)? };
        if pi.get_link_count() != 0 || (pi.get_type() != InodeType::REG && pi.get_type() != InodeType::SYMLINK) || pi.is_free() {
            pr_info!("ERROR: inode {:?} is not unlinked\n", ino);
            pr_info!("inode {:?}: {:?}\n", ino, pi);
            Err(EINVAL)
        } else {
            Ok(InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: Some(inode),
                ino,
                inode: pi,
            })
        }
    }

    // this is horrifying
    pub(crate) fn try_complete_unlink_runtime(self, sbi: &'a SbInfo) -> 
        Result<core::result::Result<InodeWrapper<'a, Clean, Complete, RegInode>, (InodeWrapper<'a, Clean, Dealloc, RegInode>, Vec<DataPageWrapper<'a, Clean, ToUnmap>>)>> 
    {
        if self.inode.get_link_count() > 0 {
            Ok(Ok(InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            }))
        } else {
            // get the list of pages associated with this inode and convert them into 
            // ToUnmap wrappers
            let info = self.get_inode_info()?;
            let pages = info.get_all_pages()?;
            let mut unmap_vec = Vec::new();
            for page in pages.values() {
                let p = DataPageWrapper::mark_to_unmap(sbi, *page)?;
                unmap_vec.try_push(p)?;
            }
            Ok(
                Err(
                    (InodeWrapper {
                        state: PhantomData,
                        op: PhantomData,
                        inode_type: PhantomData,
                        vfs_inode: self.vfs_inode,
                        ino: self.ino,
                        inode: self.inode,
                    }, 
                    unmap_vec)
                )
            )
        }
    }

    pub(crate) fn try_complete_unlink_iterator(self) -> 
        Result<
            core::result::Result<
                InodeWrapper<'a, Clean, Complete, RegInode>, 
                (InodeWrapper<'a, Clean, Dealloc, RegInode>, DataPageListWrapper<Clean, ToUnmap>)
            >
        > {
            // there are still links, so don't delete the inode or its pages
            if self.inode.get_link_count() > 0 {
                Ok(Ok(InodeWrapper {
                    state: PhantomData,
                    op: PhantomData,
                    inode_type: PhantomData,
                    vfs_inode: self.vfs_inode,
                    ino: self.ino,
                    inode: self.inode,
                }))
            } else {
                let info = self.get_inode_info()?;
                let pages = DataPageListWrapper::get_data_pages_to_unmap(&info)?;
                Ok(
                    Err(
                        (
                            InodeWrapper {
                                state: PhantomData,
                                op: PhantomData,
                                inode_type: PhantomData,
                                vfs_inode: self.vfs_inode,
                                ino: self.ino,
                                inode: self.inode,
                            }, 
                            pages
                        )
                    )
                )
            }
        }
}

impl<'a> InodeWrapper<'a, Clean, Dealloc, RegInode> {
    // NOTE: data page wrappers don't actually need to be free, they just need to be in ClearIno
    pub(crate) fn runtime_dealloc(self, _freed_pages: Vec<DataPageWrapper<'a, Clean, Free>>) -> InodeWrapper<'a, Dirty, Complete, RegInode> {
        self.inode.inode_type = InodeType::NONE;
        // link count should already be 0
        assert!(self.inode.link_count == 0);
        self.inode.mode = 0;
        self.inode.uid = 0;
        self.inode.gid = 0;
        self.inode.ctime.tv_sec = 0;
        self.inode.ctime.tv_nsec = 0;
        self.inode.atime.tv_sec = 0;
        self.inode.atime.tv_nsec = 0;
        self.inode.mtime.tv_sec = 0;
        self.inode.mtime.tv_nsec = 0;
        self.inode.blocks = 0;
        self.inode.size = 0;
        self.inode.ino = 0;

        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            inode_type: PhantomData,
            vfs_inode: self.vfs_inode,
            ino: self.ino,
            inode: self.inode
        }
    }

    // NOTE: data page wrappers don't actually need to be free, they just need to be in ClearIno
    // TODO: combine with runtime version using a trait for the data page wrapper 
    pub(crate) fn iterator_dealloc(self, _freed_pages: DataPageListWrapper<Clean, Free>) -> InodeWrapper<'a, Dirty, Complete, RegInode> {
        // link count should already be 0
        assert!(self.inode.link_count == 0);
        unsafe { dealloc_pm_inode(self.inode)} ;
        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            inode_type: PhantomData,
            vfs_inode: self.vfs_inode,
            ino: self.ino,
            inode: self.inode
        }
    }
}

impl<'a> InodeWrapper<'a, Clean, Recovery, RegInode> {
    // SAFETY: this function is only safe to call on orphaned inodes during recovery
    // this function is missing many validity checks that other functions include because 
    // it is meant to be used on potentially invalid inodes
    pub(crate) unsafe fn get_recovery_inode(sbi: &SbInfo, ino: InodeNum) -> Result<Self> {
        // we assume here that all inodes are regular inodes to make things easier.
        // this should be safe because we will not be reading these inodes and the 
        // deallocation process is the same for all inode types
        let pi = unsafe { 
            sbi.get_inode_by_ino_mut(ino)?
        };
        Ok(InodeWrapper{
            state: PhantomData,
            op: PhantomData, 
            inode_type: PhantomData,
            vfs_inode: None,
            ino,
            inode: pi,
        })
    }

    pub(crate) fn recovery_dealloc(self) -> InodeWrapper<'a, Dirty, Complete, RegInode> {
        unsafe { dealloc_pm_inode(self.inode)};
        InodeWrapper{
            state: PhantomData,
            op: PhantomData, 
            inode_type: PhantomData,
            vfs_inode: None,
            ino: self.ino,
            inode: self.inode,
        }
    }
}

impl<'a> InodeWrapper<'a, Clean, TooManyLinks, RegInode> {
    // SAFETY: this function is only safe to call on live inodes who have a link count that is too high
    pub(crate) unsafe fn get_too_many_links_inode(sbi: &SbInfo, ino: InodeNum, real_lc: u16) -> Result<Self> {
        let pi = unsafe { 
            sbi.get_inode_by_ino_mut(ino)?
        };
        if !pi.is_initialized() {
            pr_info!("ERROR: inode {:?} is invalid\n", ino);
            return Err(EINVAL);
        }
        if pi.get_link_count() <= real_lc {
            pr_info!("ERROR: inode {:?} has a too low link count (real {:?}, persistent {:?})\n", ino, real_lc, pi.get_link_count());
            return Err(EINVAL);
        }
        Ok(InodeWrapper{
            state: PhantomData,
            op: PhantomData, 
            inode_type: PhantomData,
            vfs_inode: None,
            ino: ino,
            inode: pi,
        })
    }

    pub(crate) fn recovery_dec_link(self, real_lc: u16) -> InodeWrapper<'a, Dirty, DecLink, RegInode> {
        self.inode.link_count = real_lc;
        InodeWrapper{
            state: PhantomData,
            op: PhantomData, 
            inode_type: PhantomData,
            vfs_inode: None,
            ino: self.ino,
            inode: self.inode,
        }
    }

}

unsafe fn dealloc_pm_inode(inode: &mut HayleyFsInode) {
    inode.inode_type = InodeType::NONE;
    inode.mode = 0;
    inode.uid = 0;
    inode.gid = 0;
    inode.ctime.tv_sec = 0;
    inode.ctime.tv_nsec = 0;
    inode.atime.tv_sec = 0;
    inode.atime.tv_nsec = 0;
    inode.mtime.tv_sec = 0;
    inode.mtime.tv_nsec = 0;
    inode.blocks = 0;
    inode.size = 0;
    inode.ino = 0;
}

impl<'a, Op: DeleteDir> InodeWrapper<'a, Clean, Op, DirInode> {
    /// This function checks that an inode's link count is valid for removing pages and then
    /// puts it in a typestate that allows its pages to be unmapped and deallocated
    pub(crate) fn set_unmap_page_state(self) -> Result<InodeWrapper<'a, Clean, UnmapPages, DirInode>> {
        if self.inode.link_count > 1 {
            pr_info!("ERROR: invalid link count\n");
            Err(EINVAL)
        } else {
            Ok(InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: self.vfs_inode,
                ino: self.ino,
                inode: self.inode,
            })
        }
    }
}

impl<'a> InodeWrapper<'a, Clean, UnmapPages, DirInode> {
    // NOTE: data page wrappers don't actually need to be free, they just need to be in ClearIno
    // any state after ClearIno is fine
    pub(crate) fn iterator_dealloc(self, _freed_pages: DirPageListWrapper<Clean, Free>) -> InodeWrapper<'a, Dirty, Complete, DirInode> {
        self.inode.inode_type = InodeType::NONE;
        // link count should 2 for ./.. but we don't store those in durable PM, so it's safe 
        // to just clear the link count if it is in fact 2
        assert!(self.inode.link_count == 1);
        self.inode.mode = 0;
        self.inode.uid = 0;
        self.inode.gid = 0;
        self.inode.link_count = 0;
        self.inode.ctime.tv_sec = 0;
        self.inode.ctime.tv_nsec = 0;
        self.inode.atime.tv_sec = 0;
        self.inode.atime.tv_nsec = 0;
        self.inode.mtime.tv_sec = 0;
        self.inode.mtime.tv_nsec = 0;
        self.inode.blocks = 0;
        self.inode.size = 0;
        self.inode.ino = 0;

        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            inode_type: PhantomData,
            vfs_inode: self.vfs_inode,
            ino: self.ino,
            inode: self.inode
        }
    }

    pub(crate) fn runtime_dealloc(self, _freed_pages: Vec<DirPageWrapper<'a, Clean, Free>>) -> InodeWrapper<'a, Dirty, Complete, DirInode> {
        self.inode.inode_type = InodeType::NONE;
        // link count should 1 for . but we don't store . or .. in durable PM, so it's safe 
        // to just clear the link count if it is in fact 1
        assert!(self.inode.link_count == 2);
        self.inode.mode = 0;
        self.inode.uid = 0;
        self.inode.gid = 0;
        self.inode.ctime.tv_sec = 0;
        self.inode.ctime.tv_nsec = 0;
        self.inode.atime.tv_sec = 0;
        self.inode.atime.tv_nsec = 0;
        self.inode.mtime.tv_sec = 0;
        self.inode.mtime.tv_nsec = 0;
        self.inode.blocks = 0;
        self.inode.size = 0;
        self.inode.ino = 0;

        InodeWrapper {
            state: PhantomData,
            op: PhantomData,
            inode_type: PhantomData,
            vfs_inode: self.vfs_inode,
            ino: self.ino,
            inode: self.inode
        }
    }
}

impl<'a> InodeWrapper<'a, Clean, Free, RegInode> {
    pub(crate) fn get_free_reg_inode_by_ino(sbi: &'a SbInfo, ino: InodeNum) -> Result<Self> {
        let raw_inode = unsafe { sbi.get_inode_by_ino_mut(ino)? };
        if raw_inode.is_free() {
            Ok(InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: None,
                ino,
                inode: raw_inode,
            })
        } else {
            pr_info!("ERROR: regular inode {:?} is not free\n", ino);
            Err(EPERM)
        }
    }

    pub(crate) fn allocate_file_inode(
        self,
        inode: &fs::INode,
        mode: u16,
    ) -> Result<InodeWrapper<'a, Dirty, Alloc, RegInode>> {
        self.inode.link_count = 1;
        self.inode.ino = self.ino;
        self.inode.inode_type = InodeType::REG;
        self.inode.mode = mode;
        self.inode.blocks = 0;
        self.inode.uid = unsafe { (*inode.get_inner()).i_uid.val };
        self.inode.gid = unsafe { (*inode.get_inner()).i_gid.val };
        let time = unsafe { bindings::current_time(inode.get_inner()) };
        self.inode.ctime = time;
        self.inode.atime = time;
        self.inode.mtime = time;
        Ok(Self::new(self))
    }

    // TODO: should symlinks have their own ghost type?
    pub(crate) fn allocate_symlink_inode(
        self,
        inode: &fs::INode,
        mode: u16
    ) -> Result<InodeWrapper<'a, Dirty, Alloc, RegInode>> {
        self.inode.link_count = 1;
        self.inode.ino = self.ino;
        self.inode.inode_type = InodeType::SYMLINK;
        self.inode.mode = mode;
        self.inode.blocks = 0;
        self.inode.uid = unsafe { (*inode.get_inner()).i_uid.val };
        self.inode.gid = unsafe { (*inode.get_inner()).i_gid.val };
        let time = unsafe { bindings::current_time(inode.get_inner()) };
        self.inode.ctime = time;
        self.inode.atime = time;
        self.inode.mtime = time;
        Ok(Self::new(self))
    }
}

impl<'a> InodeWrapper<'a, Clean, Free, DirInode> {
    pub(crate) fn get_free_dir_inode_by_ino(sbi: &'a SbInfo, ino: InodeNum) -> Result<Self> {
        let raw_inode = unsafe { sbi.get_inode_by_ino_mut(ino)? };
        if raw_inode.is_free() {
            Ok(InodeWrapper {
                state: PhantomData,
                op: PhantomData,
                inode_type: PhantomData,
                vfs_inode: None,
                ino,
                inode: raw_inode,
            })
        } else {
            pr_info!("ERROR: dir inode {:?} is not free\n", ino);
            Err(EPERM)
        }
    }

    pub(crate) fn allocate_dir_inode(
        self,
        parent: &fs::INode,
        mode: u16,
    ) -> Result<InodeWrapper<'a, Dirty, Alloc, DirInode>> {
        self.inode.link_count = 2;
        self.inode.ino = self.ino;
        self.inode.blocks = 0;
        self.inode.inode_type = InodeType::DIR;
        self.inode.mode = mode | bindings::S_IFDIR as u16;
        self.inode.uid = unsafe { (*parent.get_inner()).i_uid.val };
        self.inode.gid = unsafe { (*parent.get_inner()).i_gid.val };
        let time = unsafe { bindings::current_time(parent.get_inner()) };
        self.inode.ctime = time;
        self.inode.atime = time;
        self.inode.mtime = time;
        Ok(Self::new(self))
    }
}

impl<'a> InodeWrapper<'a, Clean, Complete, RegInode> {
    /// In symlink, we need to create a VFS inode **before** inserting pages into the index.
    /// This function allows us to set the VFS inode after allocation only if the inode 
    /// is in a complete state and otherwise has no inode (which should only happen in symlink)
    pub(crate) fn set_vfs_inode(&mut self, vfs_inode: *mut bindings::inode) -> Result<()>{
        if self.vfs_inode.is_none() {
            self.vfs_inode = Some(vfs_inode);
            Ok(())
        } else {
            pr_info!("ERROR: inode {:?} already has a VFS inode\n", self.ino);
            Err(EPERM)
        }
    }
}

// timestamp update methods can be used at any time because we don't make any strong guarantees
// about exactly when timestamps will be updated. The inode still needs to be flushed afterward.
impl<'a, State, Op, Type> InodeWrapper<'a, State, Op, Type> {
    pub(crate) fn update_ctime_and_mtime(self, timestamp: bindings::timespec64) -> InodeWrapper<'a, Dirty, Op, Type> {
        self.inode.ctime = timestamp;
        self.inode.mtime = timestamp;
        self.inode.atime = timestamp;
        Self::new(self)
    }

    pub(crate) fn update_ctime(self, timestamp: bindings::timespec64) -> InodeWrapper<'a, Dirty, Op, Type>{
        self.inode.ctime = timestamp;
        self.inode.atime = timestamp;
        Self::new(self)
    }
}

impl<'a, Op, Type> InodeWrapper<'a, Dirty, Op, Type> {
    pub(crate) fn flush(self) -> InodeWrapper<'a, InFlight, Op, Type> {
        hayleyfs_flush_buffer(self.inode, mem::size_of::<HayleyFsInode>(), false);
        Self::new(self)
    }
}

impl<'a, Op, Type> InodeWrapper<'a, InFlight, Op, Type> {
    pub(crate) fn fence(self) -> InodeWrapper<'a, Clean, Op, Type> {
        sfence();
        Self::new(self)
    }
}

/// Interface for volatile inode allocator structures
pub(crate) trait InodeAllocator {
    fn new(val: u64, num_inodes: u64) -> Result<Self> where Self: Sized;
    fn new_from_alloc_vec(alloc_inodes: List<Box<LinkedInode>>, num_alloc_inodes: u64, start: u64, num_inodes: u64) -> Result<Self> where Self: Sized;
    fn alloc_ino(&self, sbi: &SbInfo) -> Result<InodeNum>;
    // TODO: should this be unsafe or require a free inode wrapper?
    fn dealloc_ino(&self, ino: InodeNum, sbi: &SbInfo) -> Result<()>;
}

pub(crate) struct RBInodeAllocator {
    map: Arc<Mutex<RBTree<InodeNum, ()>>>,
}

impl InodeAllocator for RBInodeAllocator {
    fn new(val: u64, num_inodes: u64) -> Result<Self> {
        let mut rb = RBTree::new();
        for i in val..num_inodes {
            rb.try_insert(i, ())?;
        }
        Ok(Self {
            map: Arc::try_new(Mutex::new(rb))?
        })
    }

    fn new_from_alloc_vec(alloc_inodes: List<Box<LinkedInode>>, num_alloc_inodes: u64, start: u64, num_inodes: u64) -> Result<Self> {
        let mut rb = RBTree::new();
        let mut cur_ino = start;
        let mut inode_cursor = alloc_inodes.cursor_front();
        let mut current_alloc_inode = inode_cursor.current();

        // skip inode 1
        if let Some(inode) = current_alloc_inode {
            if inode.get_ino() == 1 {
                inode_cursor.move_next();
                current_alloc_inode = inode_cursor.current();
            }
        }

        // if start <= alloc_inodes.len().try_into()? {
        if num_alloc_inodes > 0 {
            while current_alloc_inode.is_some() {
                if let Some(current_alloc_inode) = current_alloc_inode {
                    let current_alloc_ino = current_alloc_inode.get_ino();
                    if cur_ino < current_alloc_ino {
                        rb.try_insert(cur_ino, ())?;
                        cur_ino += 1;
                    } else if cur_ino == current_alloc_ino {
                        cur_ino += 1;
                        inode_cursor.move_next();
                    } else {
                        // shouldn't ever happen
                        pr_info!("ERROR: current inode is {:?} but current alloc inode is {:?}\n", cur_ino, current_alloc_ino);
                        return Err(EINVAL);
                    }
                }
                current_alloc_inode = inode_cursor.current();
            }
        } 
        // add all remaining inodes to the allocator
        if cur_ino < num_inodes.try_into()? {
            for j in cur_ino..num_inodes.try_into()? {
                rb.try_insert(j.try_into()?, ())?;
            }
        }
        Ok(Self {
            map: Arc::try_new(Mutex::new(rb))?
        })
    }

    fn alloc_ino(&self, sbi: &SbInfo) -> Result<InodeNum> {
        let map = Arc::clone(&self.map);
        let mut map = map.lock();
        let iter = map.iter().next();
        let ino = match iter {
            None => {
                pr_info!("ERROR: ran out of inodes in RB inode allocator\n");
                return Err(ENOSPC);
            } 
            Some(ino) => *ino.0
        };
        map.remove(&ino);
        sbi.inc_inodes_in_use();
        Ok(ino)
    }

    fn dealloc_ino(&self, ino: InodeNum, sbi: &SbInfo) -> Result<()> {
        let map = Arc::clone(&self.map);
        let mut map = map.lock();
        let res = map.try_insert(ino, ())?;
        // pr_info!("deallocated ino {:?}\n", ino);
        sbi.dec_inodes_in_use();
        if res.is_some() {
            pr_info!("ERROR: inode {:?} was deallocated but is already in allocator\n", ino);
            Err(EINVAL)
        } else {
            Ok(())
        }
    }
}
