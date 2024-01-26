use crate::balloc::*;
use crate::defs::*;
use crate::h_dir::*;
use crate::typestate::*;
use core::ffi;
use kernel::prelude::*;
use kernel::{
    rbtree::RBTree,
    sync::{smutex::Mutex, Arc},
};

// TODO: how should name be represented here? array is probably not the best?
#[allow(dead_code)]
#[derive(Debug, Copy, Clone)]
pub(crate) struct DentryInfo {
    ino: InodeNum,
    virt_addr: Option<*const ffi::c_void>,
    name: [u8; MAX_FILENAME_LEN],
    is_dir: bool,
}

#[allow(dead_code)]
impl DentryInfo {
    pub(crate) fn new(
        ino: InodeNum,
        virt_addr: Option<*const ffi::c_void>,
        name: [u8; MAX_FILENAME_LEN],
        is_dir: bool
    ) -> Self {
        Self {
            ino,
            virt_addr,
            name,
            is_dir,
        }
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        self.ino
    }

    pub(crate) fn get_virt_addr(&self) -> Option<*const ffi::c_void> {
        self.virt_addr
    }

    pub(crate) fn get_name(&self) -> &[u8; MAX_FILENAME_LEN] {
        &self.name
    }

    pub(crate) fn get_name_as_cstr(&self) -> &CStr {
        unsafe { CStr::from_char_ptr(self.get_name().as_ptr() as *const core::ffi::c_char) }
    }

    pub(crate) fn is_dir(&self) -> bool {
        self.is_dir
    }
}

// /// maps inodes to info about dentries for inode's children
// pub(crate) trait InoDentryMap {
//     fn new() -> Result<Self>
//     where
//         Self: Sized;
//     fn insert(&self, ino: InodeNum, dentry: DentryInfo) -> Result<()>;
//     fn lookup_dentry(&self, ino: &InodeNum, name: &CStr) -> Option<DentryInfo>;
//     fn delete(&self, ino: InodeNum, dentry: DentryInfo) -> Result<()>;
// }

#[allow(dead_code)]
pub(crate) struct InoDentryTree {
    map: Arc<Mutex<RBTree<InodeNum, RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>>>>,
}

impl InoDentryTree {
    pub(crate) fn new() -> Result<Self> {
        Ok(Self {
            map: Arc::try_new(Mutex::new(RBTree::new()))?,
        })
    }

    pub(crate) fn insert(&self, ino: InodeNum, dentry: DentryInfo) -> Result<()> {
        let map = Arc::clone(&self.map);
        let mut map = map.lock();
        if let Some(ref mut node) = map.get_mut(&ino) {
            node.try_insert(dentry.name, dentry)?;
        } else {
            let mut tree = RBTree::new();
            tree.try_insert(dentry.name, dentry)?;
            map.try_insert(ino, tree)?;
        }
        Ok(())
    }

    pub(crate) fn remove(
        &self,
        ino: InodeNum,
    ) -> Option<RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>> {
        let map = Arc::clone(&self.map);
        let mut map = map.lock();
        map.remove(&ino)
    }
}

#[derive(Debug, Copy, Clone, PartialOrd, Eq, PartialEq, Ord)]
pub(crate) struct DirPageInfo {
    // owner: InodeNum,
    page_no: PageNum,
    // full: bool,
    // virt_addr: *mut ffi::c_void,
}

impl DirPageInfo {
    pub(crate) fn get_page_no(&self) -> PageNum {
        self.page_no
    }

    pub(crate) fn new(page_no: PageNum) -> Self {
        Self { page_no }
    }
}

// #[derive(Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
// pub(crate) struct DataPageInfo {
//     // owner: InodeNum,
//     page_no: PageNum,
//     // offset: u64,
// }

// impl DataPageInfo {
//     pub(crate) fn new(page_no: PageNum) -> Self {
//         Self {
//             // owner,
//             page_no,
//             // offset,
//         }
//     }

//     // pub(crate) fn get_page_no(&self) -> PageNum {
//     //     self.page_no
//     // }

//     // pub(crate) fn get_offset(&self) -> u64 {
//     //     self.offset
//     // }

//     // pub(crate) fn get_owner(&self) -> InodeNum {
//     //     self.owner
//     // }
// }

