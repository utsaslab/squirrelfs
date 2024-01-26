// SPDX-License-Identifier: GPL-2.0

//! Rust file system sample.

use balloc::*;
use core::{ffi, ptr, sync::atomic::Ordering};
use defs::*;
use h_dir::*;
use h_inode::*;
use kernel::prelude::*;
use kernel::{bindings, c_str, fs, linked_list::List, rbtree::RBTree, types::ForeignOwnable};
use namei::*;
use pm::*;
use typestate::*;
use volatile::*;

mod balloc;
mod defs;
mod h_dir;
mod h_file;
mod h_inode;
mod h_symlink;
mod ioctl;
mod namei;
mod pm;
mod typestate;
mod volatile;

module_fs! {
    type: HayleyFs,
    name: "hayley_fs",
    author: "Hayley LeBlanc",
    description: "hayley_fs",
    license: "GPL",
}

struct HayleyFs;

#[vtable]
impl fs::Context<Self> for HayleyFs {
    type Data = Box<SbInfo>;

    kernel::define_fs_params! {Box<SbInfo>,
        {flag, "init", |s, v| {s.mount_opts.init = Some(v); Ok(())}},
        // TODO: let the user pass in a string
        {u64, "write_type", |s, v| {
            if v == 0 {
                pr_info!("using single page writes\n");
                s.mount_opts.write_type = Some(WriteType::SinglePage);
            } else if v == 1 {
                pr_info!("using runtime checked writes\n");
                s.mount_opts.write_type = Some(WriteType::RuntimeCheck);
            } else {
                pr_info!("using iterator writes\n");
                s.mount_opts.write_type = Some(WriteType::Iterator);
            }
            Ok(())
        }}
    }

    fn try_new() -> Result<Self::Data> {
        pr_info!("Context created");
        Ok(Box::try_new(SbInfo::new())?)
    }
}

impl fs::Type for HayleyFs {
    type Context = Self;
    type Data = Box<SbInfo>;
    type InodeOps = InodeOps;
    type DirOps = DirOps;
    const SUPER_TYPE: fs::Super = fs::Super::BlockDev;
    const NAME: &'static CStr = c_str!("hayleyfs");
    const FLAGS: i32 = fs::flags::REQUIRES_DEV | fs::flags::USERNS_MOUNT;

    fn fill_super(
        mut data: Box<SbInfo>,
        sb: fs::NewSuperBlock<'_, Self>,
    ) -> Result<&fs::SuperBlock<Self>> {
        pr_info!("fill super\n");

        // obtain virtual address and size of PM device
        data.get_pm_info(&sb)?;

        let sb = if let Some(true) = data.mount_opts.init {
            // initialize the file system
            // zero out PM device with non-temporal stores
            pr_info!("initializing file system...\n");

            let inode = unsafe { init_fs(&mut data, &sb)? };

            data.page_allocator = Option::<PerCpuPageAllocator>::new_from_range(
                // DATA_PAGE_START,
                data.get_data_pages_start_page(),
                // NUM_PAGE_DESCRIPTORS, // TODO: have this be the actual number of blocks
                if data.num_pages < data.num_blocks {
                    data.num_pages
                } else {
                    data.num_blocks
                },
                data.cpus,
            )?;

            data.inode_allocator = Some(InodeAllocator::new(ROOT_INO + 1, data.num_inodes)?);

            // initialize superblock
            let sb = sb.init(
                data,
                &fs::SuperParams {
                    magic: SUPER_MAGIC.try_into()?,
                    ..fs::SuperParams::DEFAULT
                },
            )?;

            // let inode_info = Box::try_new(HayleyFsDirInodeInfo::new(ROOT_INO))?;
            // root_ino.i_private = inode_info.into_foreign() as *mut _;
            pr_info!("initializing root from inode\n");
            sb.init_root_from_inode(inode)?
        } else {
            // remount
            pr_info!("mounting existing file system...\n");
            remount_fs(&mut data)?;

            // grab the persistent root inode up here to avoid ownership problems

            // initialize superblock
            let sb = sb.init(
                data,
                &fs::SuperParams {
                    magic: SUPER_MAGIC.try_into()?,
                    ..fs::SuperParams::DEFAULT
                },
            )?;

            let sbi = unsafe { &mut *((*sb.get_inner()).s_fs_info as *mut SbInfo) };

            let pi = sbi.get_inode_by_ino(ROOT_INO)?;

            // TODO: this is so janky. fix the kernel code so that this is cleaner
            // obtain the root inode we just created and fill it in with correct values
            let inode = unsafe { bindings::new_inode(sb.get_inner()) };
            if inode.is_null() {
                return Err(ENOMEM);
            }

            // fill in the new raw inode with info from our persistent inode
            // TODO: safer way of doing this
            unsafe {
                (*inode).i_ino = ROOT_INO;
                (*inode).i_size = bindings::le64_to_cpu(pi.get_size()).try_into()?;
                bindings::set_nlink(inode, bindings::le16_to_cpu(pi.get_link_count()).into());
                (*inode).i_mode = bindings::le16_to_cpu(pi.get_mode());
                (*inode).i_blocks = bindings::le64_to_cpu(pi.get_blocks());
                let uid = bindings::le32_to_cpu(pi.get_uid());
                let gid = bindings::le32_to_cpu(pi.get_gid());
                // TODO: https://elixir.bootlin.com/linux/latest/source/fs/ext2/inode.c#L1395 ?
                bindings::i_uid_write(inode, uid);
                bindings::i_gid_write(inode, gid);
                (*inode).i_atime = pi.get_atime();
                (*inode).i_ctime = pi.get_ctime();
                (*inode).i_mtime = pi.get_mtime();
                (*inode).i_blkbits =
                    bindings::blksize_bits(sbi.blocksize.try_into()?).try_into()?;
                // TODO: set the rest of the fields!
            }

            sb.init_root_from_inode(inode)?
        };
        pr_info!("fill_super done\n");

        Ok(sb)
    }