// TODO: could just be offset and page number - storing whole DataPageInfo is redundant...
#[repr(C)]
pub(crate) struct HayleyFsRegInodeInfo {
    ino: InodeNum,
    num_pages: u64,
    pages: Arc<Mutex<RBTree<u64, PageNum>>>,
}

impl HayleyFsRegInodeInfo {
    pub(crate) fn new(ino: InodeNum) -> Result<Self> {
        Ok(Self {
            ino,
            num_pages: 0,
            pages: Arc::try_new(Mutex::new(RBTree::new()))?,
        })
    }

    pub(crate) fn new_from_tree(
        ino: InodeNum,
        tree: RBTree<u64, PageNum>,
        num_pages: u64,
    ) -> Result<Self> {
        Ok(Self {
            ino,
            pages: Arc::try_new(Mutex::new(tree))?,
            num_pages,
        })
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        self.ino
    }

    pub(crate) fn get_num_pages(&self) -> u64 {
        self.num_pages
    }
}

/// maps file inodes to info about their pages
pub(crate) trait InoDataPageMap {
    fn new(ino: InodeNum) -> Result<Self>
    where
        Self: Sized;
    fn insert<'a, State: Initialized>(
        &self,
        page: &DataPageWrapper<'a, Clean, State>,
    ) -> Result<()>;
    fn insert_unchecked<'a, State: Initialized>(
        &self,
        page: &StaticDataPageWrapper<'a, Clean, State>,
    ) -> Result<()>;
    fn insert_page_iterator(&self, offset: u64, page_no: PageNum) -> Result<()>;
    fn insert_pages<S: WrittenTo>(&self, page_list: DataPageListWrapper<Clean, S>) -> Result<()>;
    fn find(&self, offset: u64) -> Option<PageNum>;
    fn get_all_pages(&self) -> Result<RBTree<u64, PageNum>>;
    fn remove_pages(&self, page_list: &DataPageListWrapper<Clean, Free>) -> Result<()>;
}

impl InoDataPageMap for HayleyFsRegInodeInfo {
    fn new(ino: InodeNum) -> Result<Self> {
        HayleyFsRegInodeInfo::new(ino)
    }

    fn insert<'a, State: Initialized>(
        &self,
        page: &DataPageWrapper<'a, Clean, State>,
    ) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        let offset = page.get_offset();
        pages.try_insert(offset, page.get_page_no())?;
        Ok(())
    }

    fn insert_unchecked<'a, State: Initialized>(
        &self,
        page: &StaticDataPageWrapper<'a, Clean, State>,
    ) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        let offset = page.get_offset();
        pages.try_insert(offset, page.get_page_no())?;
        Ok(())
    }

    fn insert_page_iterator(&self, offset: u64, page_no: PageNum) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        pages.try_insert(offset, page_no)?;
        Ok(())
    }

    fn insert_pages<S: WrittenTo>(&self, page_list: DataPageListWrapper<Clean, S>) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        let mut cursor = page_list.get_page_list_cursor();
        let mut current = cursor.current();
        let mut offset = page_list.get_offset();
        while current.is_some() {
            if let Some(current) = current {
                pages.try_insert(offset, current.get_page_no())?;
            }
            cursor.move_next();
            current = cursor.current();
            offset += HAYLEYFS_PAGESIZE;
        }
        Ok(())
    }

    fn find(&self, offset: u64) -> Option<PageNum> {
        let pages = Arc::clone(&self.pages);
        let pages = pages.lock();
        let result = pages.get(&offset);
        match result {
            Some(page) => Some(*page),
            None => None,
        }
    }

    fn get_all_pages(&self) -> Result<RBTree<u64, PageNum>> {
        let pages = Arc::clone(&self.pages);
        let pages = pages.lock();
        let mut return_tree = RBTree::new();
        // TODO: can you do this without copying all of the pages?
        for offset in pages.keys() {
            return_tree.try_insert(*offset, *pages.get(offset).unwrap())?;
        }
        Ok(return_tree)
    }

    fn remove_pages(&self, page_list: &DataPageListWrapper<Clean, Free>) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        let mut cursor = page_list.get_page_list_cursor();
        let mut current = cursor.current();
        while current.is_some() {
            if let Some(current) = current {
                pages.remove(&current);
            }
            cursor.move_next();
            current = cursor.current();
        }
        Ok(())
    }
}