    fn put_super(sb: &fs::SuperBlock<Self>) {
        let sbi = unsafe { &mut *(sb.s_fs_info() as *mut SbInfo) };
        let persistent_sb = sbi.get_super_block_mut().unwrap();
        // TODO: safe wrapper around super block
        persistent_sb.set_clean_unmount(true);
        hayleyfs_flush_buffer(persistent_sb, SB_SIZE.try_into().unwrap(), true);
        pr_info!("PUT SUPERBLOCK\n");
    }

    fn statfs(sb: &fs::SuperBlock<Self>, buf: *mut bindings::kstatfs) -> Result<()> {
        // TODO: better support in rust/ so we don't have to do this all via raw pointers
        let sbi = unsafe { &*(sb.s_fs_info() as *const SbInfo) };
        unsafe {
            (*buf).f_type = SUPER_MAGIC;
            (*buf).f_bsize = sbi.blocksize.try_into()?;
            (*buf).f_blocks = sbi.num_blocks;
            if sbi.num_blocks < sbi.get_blocks_in_use() {
                pr_info!(
                    "WARNING: {:?} total blocks but {:?} blocks in use\n",
                    sbi.num_blocks,
                    sbi.get_blocks_in_use()
                );
            }
            (*buf).f_bfree = sbi.num_blocks - sbi.get_blocks_in_use();
            (*buf).f_bavail = sbi.num_blocks - sbi.get_blocks_in_use();
            (*buf).f_files = sbi.num_inodes;
            if sbi.num_inodes < sbi.get_inodes_in_use() {
                pr_info!(
                    "{:?} total inodes, {:?} inodes in use\n",
                    sbi.num_inodes,
                    sbi.get_inodes_in_use()
                );
            }
            (*buf).f_ffree = sbi.num_inodes - sbi.get_inodes_in_use();
            (*buf).f_namelen = MAX_FILENAME_LEN.try_into()?;
        }

        Ok(())
    }

    fn dirty_inode(inode: &fs::INode, _flags: i32) {
        let sb = inode.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let raw_pi = sbi.get_inode_by_ino(inode.i_ino()).unwrap();

        let inode_type = raw_pi.get_type();
        // TODO: use a new getter that returns a trait object so that we
        // don't need the match statement, since the branches are basically identical
        let atime = unsafe { bindings::current_time(inode.get_inner()) };
        match inode_type {
            InodeType::REG => {
                // pr_info!("evict reg\n");
                let inode = sbi
                    .get_init_reg_inode_by_vfs_inode(inode.get_inner())
                    .unwrap();
                inode.update_atime_consume(atime);
            }
            InodeType::DIR => match sbi.get_init_dir_inode_by_vfs_inode(inode.get_inner()) {
                Ok(inode) => inode.update_atime_consume(atime),
                Err(_) => {}
            },
            InodeType::SYMLINK => {
                // pr_info!("evict symlink\n");
                let inode = sbi
                    .get_init_reg_inode_by_vfs_inode(inode.get_inner())
                    .unwrap();
                inode.update_atime_consume(atime);
            }
            InodeType::NONE => {}
        }

        // TODO: DO THIS SAFELY WITH WRAPPERS
        // raw_pi.atime = unsafe { bindings::current_time(inode.get_inner()) };
        // unsafe { raw_pi.set_atime(bindings::current_time(inode.get_inner())) };
        hayleyfs_flush_buffer(raw_pi, core::mem::size_of::<HayleyFsInode>(), true);
    }

    fn evict_inode(inode: &fs::INode) {
        init_timing!(evict_inode_full);
        start_timing!(evict_inode_full);
        let sb = inode.i_sb();
        let ino = inode.i_ino();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let link_count = unsafe { (*inode.get_inner()).__bindgen_anon_1.i_nlink };

        // store the inode's private page list in the global tree so that we
        // can access it later if the inode comes back into the cache
        let mode = unsafe { (*inode.get_inner()).i_mode };
        if unsafe { bindings::S_ISREG(mode.try_into().unwrap()) }
            || unsafe { bindings::S_ISLNK(mode.try_into().unwrap()) }
        {
            init_timing!(evict_reg_inode_pages);
            start_timing!(evict_reg_inode_pages);
            if link_count == 0 {
                // free the inode and its pages
                // TODO: handle errors
                let pi = InodeWrapper::get_unlinked_ino(sbi, ino, inode.get_inner()).unwrap();
                let _pi = finish_unlink(sbi, pi).unwrap();

                end_timing!(EvictRegInodePages, evict_reg_inode_pages);
            } else {
                // using from_foreign should make sure the info structure is dropped here
                let inode_info = unsafe {
                    <Box<HayleyFsRegInodeInfo> as ForeignOwnable>::from_foreign(
                        (*inode.get_inner()).i_private,
                    )
                };
                unsafe { (*inode.get_inner()).i_private = core::ptr::null_mut() };
                let pages = inode_info.get_all_pages().unwrap();
                sbi.ino_data_page_tree.insert_inode(ino, pages).unwrap();
            }
        } else if unsafe { bindings::S_ISDIR(mode.try_into().unwrap()) } {
            init_timing!(evict_dir_inode_pages);
            start_timing!(evict_dir_inode_pages);
            if sbi.inodes_to_free.check_and_remove(ino) {
                // pr_info!("{:?} has already been freed\n", ino);
                let pi = sbi
                    .get_init_dir_inode_by_vfs_inode(inode.get_inner())
                    .unwrap();
                let pi = pi.set_unmap_page_state().unwrap();
                let _pi = rmdir_delete_pages(sbi, pi).unwrap();
            } else {
                // TODO: handle removed open directories?
                // using from_foreign should make sure the info structure is dropped here
                let inode_info = unsafe {
                    <Box<HayleyFsDirInodeInfo> as ForeignOwnable>::from_foreign(
                        (*inode.get_inner()).i_private,
                    )
                };
                unsafe { (*inode.get_inner()).i_private = core::ptr::null_mut() };
                let pages = inode_info.get_all_pages().unwrap();
                sbi.ino_dir_page_tree.insert_inode(ino, pages).unwrap();
            }
            end_timing!(EvictDirInodePages, evict_dir_inode_pages);
        }
        unsafe {
            bindings::truncate_inode_pages(&mut (*inode.get_inner()).i_data, 0);
            bindings::clear_inode(inode.get_inner());
        }

        // TODO: we might want to make deallocating inode numbers unsafe or
        // require proof that the inode in question has actually been
        // persistently freed
        // inode should only be deallocated if the inode's link count is actually 0
        if link_count == 0 {
            sbi.dealloc_ino(ino).unwrap();
        }
        end_timing!(EvictInodeFull, evict_inode_full);
    }