/// maps dir inodes to info about their pages
pub(crate) trait InoDirPageMap {
    fn new(ino: InodeNum, dentries: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>) -> Result<Self>
    where
        Self: Sized;
    fn insert<'a, State: Initialized>(&self, page: &DirPageWrapper<'a, Clean, State>)
        -> Result<()>;
    fn insert_page_infos(&self, new_pages: RBTree<DirPageInfo, ()>) -> Result<()>;
    fn find_page_with_free_dentry(&self, sbi: &SbInfo) -> Result<Option<DirPageInfo>>;
    fn get_all_pages(&self) -> Result<RBTree<DirPageInfo, ()>>;
    fn delete(&self, page: DirPageInfo) -> Result<()>;
    fn debug_print_pages(&self);
}

#[repr(C)]
pub(crate) struct HayleyFsDirInodeInfo {
    ino: InodeNum,
    pages: Arc<Mutex<RBTree<DirPageInfo, ()>>>,
    dentries: Arc<Mutex<RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>>>, // dentries: Arc<Mutex<Vec<DentryInfo>>>,
}

impl HayleyFsDirInodeInfo {
    pub(crate) fn new(ino: InodeNum, dentries: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>) -> Result<Self> {
        Ok(Self {
            ino,
            pages: Arc::try_new(Mutex::new(RBTree::new()))?,
            dentries: Arc::try_new(Mutex::new(dentries))?,
        })
    }

    pub(crate) fn new_from_tree(
        ino: InodeNum,
        page_tree: RBTree<DirPageInfo, ()>,
        dentry_tree: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>,
    ) -> Result<Self> {
        Ok(Self {
            ino,
            pages: Arc::try_new(Mutex::new(page_tree))?,
            dentries: Arc::try_new(Mutex::new(dentry_tree))?,
        })
    }

    pub(crate) fn get_ino(&self) -> InodeNum {
        self.ino
    }
}

impl InoDirPageMap for HayleyFsDirInodeInfo {
    fn new(ino: InodeNum, dentries: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>) -> Result<Self> {
        Self::new(ino, dentries)
    }

    fn insert<'a, State: Initialized>(
        &self,
        page: &DirPageWrapper<'a, Clean, State>,
    ) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        // TODO: sort?
        let page_info = DirPageInfo {
            page_no: page.get_page_no(),
        };
        pages.try_insert(page_info, ())?;
        Ok(())
    }

    fn insert_page_infos(&self, new_pages: RBTree<DirPageInfo, ()>) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        // TODO: sort?
        for (key, value) in new_pages.iter() {
            pages.try_insert(*key, *value)?;
        }
        Ok(())
    }

    // TODO: this only works because we don't ever deallocate dir pages right now
    // there could be a race between a process that is deleting a dir page and a
    // process trying to add a dentry to it. this method should just add the dentry
    fn find_page_with_free_dentry<'a>(&self, sbi: &SbInfo) -> Result<Option<DirPageInfo>> {
        let pages = Arc::clone(&self.pages);
        let pages = pages.lock();
        for page in pages.keys() {
            let p = DirPageWrapper::from_page_no(sbi, page.get_page_no())?;
            if p.has_free_space(sbi)? {
                return Ok(Some(page.clone()));
            }
        }

        Ok(None)
    }

    // TODO: should this return a linked list?
    fn get_all_pages(&self) -> Result<RBTree<DirPageInfo, ()>> {
        let pages = Arc::clone(&self.pages);
        let pages = pages.lock();
        let mut return_tree = RBTree::new();
        // TODO: can you do this without copying all of the pages?
        for page in pages.keys() {
            return_tree.try_insert(page.clone(), ())?;
        }
        Ok(return_tree)
    }

    fn delete(&self, page: DirPageInfo) -> Result<()> {
        let pages = Arc::clone(&self.pages);
        let mut pages = pages.lock();
        pages.remove(&page);
        Ok(())
    }

    fn debug_print_pages(&self) {
        let pages = Arc::clone(&self.pages);
        let pages = pages.lock();
        for page in pages.keys() {
            pr_info!("{:?}\n", page);
        }
    }
}