    // TODO: safety
    fn init_private(inode: *mut bindings::inode) -> Result<()> {
        let dentries = new_dentry_tree(ROOT_INO, ROOT_INO)?;
        let inode_info = Box::try_new(HayleyFsDirInodeInfo::new(ROOT_INO, dentries))?;
        unsafe { (*inode).i_private = inode_info.into_foreign() as *mut _ };
        Ok(())
    }
}

/// # Safety
/// This function is intentionally unsafe. It needs to be modified once the safe persistent object
/// APIs are in place
/// TODO: make safe
/// TODO: should it be NeedsRoot? ownership needs work if so
unsafe fn init_fs<T: fs::Type + ?Sized>(
    sbi: &mut SbInfo,
    sb: &fs::NewSuperBlock<'_, T, fs::NeedsInit>,
) -> Result<*mut bindings::inode> {
    pr_info!("init fs\n");

    unsafe {
        let data_page_start = sbi.get_data_pages_start_page() * HAYLEYFS_PAGESIZE;
        memset_nt(
            sbi.get_virt_addr() as *mut ffi::c_void,
            0,
            data_page_start.try_into()?, // only zero out regions that store metadata
            true,
        );

        // TODO: this is so janky. fix the kernel code so that this is cleaner
        // obtain the root inode we just created and fill it in with correct values
        let inode = bindings::new_inode(sb.get_inner());
        if inode.is_null() {
            return Err(ENOMEM);
        }

        let pi = HayleyFsInode::init_root_inode(sbi, inode)?;
        let super_block = HayleyFsSuperBlock::init_super_block(sbi.get_virt_addr(), sbi.get_size());

        hayleyfs_flush_buffer(pi, INODE_SIZE.try_into()?, false);
        hayleyfs_flush_buffer(super_block, SB_SIZE.try_into()?, true);

        // fill in the new raw inode with info from our persistent inode
        // TODO: safer way of doing this
        (*inode).i_ino = ROOT_INO;
        (*inode).i_size = bindings::le64_to_cpu(pi.get_size()).try_into()?;
        bindings::set_nlink(inode, bindings::le16_to_cpu(pi.get_link_count()).into());
        (*inode).i_mode = bindings::le16_to_cpu(pi.get_mode());
        (*inode).i_blocks = bindings::le64_to_cpu(pi.get_blocks());
        let uid = bindings::le32_to_cpu(pi.get_uid());
        let gid = bindings::le32_to_cpu(pi.get_gid());
        // TODO: https://elixir.bootlin.com/linux/latest/source/fs/ext2/inode.c#L1395 ?
        bindings::i_uid_write(inode, uid);
        bindings::i_gid_write(inode, gid);
        (*inode).i_atime = pi.get_atime();
        (*inode).i_ctime = pi.get_ctime();
        (*inode).i_mtime = pi.get_mtime();
        (*inode).i_blkbits = bindings::blksize_bits(sbi.blocksize.try_into()?).try_into()?;
        // TODO: set the rest of the fields!

        pr_info!("init fs done\n");
        Ok(inode)
    }
}

pub(crate) fn recover_rename<'a>(
    sbi: &SbInfo,
    d: DentryWrapper<'a, Clean, Recovering>,
) -> Result<()> {
    //NB: We are at steps 1-5 of Figure 3.

    if let Some(src) = d.rename_ptr(sbi)? {
        //NB: We are at steps 2 or 3 of Figure 3.
        // dst: has typestate InitRenamePointer
        //some d.rename_pointer => // d is a dst
        let dst: DentryWrapper<'a, Clean, InitRenamePointer> = d.into_init_rename(sbi)?;

        if dst.get_dentry_info().get_ino() != src.get_ino() {
            // NB: We are at step 2 or at step 4 of Figure 3.
            // Either rolling back step 2 or rolling ahead from step 4
            // requires unsetting the dst's rename pointer, leaving src
            // untouched for subsequent cleanup.

            // d.rename_pointer.inode != d.inode
            //   => d.typestate' = RecoverClearSetRptr

            //          make_dirty[d]
            //          no d.rename_ptr'

            // src: has typestate Recovering
            let src = DentryWrapper::src_to_recovering(&dst, src)?;
            dst.clear_rename_pointer(&src).flush().fence();
        } else {
            // NB: We are at step 3 of Figure 3.
            // src: has typestate Renamed
            let src = DentryWrapper::src_to_renamed(&dst, src)?;

            // Step 4: src -> ClearIno
            let src = src.clear_ino().flush().fence();

            // Step 5: dst -> ClearRenamePointer
            dst.clear_rename_pointer(&src).flush().fence();

            // Step 6: src -> Dealloc
            src.dealloc_dentry().flush().fence();
        }

        //We are either at step 1 or step 5 of Figure 3 - the rename has either been rolled
        //back or has completed.
    }
    // If d's rename pointer is not set, either it is a src in which
    // its inode has already been zeroed out (step 5); or, it is an
    // entry not participating in a rename during crash.  Ignore.

    Ok(())
}