pub(crate) trait InoDentryMap {
    fn insert_dentry(&self, dentry: DentryInfo) -> Result<()>;
    fn insert_dentries(
        &self,
        new_dentries: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>,
    ) -> Result<()>;
    fn lookup_dentry(&self, name: &CStr) -> Result<Option<DentryInfo>>;
    fn get_all_dentries(&self) -> Result<Vec<DentryInfo>>;
    fn delete_dentry(&self, dentry: DentryInfo) -> Result<()>;
    fn atomic_add_and_delete_dentry<'a>(
        &self,
        new_dentry: &DentryWrapper<'a, Clean, Complete>,
        old_dentry_name: &[u8; MAX_FILENAME_LEN],
    ) -> Result<()>;
    fn atomic_add_and_delete_dentry_crossdir<'a>(
        &self, // src dir
        dst_dir: &HayleyFsDirInodeInfo,
        new_dentry: &DentryWrapper<'a, Clean, Complete>,
        old_dentry_name: &[u8; MAX_FILENAME_LEN],
    ) -> Result<()>;
    fn has_dentries(&self) -> bool;
    fn debug_print_dentries(&self);
}

impl InoDentryMap for HayleyFsDirInodeInfo {
    fn insert_dentry(&self, dentry: DentryInfo) -> Result<()> {
        let dentries = Arc::clone(&self.dentries);
        let mut dentries = dentries.lock();
        dentries.try_insert(dentry.name, dentry)?;
        Ok(())
    }

    fn insert_dentries(
        &self,
        new_dentries: RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>,
    ) -> Result<()> {
        let dentries = Arc::clone(&self.dentries);
        let mut dentries = dentries.lock();
        for name in new_dentries.keys() {
            // janky hack to fill in the tree. ideally we could do this
            // without iterating.
            // it is safe to unwrap because we know name is a key and we have
            // ownership of new_dentries
            dentries.try_insert(*name, *new_dentries.get(name).unwrap())?;
        }
        Ok(())
    }

    fn lookup_dentry(&self, name: &CStr) -> Result<Option<DentryInfo>> {
        if name.len() >= MAX_FILENAME_LEN {
            return Err(ENAMETOOLONG);
        }
        let dentries = Arc::clone(&self.dentries);
        let dentries = dentries.lock();
        // TODO: can you do this without creating the array?
        let mut full_filename = [0; MAX_FILENAME_LEN];
        full_filename[..name.len()].copy_from_slice(name.as_bytes());
        let dentry = dentries.get(&full_filename);
        Ok(dentry.copied())
    }

    fn get_all_dentries(&self) -> Result<Vec<DentryInfo>> {
        let dentries = Arc::clone(&self.dentries);
        let dentries = dentries.lock();
        let mut return_vec = Vec::new();

        // TODO: use an iterator method
        for d in dentries.values() {
            return_vec.try_push(d.clone())?;
        }
        Ok(return_vec)
    }

    fn delete_dentry(&self, dentry: DentryInfo) -> Result<()> {
        let dentries = Arc::clone(&self.dentries);
        let mut dentries = dentries.lock();
        dentries.remove(&dentry.name);
        Ok(())
    }

    fn atomic_add_and_delete_dentry<'a>(
        &self,
        new_dentry: &DentryWrapper<'a, Clean, Complete>,
        old_dentry_name: &[u8; MAX_FILENAME_LEN], // can't use actual dentry because it no longer has a name
    ) -> Result<()> {
        let new_dentry_info = new_dentry.get_dentry_info();
        let dentries = Arc::clone(&self.dentries);
        let mut dentries = dentries.lock();
        dentries.try_insert(new_dentry_info.name, new_dentry_info)?;
        dentries.remove(old_dentry_name);
        Ok(())
    }

    fn atomic_add_and_delete_dentry_crossdir<'a>(
        &self, // src dir
        dst_dir: &HayleyFsDirInodeInfo,
        new_dentry: &DentryWrapper<'a, Clean, Complete>,
        old_dentry_name: &[u8; MAX_FILENAME_LEN],
    ) -> Result<()> {
        let new_dentry_info = new_dentry.get_dentry_info();
        let src_dentries = Arc::clone(&self.dentries);
        let mut src_dentries = src_dentries.lock();
        let dst_dentries = Arc::clone(&dst_dir.dentries);
        let mut dst_dentries = dst_dentries.lock();
        dst_dentries.try_insert(new_dentry_info.name, new_dentry_info)?;
        src_dentries.remove(old_dentry_name);
        Ok(())
    }

    fn has_dentries(&self) -> bool {
        let dentries = Arc::clone(&self.dentries);
        let dentries = dentries.lock();
        for d in dentries.keys() {
            let name = unsafe { CStr::from_char_ptr(d.as_ptr() as *const core::ffi::c_char).to_str().unwrap() };
            if name != "." && name != ".." {
                return true;
            }
        }
        false
    }

    fn debug_print_dentries(&self) {
        let dentries = Arc::clone(&self.dentries);
        let dentries = dentries.lock();
        for d in dentries.values() {
            pr_info!("{:?}\n", d);
        }
    }
}

pub(crate) trait PageInfo {}
// impl PageInfo for DataPageInfo {}
impl PageInfo for DirPageInfo {}

pub(crate) struct InoDataPageTree {
    tree: Arc<Mutex<RBTree<InodeNum, RBTree<u64, PageNum>>>>,
}

impl InoDataPageTree {
    pub(crate) fn new() -> Result<Self> {
        Ok(Self {
            tree: Arc::try_new(Mutex::new(RBTree::new()))?,
        })
    }

    pub(crate) fn insert_inode(&self, ino: InodeNum, pages: RBTree<u64, PageNum>) -> Result<()> {
        let tree = Arc::clone(&self.tree);
        let mut tree = tree.lock();
        tree.try_insert(ino, pages)?;
        Ok(())
    }

    pub(crate) fn remove(&self, ino: InodeNum) -> Option<RBTree<u64, PageNum>> {
        let tree = Arc::clone(&self.tree);
        let mut tree = tree.lock();
        tree.remove(&ino)
    }
}

pub(crate) struct InoDirPageTree {
    tree: Arc<Mutex<RBTree<InodeNum, RBTree<DirPageInfo, ()>>>>,
}

impl InoDirPageTree {
    pub(crate) fn new() -> Result<Self> {
        Ok(Self {
            tree: Arc::try_new(Mutex::new(RBTree::new()))?,
        })
    }

    pub(crate) fn insert_inode(&self, ino: InodeNum, pages: RBTree<DirPageInfo, ()>) -> Result<()> {
        let tree = Arc::clone(&self.tree);
        let mut tree = tree.lock();
        tree.try_insert(ino, pages)?;
        Ok(())
    }

    pub(crate) fn insert_one(&self, ino: InodeNum, page: DirPageInfo) -> Result<()> {
        let tree = Arc::clone(&self.tree);
        let mut tree = tree.lock();
        let entry = tree.get_mut(&ino);
        if let Some(entry) = entry {
            entry.try_insert(page, ())?;
        } else {
            let mut new_tree = RBTree::new();
            new_tree.try_insert(page, ())?;
            tree.try_insert(ino, new_tree)?;
        }
        Ok(())
    }

    pub(crate) fn remove(&self, ino: InodeNum) -> Option<RBTree<DirPageInfo, ()>> {
        let tree = Arc::clone(&self.tree);
        let mut tree = tree.lock();
        tree.remove(&ino)
    }
}

pub(crate) fn move_dir_inode_tree_to_map(
    sbi: &SbInfo,
    parent_inode_info: &HayleyFsDirInodeInfo,
) -> Result<()> {
    let ino = parent_inode_info.get_ino();
    let dentries = sbi.ino_dentry_tree.remove(ino);
    let dir_pages = sbi.ino_dir_page_tree.remove(ino);

    if let Some(dentries) = dentries {
        parent_inode_info.insert_dentries(dentries)?;
    }
    if let Some(dir_pages) = dir_pages {
        parent_inode_info.insert_page_infos(dir_pages)?;
    }
    Ok(())
}

pub(crate) struct InodeToFreeList {
    list: Arc<Mutex<RBTree<InodeNum, ()>>>,
}

impl InodeToFreeList {
    pub(crate) fn new() -> Result<Self> {
        Ok(Self {
            list: Arc::try_new(Mutex::new(RBTree::new()))?,
        })
    }

    pub(crate) fn insert(&self, ino: InodeNum) -> Result<()> {
        let list = Arc::clone(&self.list);
        let mut list = list.lock();
        list.try_insert(ino, ())?;
        Ok(())
    }

    // looks up the given inode num and removes it if it is present
    pub(crate) fn check_and_remove(&self, ino: InodeNum) -> bool {
        let list = Arc::clone(&self.list);
        let mut list = list.lock();
        let val = list.remove(&ino);
        val.is_some()
    }

    pub(crate) fn find(&self, ino: InodeNum) -> bool {
        let list = Arc::clone(&self.list);
        let list = list.lock();
        let val = list.get(&ino);
        val.is_some()
    }
}