fn recover_all_renames(sbi: &SbInfo) -> Result<()> {
    let begin: usize = sbi.get_data_pages_start_page().try_into()?;

    let pages = sbi.get_page_desc_table()?;
    for (i, desc) in pages
        .iter()
        .enumerate()
        .filter(|(_, p)| p.get_page_type() == PageType::DIR)
    {
        let desc: &DirPageHeader = desc.try_into()?;
        if !desc.is_initialized() {
            continue;
        }

        let page = i + begin;
        let dir_page_wrapper = DirPageWrapper::from_page_no(sbi, page.try_into()?)?;

        for dinfo in dir_page_wrapper.get_alloc_dentry_info(sbi)? {
            let dst = DentryWrapper::from_dinfo(dinfo)?;
            recover_rename(sbi, dst)?;
        }
    }

    Ok(())
}

fn remount_fs(sbi: &mut SbInfo) -> Result<()> {
    let mut alloc_inode_list: List<Box<LinkedInode>> = List::new();
    let mut num_alloc_inodes = 0;
    let mut alloc_page_list: List<Box<LinkedPage>> = List::new();
    let mut num_alloc_pages = 0;
    let mut init_dir_pages: RBTree<InodeNum, Vec<PageNum>> = RBTree::new();
    let mut init_data_pages: RBTree<InodeNum, Vec<PageNum>> = RBTree::new();
    let mut live_inode_list: List<Box<LinkedInode>> = List::new();
    let mut processed_live_inodes: RBTree<InodeNum, ()> = RBTree::new(); // rbtree as a set

    let mut orphaned_inodes: RBTree<InodeNum, ()> = RBTree::new();
    let mut orphaned_pages: RBTree<PageNum, ()> = RBTree::new();
    let mut orphaned_dentries: Vec<DentryInfo> = Vec::new(); // this is unlikely to get large enough to cause problems

    // these are used to determine if we have link count leaks
    let mut real_link_counts: RBTree<InodeNum, u16> = RBTree::new();
    let mut persistent_link_counts: RBTree<InodeNum, u16> = RBTree::new();

    // keeps track of maximum inode/page number in use to recreate the allocator
    let mut max_inode = 0;
    let mut max_page = sbi.get_data_pages_start_page();

    live_inode_list.push_back(Box::try_new(LinkedInode::new(1))?);

    pr_info!("remount fs\n");

    // 1. check the super block to make sure it is a valid fs and to fill in sbi
    let sbi_size = sbi.get_size();
    let sb = sbi.get_super_block()?;
    if sb.get_size() != sbi_size {
        pr_info!(
            "Expected device of size {:?} but found {:?}\n",
            sb.get_size(),
            sbi_size
        );
        return Err(EINVAL);
    }
    let recovering = !sb.get_clean_unmount();
    // let recovering = true;
    pr_info!("Recovering: {:?}\n", recovering);

    // 2. scan the inode table to determine which inodes are allocated
    // TODO: this scan will change significantly if the inode table is ever
    // not a single contiguous array
    let inode_table = sbi.get_inode_table()?;
    for (i, inode) in inode_table.iter().enumerate() {
        if !inode.is_free() && i != 0 {
            alloc_inode_list.push_back(Box::try_new(LinkedInode::new(i.try_into()?))?);
            persistent_link_counts.try_insert(i.try_into()?, inode.get_link_count())?;
            if recovering {
                // if this inode is not orphaned, we'll remove it during our scan later
                orphaned_inodes.try_insert(i.try_into()?, ())?;
            }
            sbi.inc_inodes_in_use();
            num_alloc_inodes += 1;
        }
    }
    if recovering {
        // root is always live so make sure it is not counted as an orphan
        orphaned_inodes.remove(&1);
    }

    // 3. scan the page descriptor table to determine which pages are in use
    let page_desc_table = sbi.get_page_desc_table()?;
    for (i, desc) in page_desc_table.iter().enumerate() {
        if !desc.is_free() {
            sbi.inc_blocks_in_use();
            if i > max_page.try_into()? {
                max_page = i.try_into()?;
            }
            let index: u64 = i.try_into()?;
            // add pages to maps that associate inodes with the pages they own
            // we don't add them to the index yet because an initialized page
            // is not necessarily live (right?)
            if desc.get_page_type() == PageType::DIR {
                let dir_desc: &DirPageHeader = desc.try_into()?;
                if dir_desc.is_initialized() {
                    let parent = dir_desc.get_ino();
                    if let Some(node) = init_dir_pages.get_mut(&parent) {
                        node.try_push(index + sbi.get_data_pages_start_page())?;
                    } else {
                        let mut vec = Vec::new();
                        vec.try_push(index + sbi.get_data_pages_start_page())?;
                        init_dir_pages.try_insert(parent, vec)?;
                    }
                }
            } else if desc.get_page_type() == PageType::DATA {
                let data_desc: &DataPageHeader = desc.try_into()?;
                if data_desc.is_initialized() {
                    let parent = data_desc.get_ino();
                    if let Some(node) = init_data_pages.get_mut(&parent) {
                        node.try_push(index + sbi.get_data_pages_start_page())?;
                    } else {
                        let mut vec = Vec::new();
                        vec.try_push(index + sbi.get_data_pages_start_page())?;
                        init_data_pages.try_insert(parent, vec)?;
                    }
                }
            }
            alloc_page_list.push_back(Box::try_new(LinkedPage::new(
                index + sbi.get_data_pages_start_page(),
            ))?);
            if recovering {
                // if this page is not orphaned we'll remove it from the set later
                orphaned_pages.try_insert(index + sbi.get_data_pages_start_page(), ())?;
            }
            num_alloc_pages += 1;
        }
    }
    if recovering {
        recover_all_renames(sbi)?;
    }

    // 4. scan the directory entries in live pages to determine which inodes are live
    // TODO: does this handle hard links correctly?
    let mut current_live_inode = live_inode_list.pop_front();
    while current_live_inode.is_some() {
        if let Some(current_live_inode) = current_live_inode {
            let live_inode = current_live_inode.get_ino();

            if let Some(lc) = real_link_counts.get_mut(&live_inode) {
                *lc += 1;
            } else {
                let live_ino_type = sbi.check_inode_type_by_inode_num(live_inode)?;
                if live_ino_type == InodeType::DIR {
                    // dirs always point to themselves, so the first dentry we find that
                    // refers to a dir inode is actually its second link
                    real_link_counts.try_insert(live_inode, 2)?;
                } else {
                    real_link_counts.try_insert(live_inode, 1)?;
                }
            }

            if live_inode > max_inode {
                max_inode = live_inode;
            }
            let owned_dir_pages = init_dir_pages.get(&live_inode);
            let owned_data_pages = init_data_pages.get(&live_inode);

            // iterate over pages owned by this inode, find valid dentries in those
            // pages, and add their inodes to the live inode list. also add the dir pages
            // to the volatile index
            if let Some(pages) = owned_dir_pages {
                for page in pages {
                    // page is live - remove it from the orphan set
                    if recovering {
                        orphaned_pages.remove(page);
                    }
                    let dir_page_wrapper = DirPageWrapper::from_page_no(sbi, *page)?;
                    let allocated_dentries = dir_page_wrapper.get_alloc_dentry_info(sbi)?;
                    // add live dentries to the index
                    for dentry in allocated_dentries {
                        // if the dentry is live (i.e. has an inode number), add its inode
                        // to the live inode list. otherwise, add it to the list of dentries
                        // to free. note that we do not have to worry about dentries in
                        // unallocated pages because we'll zero them out before we reuse the page
                        if dentry.get_ino() != 0 {
                            // count child directories towards link count
                            if dentry.is_dir() {
                                if let Some(lc) = real_link_counts.get_mut(&live_inode) {
                                    *lc += 1;
                                }
                            }
                            sbi.ino_dentry_tree.insert(live_inode, dentry)?;
                            live_inode_list
                                .push_back(Box::try_new(LinkedInode::new(dentry.get_ino()))?);
                            if recovering {
                                orphaned_inodes.remove(&dentry.get_ino());
                            }
                        } else {
                            if recovering {
                                orphaned_dentries.try_push(dentry)?;
                            }
                        }
                    }
                    let page_info = DirPageInfo::new(dir_page_wrapper.get_page_no());
                    sbi.ino_dir_page_tree.insert_one(live_inode, page_info)?;
                }
            }

            // add data page to the volatile index
            if let Some(pages) = owned_data_pages {
                // we remove live pages from the orphan set in build_tree()
                let pages = build_tree(sbi, pages, &mut orphaned_pages)?;
                sbi.ino_data_page_tree.insert_inode(live_inode, pages)?;
            }

            processed_live_inodes.try_insert(live_inode, ())?;
        }
        current_live_inode = live_inode_list.pop_front()
    }
    if recovering {
        free_orphans(
            sbi,
            orphaned_inodes,
            orphaned_pages,
            orphaned_dentries,
            &mut persistent_link_counts,
        )?;
        fix_link_counts(sbi, persistent_link_counts, real_link_counts)?;
    }

    sbi.page_allocator = Option::<PerCpuPageAllocator>::new_from_alloc_vec(
        alloc_page_list,
        num_alloc_pages,
        sbi.get_data_pages_start_page(),
        if sbi.num_pages < sbi.num_blocks {
            sbi.num_pages
        } else {
            sbi.num_blocks
        },
        sbi.cpus,
    )?;
    sbi.inode_allocator = Some(RBInodeAllocator::new_from_alloc_vec(
        alloc_inode_list,
        num_alloc_inodes,
        ROOT_INO + 1,
        sbi.num_inodes,
    )?);
    // reborrow the super block to appease the borrow checker
    let sb = sbi.get_super_block_mut().unwrap();
    sb.set_clean_unmount(false);
    hayleyfs_flush_buffer(sb, SB_SIZE.try_into()?, true);
    Ok(())
}

fn build_tree(
    sbi: &SbInfo,
    input_vec: &Vec<PageNum>,
    orphaned_pages: &mut RBTree<PageNum, ()>,
) -> Result<RBTree<u64, PageNum>> {
    let mut output_tree = RBTree::new();

    for page_no in input_vec {
        orphaned_pages.remove(page_no);
        let data_page_wrapper = DataPageWrapper::from_page_no(sbi, *page_no)?;
        let offset = data_page_wrapper.get_offset();
        output_tree.try_insert(offset, *page_no)?;
    }

    Ok(output_tree)
}

fn free_orphans(
    sbi: &SbInfo,
    orphaned_inodes: RBTree<InodeNum, ()>,
    orphaned_pages: RBTree<PageNum, ()>,
    orphaned_dentries: Vec<DentryInfo>,
    persistent_link_counts: &mut RBTree<InodeNum, u16>,
) -> Result<()> {
    // for each orphaned object, follow its regular deallocation process
    // TODO: reconcile what you do here with Nathan's recovery typestates
    // TODO: parallelize
    // TODO: don't flush after every operation
    // TODO: finalization check to make sure everything is clean

    // 1. inodes
    // we don't have any indexed information about this inode, so
    // we just want to persistently deallocate it - skip the other parts
    // that occur in unlink/rmdir to deal with pages
    // we also remove persistent link counts for orphaned inodes so that we don't
    // consider them during the link count fixup step
    for (iter, _) in orphaned_inodes.iter() {
        persistent_link_counts.remove(iter);
        let pi = unsafe { InodeWrapper::get_recovery_inode(sbi, *iter)? };
        let _pi = pi.recovery_dealloc().flush().fence();
    }

    // 2. pages
    // we use static data page wrappers here since the DataPageListWrapper
    // abstraction doesn't really make sense here
    for (iter, _) in orphaned_pages.iter() {
        // note: it is important to check for data pages FIRST because if a page doesn't have a type,
        // its offset field could still be set, so we need to make sure it is freed
        if let Ok(page) = unsafe { DataPageListWrapper::get_recovery_page(sbi, *iter) } {
            let _page = page.recovery_dealloc(sbi)?.fence();
        } else if let Ok(page) = unsafe { DirPageListWrapper::get_recovery_page(sbi, *iter) } {
            let _page = page.recovery_dealloc(sbi)?.fence();
        } else {
            return Err(EINVAL);
        }
    }

    // 3. dentries
    for dentry in orphaned_dentries {
        let dentry = unsafe { DentryWrapper::get_recovery_dentry(&dentry)? };
        let _dentry = dentry.recovery_dealloc().flush().fence();
    }

    Ok(())
}

fn fix_link_counts(
    sbi: &SbInfo,
    persistent_link_counts: RBTree<InodeNum, u16>,
    real_link_counts: RBTree<InodeNum, u16>,
) -> Result<()> {
    // TODO: can we do this faster than iterating over all of the inodes?
    // we will again assume that all inodes are regular inodes since it
    // doesn't actually matter what type they are here
    for (ino, persistent_lc) in persistent_link_counts.iter() {
        if let Some(real_lc) = real_link_counts.get(ino) {
            if persistent_lc != real_lc {
                let pi = unsafe { InodeWrapper::get_too_many_links_inode(sbi, *ino, *real_lc)? };
                let _pi = pi.recovery_dec_link(*real_lc).flush().fence();
            }
        } else {
            return Err(EINVAL);
        }
    }
    Ok(())
}

pub(crate) trait PmDevice {
    fn get_pm_info(&mut self, sb: &fs::NewSuperBlock<'_, HayleyFs>) -> Result<()>;
}

impl PmDevice for SbInfo {
    fn get_pm_info(&mut self, sb: &fs::NewSuperBlock<'_, HayleyFs>) -> Result<()> {
        // obtain the dax_device struct
        let dax_dev = sb.get_dax_dev()?;

        let mut virt_addr: *mut ffi::c_void = ptr::null_mut();

        // obtain virtual address and size of the dax device
        // SAFETY: The type invariant of `sb` guarantees that `sb.sb` is the only pointer to
        // a newly-allocated superblock. The safety condition of `get_dax_dev` guarantees
        // that `dax_dev` is the only active pointer to the associated `dax_device`, so it is
        // safe to mutably dereference it.
        let num_blocks = unsafe {
            bindings::dax_direct_access(
                dax_dev,
                0,
                (u64::MAX / HAYLEYFS_PAGESIZE).try_into()?,
                bindings::dax_access_mode_DAX_ACCESS,
                &mut virt_addr,
                ptr::null_mut(),
            )
        };

        unsafe {
            self.set_dax_dev(dax_dev);
            self.set_virt_addr(virt_addr as *mut u8);
        }
        let pgsize_i64: i64 = HAYLEYFS_PAGESIZE.try_into()?;
        self.size = num_blocks * pgsize_i64;
        self.num_blocks = num_blocks.try_into()?;

        let device_size: u64 = self.size.try_into()?;
        let pages_per_inode = 8;
        let bytes_per_inode = pages_per_inode * HAYLEYFS_PAGESIZE;
        pr_info!("device size: {:?}\n", device_size);
        let num_inodes: u64 = device_size / bytes_per_inode;
        let inode_table_size = num_inodes * INODE_SIZE;
        let inode_table_pages = (inode_table_size / HAYLEYFS_PAGESIZE) + 1; // account for possible rounding down
        let num_pages = num_inodes * pages_per_inode;
        let page_desc_table_size = num_pages * PAGE_DESCRIPTOR_SIZE;
        let page_desc_table_pages = (page_desc_table_size / HAYLEYFS_PAGESIZE) + 1;
        pr_info!(
            "size of inode table (MB): {:?}\n",
            inode_table_size / (1024 * 1024)
        );
        pr_info!(
            "size of page descriptor table (MB): {:?}\n",
            page_desc_table_size / (1024 * 1024)
        );
        pr_info!("number of inodes: {:?}\n", num_inodes);
        pr_info!("number of pages: {:?}\n", num_pages);

        self.num_inodes = num_inodes;
        self.inode_table_size = inode_table_size;
        self.inode_table_pages = inode_table_pages;
        self.num_pages = num_pages;
        self.page_desc_table_size = page_desc_table_size;
        self.page_desc_table_pages = page_desc_table_pages;

        self.blocks_in_use.store(
            inode_table_pages + page_desc_table_pages + 1,
            Ordering::SeqCst,
        );

        Ok(())
    }
}
