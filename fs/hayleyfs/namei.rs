use crate::balloc::*;
use crate::defs::*;
use crate::h_dir::*;
use crate::h_file::*;
use crate::h_inode::*;
use crate::h_symlink::*;
use crate::pm::*;
use crate::typestate::*;
use crate::volatile::*;
use crate::{
    end_timing, fence_all, fence_all_vecs, fence_obj, fence_vec, init_timing, start_timing,
};

use core::sync::atomic::Ordering;
use kernel::prelude::*;
use kernel::{
    bindings, dir, error, file, fs, inode, io_buffer::IoBufferReader, rbtree::RBTree, symlink,
    user_ptr::UserSlicePtr, ForeignOwnable,
};

// TODO: should use .borrow() to get the SbInfo structure out?

pub(crate) struct InodeOps;
#[vtable]
impl inode::Operations for InodeOps {
    fn lookup(
        dir: &fs::INode,
        dentry: &mut fs::DEntry,
        _flags: u32,
    ) -> Result<Option<*mut bindings::inode>> {
        // TODO: handle flags
        // TODO: reorganize so that system call logic is separate from
        // conversion from raw pointers

        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
        let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
        let parent_inode_info = parent_inode.get_inode_info()?;
        move_dir_inode_tree_to_map(sbi, &parent_inode_info)?;
        let result = parent_inode_info.lookup_dentry(dentry.d_name())?;
        if let Some(dentry_info) = result {
            // the dentry exists in the specified directory
            Ok(Some(hayleyfs_iget(
                sb,
                sbi,
                dentry_info.get_ino(),
                parent_inode_info.get_ino(),
            )?))
        } else {
            // the dentry does not exist in this directory
            Ok(None)
        }
    }

    fn create(
        mnt_idmap: *mut bindings::mnt_idmap,
        dir: &mut fs::INode,
        dentry: &fs::DEntry,
        umode: bindings::umode_t,
        excl: bool,
    ) -> Result<i32> {
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let (_new_dentry, new_inode) = hayleyfs_create(sbi, dir, dentry, umode, excl)?;

        let vfs_inode = new_vfs_inode(
            sb,
            sbi,
            mnt_idmap,
            dir,
            dentry,
            &new_inode,
            umode,
            None,
            dir.i_ino(),
        )?;
        unsafe { insert_vfs_inode(vfs_inode, dentry)? };
        Ok(0)
    }

    fn link(old_dentry: &fs::DEntry, dir: &mut fs::INode, dentry: &fs::DEntry) -> Result<i32> {
        let inode = old_dentry.d_inode();
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        unsafe { bindings::ihold(inode) };

        let result = hayleyfs_link(sbi, old_dentry, dir, dentry);

        if result.is_ok() {
            // TODO: safe wrappers
            unsafe {
                bindings::d_instantiate(dentry.get_inner(), old_dentry.d_inode());
            }
        }

        if let Err(e) = result {
            unsafe { bindings::iput(inode) };
            Err(e)
        } else {
            Ok(0)
        }
    }

    fn mkdir(
        mnt_idmap: *mut bindings::mnt_idmap,
        dir: &mut fs::INode,
        dentry: &fs::DEntry,
        umode: bindings::umode_t,
    ) -> Result<i32> {
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let (_new_dentry, _parent_inode, new_inode) = hayleyfs_mkdir(sbi, dir, dentry, umode)?;

        dir.inc_nlink();

        let vfs_inode = new_vfs_inode(
            sb,
            sbi,
            mnt_idmap,
            dir,
            dentry,
            &new_inode,
            umode,
            None,
            dir.i_ino(),
        )?;
        unsafe { insert_vfs_inode(vfs_inode, dentry)? };
        Ok(0)
    }

    fn rmdir(dir: &mut fs::INode, dentry: &fs::DEntry) -> Result<i32> {
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        // TODO: is there a nice Result function you could use to remove the match?
        let result = hayleyfs_rmdir(sbi, dir, dentry);
        match result {
            Ok(_) => Ok(0),
            Err(e) => Err(e),
        }
    }

    fn rename(
        _mnt_idmap: *const bindings::mnt_idmap,
        old_dir: &mut fs::INode,
        old_dentry: &fs::DEntry,
        new_dir: &mut fs::INode,
        new_dentry: &fs::DEntry,
        flags: u32,
    ) -> Result<()> {
        if flags != 0 {
            return Err(ENOTSUPP);
        }

        let sb = old_dir.i_sb();
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let result = hayleyfs_rename(sbi, old_dir, old_dentry, new_dir, new_dentry, flags);

        match result {
            Ok(_) => Ok(()),
            Err(e) => Err(e),
        }
    }

    // TODO: if this unlink results in its dir page being emptied, we should
    // deallocate the dir page (at some point)
    fn unlink(dir: &mut fs::INode, dentry: &fs::DEntry) -> Result<()> {
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let result = hayleyfs_unlink(sbi, dir, dentry);
        if let Err(e) = result {
            return Err(e);
        }

        Ok(())
    }

    fn symlink(
        mnt_idmap: *mut bindings::mnt_idmap,
        dir: &fs::INode,
        dentry: &fs::DEntry,
        symname: *const core::ffi::c_char,
    ) -> Result<()> {
        let sb = dir.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };

        let perm: u16 = 0777;
        let flag: u16 = bindings::S_IFLNK.try_into().unwrap();
        let mode: u16 = flag | perm; // TODO: correct mode

        let result = hayleyfs_symlink(sbi, dir, dentry, symname, mode);
        if let Err(e) = result {
            return Err(e);
        } else if let Ok((mut new_inode, _, _new_page, pi_info)) = result {
            let vfs_inode = new_vfs_inode(
                sb,
                sbi,
                mnt_idmap,
                dir,
                dentry,
                &new_inode,
                mode,
                Some(pi_info),
                dir.i_ino(),
            )?;
            new_inode.set_vfs_inode(vfs_inode)?;
            // let pi_info = new_inode.get_inode_info()?;
            // pi_info.insert_pages(&new_page, new_page.len())?;
            unsafe { insert_vfs_inode(vfs_inode, dentry)? };
            Ok(())
        } else {
            unreachable!();
        }
    }

    fn setattr(
        mnt_idmap: *mut bindings::mnt_idmap,
        dentry: &fs::DEntry,
        iattr: *mut bindings::iattr,
    ) -> Result<()> {
        let inode: &mut fs::INode = unsafe { &mut *dentry.d_inode().cast() };

        let truncate = unsafe {
            let ret = bindings::setattr_prepare(mnt_idmap, dentry.get_inner(), iattr);
            if ret < 0 {
                return Err(error::Error::from_kernel_errno(ret));
            }
            bindings::setattr_copy(mnt_idmap, inode.get_inner(), iattr);

            (*iattr).ia_valid & bindings::ATTR_SIZE != 0 && (*iattr).ia_size != inode.i_size_read()
        };

        if truncate {
            let sb = inode.i_sb();
            let fs_info_raw = unsafe { (*sb).s_fs_info };
            // TODO: it's probably not safe to just grab s_fs_info and
            // get a mutable reference to one of the dram indexes
            let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
            let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
            hayleyfs_truncate(sbi, pi, unsafe { (*iattr).ia_size })
        } else {
            // TODO: should be enotsupp?
            Ok(())
        }
    }
}

// TODO: shouldn't really be generic but HayleyFs isn't accessible here
/// hayleyfs_iget is for obtaining the VFS inode for an inode that already
/// exists persistently. new_vfs_inode is for setting up VFS inodes for
/// completely new inodes
pub(crate) fn hayleyfs_iget(
    sb: *mut bindings::super_block,
    sbi: &SbInfo,
    ino: InodeNum,
    parent_ino: InodeNum,
) -> Result<*mut bindings::inode> {
    init_timing!(inode_exists);
    start_timing!(inode_exists);
    init_timing!(full_iget);
    start_timing!(full_iget);
    // obtain an inode from VFS
    let inode = unsafe { bindings::iget_locked(sb, ino) };
    if inode.is_null() {
        return Err(ENOMEM);
    }
    // if we don't need to set up the inode, just return it
    let i_new: u64 = bindings::I_NEW.into();

    unsafe {
        if (*inode).i_state & i_new == 0 {
            end_timing!(IgetInodeExists, inode_exists);
            return Ok(inode);
        }
    }

    // set up the new inode
    let pi = sbi.get_inode_by_ino(ino)?;

    unsafe {
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
        (*inode).i_flags |= bindings::S_DAX;
        // TODO: set the rest of the fields!
    }

    let inode_type = pi.get_type();
    match inode_type {
        InodeType::REG => unsafe {
            init_timing!(init_reg_inode);
            start_timing!(init_reg_inode);
            (*inode).i_op = inode::OperationsVtable::<InodeOps>::build();
            (*inode).__bindgen_anon_3.i_fop = file::OperationsVtable::<Adapter, FileOps>::build();

            let pages = sbi.ino_data_page_tree.remove(ino);
            // if the inode has any pages associated with it, remove them from the
            // global tree and put them in this inode's i_private
            if let Some(pages) = pages {
                let inode_info = Box::try_new(HayleyFsRegInodeInfo::new_from_tree(
                    ino,
                    pages,
                    pi.get_blocks(),
                ))?;
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            } else {
                let inode_info = Box::try_new(HayleyFsRegInodeInfo::new(ino))?;
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            }
            end_timing!(InitRegInode, init_reg_inode);
        },
        InodeType::DIR => unsafe {
            init_timing!(init_dir_inode);
            start_timing!(init_dir_inode);
            (*inode).i_op = inode::OperationsVtable::<InodeOps>::build();
            (*inode).__bindgen_anon_3.i_fop = dir::OperationsVtable::<DirOps>::build();

            let pages = sbi.ino_dir_page_tree.remove(ino);
            // if the inode has any pages associated with it, remove them from the
            // global tree and put them in this inode's i_private
            if let Some(pages) = pages {
                let dentries = sbi.ino_dentry_tree.remove(ino);
                let inode_info = if let Some(mut dentries) = dentries {
                    // we need to add the . and .. dentries to the list since they are not
                    // stored durably
                    add_dot_dentries(ino, parent_ino, &mut dentries)?;
                    Box::try_new(HayleyFsDirInodeInfo::new_from_tree(ino, pages, dentries))?
                } else {
                    let dentries = new_dentry_tree(ino, parent_ino)?;
                    Box::try_new(HayleyFsDirInodeInfo::new_from_tree(ino, pages, dentries))?
                };
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            } else {
                let dentries = new_dentry_tree(ino, parent_ino)?;
                let inode_info = Box::try_new(HayleyFsDirInodeInfo::new(ino, dentries))?;
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            }
            end_timing!(InitDirInode, init_dir_inode);
        },
        InodeType::SYMLINK => unsafe {
            (*inode).i_op = symlink::OperationsVtable::<SymlinkOps>::build();
            let pages = sbi.ino_data_page_tree.remove(ino);
            // if the inode has any pages associated with it, remove them from the
            // global tree and put them in this inode's i_private
            if let Some(pages) = pages {
                let inode_info = Box::try_new(HayleyFsRegInodeInfo::new_from_tree(
                    ino,
                    pages,
                    pi.get_blocks(),
                ))?;
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            } else {
                let inode_info = Box::try_new(HayleyFsRegInodeInfo::new(ino))?;
                (*inode).i_private = inode_info.into_foreign() as *mut _;
            }
        },
        InodeType::NONE => {
            pr_info!("Inode {:?} has type NONE\n", ino);
            panic!("Inode type is NONE")
        }
    }
    unsafe { bindings::unlock_new_inode(inode) };
    end_timing!(FullIget, full_iget);
    Ok(inode)
}

// TODO: add type
/// new_vfs_inode is used to set up the VFS inode for a completely new HayleyFsInode.
/// if the HayleyFsInode already exists, you should use hayleyfs_iget
fn new_vfs_inode<'a, Type>(
    sb: *mut bindings::super_block,
    sbi: &SbInfo,
    mnt_idmap: *mut bindings::mnt_idmap,
    dir: &fs::INode,
    _dentry: &fs::DEntry,
    new_inode: &InodeWrapper<'a, Clean, Complete, Type>,
    umode: bindings::umode_t,
    inode_info: Option<Box<HayleyFsRegInodeInfo>>,
    parent_ino: InodeNum,
) -> Result<*mut bindings::inode> {
    init_timing!(full_vfs_inode);
    start_timing!(full_vfs_inode);
    // set up VFS structures
    let vfs_inode = unsafe { &mut *(bindings::new_inode(sb) as *mut bindings::inode) };

    // TODO: could this be moved out to the callback?
    unsafe {
        bindings::inode_init_owner(mnt_idmap, vfs_inode, dir.get_inner(), umode);
    }

    let ino = new_inode.get_ino();
    vfs_inode.i_ino = ino;
    // we don't have access to ZST Type, but inode wrapper constructors check types
    // so we can rely on these being correct
    let inode_type = new_inode.get_type();
    match inode_type {
        InodeType::REG => {
            init_timing!(init_reg_vfs_inode);
            start_timing!(init_reg_vfs_inode);
            vfs_inode.i_mode = umode;
            // initialize the DRAM info and save it in the private pointer
            let inode_info = Box::try_new(HayleyFsRegInodeInfo::new(ino))?;
            vfs_inode.i_private = inode_info.into_foreign() as *mut _;
            unsafe {
                vfs_inode.i_op = inode::OperationsVtable::<InodeOps>::build();
                vfs_inode.__bindgen_anon_3.i_fop =
                    file::OperationsVtable::<Adapter, FileOps>::build();
                bindings::set_nlink(vfs_inode, 1);
            }
            end_timing!(InitRegVfsInode, init_reg_vfs_inode);
        }
        InodeType::DIR => {
            init_timing!(init_dir_vfs_inode);
            start_timing!(init_dir_vfs_inode);
            vfs_inode.i_mode = umode | bindings::S_IFDIR as u16;
            // initialize the DRAM info and save it in the private pointer

            let dentries = new_dentry_tree(ino, parent_ino)?;
            let inode_info = Box::try_new(HayleyFsDirInodeInfo::new(ino, dentries))?;
            vfs_inode.i_private = inode_info.into_foreign() as *mut _;
            unsafe {
                vfs_inode.i_op = inode::OperationsVtable::<InodeOps>::build();
                vfs_inode.__bindgen_anon_3.i_fop = dir::OperationsVtable::<DirOps>::build();
                bindings::set_nlink(vfs_inode, 2);
            }
            end_timing!(InitDirVfsInode, init_dir_vfs_inode);
        }
        InodeType::SYMLINK => {
            vfs_inode.i_mode = umode;
            // initialize the DRAM info and save it in the private pointer
            // let inode_info = Box::try_new(HayleyFsRegInodeInfo::new(ino))?;
            let inode_info = if let Some(inode_info) = inode_info {
                inode_info
            } else {
                pr_info!("ERROR: symlink must provide inode info\n");
                return Err(EINVAL);
            };
            vfs_inode.i_private = inode_info.into_foreign() as *mut _;
            unsafe {
                vfs_inode.i_op = symlink::OperationsVtable::<SymlinkOps>::build();
                bindings::set_nlink(vfs_inode, 1);
            }
        }
        InodeType::NONE => panic!("Inode type is none"),
    }

    vfs_inode.i_mtime = new_inode.get_mtime();
    vfs_inode.i_ctime = new_inode.get_ctime();
    vfs_inode.i_atime = new_inode.get_atime();
    vfs_inode.i_size = new_inode.get_size().try_into()?;
    vfs_inode.i_blocks = new_inode.get_blocks();
    vfs_inode.i_blkbits = unsafe { bindings::blksize_bits(sbi.blocksize.try_into()?).try_into()? };
    vfs_inode.i_flags |= bindings::S_DAX;

    unsafe {
        let uid = bindings::le32_to_cpu(new_inode.get_uid());
        let gid = bindings::le32_to_cpu(new_inode.get_gid());
        // TODO: https://elixir.bootlin.com/linux/latest/source/fs/ext2/inode.c#L1395 ?
        bindings::i_uid_write(vfs_inode, uid);
        bindings::i_gid_write(vfs_inode, gid);
        // let ret = bindings::insert_inode_locked(vfs_inode);
        // if ret < 0 {
        //     // TODO: from_kernel_errno should really only be pub(crate)
        //     // probably because you aren't supposed to directly call C fxns from modules
        //     // but there's no good code to call this stuff from the kernel yet
        //     // once there is, return from_kernel_errno to pub(crate)
        //     return Err(error::Error::from_kernel_errno(ret));
        // }
        // bindings::d_instantiate(dentry.get_inner(), vfs_inode);
        // bindings::unlock_new_inode(vfs_inode);
    }
    end_timing!(FullVfsInode, full_vfs_inode);
    Ok(vfs_inode)
}

pub(crate) fn new_dentry_tree(
    ino: InodeNum,
    parent_ino: InodeNum,
) -> Result<RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>> {
    let mut dentries = RBTree::new();
    add_dot_dentries(ino, parent_ino, &mut dentries)?;
    Ok(dentries)
}

pub(crate) fn add_dot_dentries(
    ino: InodeNum,
    parent_ino: InodeNum,
    dentries: &mut RBTree<[u8; MAX_FILENAME_LEN], DentryInfo>,
) -> Result<()> {
    let dot: &CStr = CStr::from_bytes_with_nul(".\0".as_bytes())?;
    let dotdot = CStr::from_bytes_with_nul("..\0".as_bytes())?;
    let mut dot_array = [0; MAX_FILENAME_LEN];
    let mut dotdot_array = [0; MAX_FILENAME_LEN];
    dot_array[..dot.len()].copy_from_slice(dot.as_bytes());
    dotdot_array[..dotdot.len()].copy_from_slice(dotdot.as_bytes());
    dentries.try_insert(dot_array, DentryInfo::new(ino, None, dot_array, true))?;
    dentries.try_insert(
        dotdot_array,
        DentryInfo::new(parent_ino, None, dotdot_array, true),
    )?;
    Ok(())
}

unsafe fn insert_vfs_inode(vfs_inode: *mut bindings::inode, dentry: &fs::DEntry) -> Result<()> {
    // TODO: check that the inode is fully set up and doesn't already exist
    // until then this is unsafe
    unsafe {
        let ret = bindings::insert_inode_locked(vfs_inode);
        if ret < 0 {
            // TODO: from_kernel_errno should really only be pub(crate)
            // probably because you aren't supposed to directly call C fxns from modules
            // but there's no good code to call this stuff from the kernel yet
            // once there is, return from_kernel_errno to pub(crate)
            return Err(error::Error::from_kernel_errno(ret));
        }
        bindings::d_instantiate(dentry.get_inner(), vfs_inode);
        bindings::unlock_new_inode(vfs_inode);
    }
    Ok(())
}

fn hayleyfs_create<'a>(
    sbi: &'a SbInfo,
    dir: &mut fs::INode,
    dentry: &fs::DEntry,
    umode: bindings::umode_t,
    _excl: bool,
) -> Result<(
    DentryWrapper<'a, Clean, Complete>,
    InodeWrapper<'a, Clean, Complete, RegInode>,
)> {
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    dir.update_ctime_and_mtime();
    let parent_inode = parent_inode
        .update_ctime_and_mtime(dir.get_mtime())
        .flush()
        .fence();
    let pd = get_free_dentry(sbi, &parent_inode)?;
    let pd = pd.set_name(dentry.d_name(), false)?.flush().fence();
    let (dentry, inode) = init_dentry_with_new_reg_inode(sbi, dir, pd, umode)?;
    let parent_inode_info = parent_inode.get_inode_info()?;
    dentry.index(&parent_inode_info)?;

    Ok((dentry, inode))
}

fn hayleyfs_link<'a>(
    sbi: &'a mut SbInfo,
    old_dentry: &fs::DEntry,
    dir: &fs::INode,
    dentry: &fs::DEntry,
) -> Result<(
    DentryWrapper<'a, Clean, Complete>,
    InodeWrapper<'a, Clean, Complete, RegInode>,
)> {
    // old dentry is the dentry for the target name,
    // dir is the PARENT inode,
    // dentry is the dentry for the new name

    // first, obtain the inode that's getting the link from old_dentry
    // TODO: move unsafe cast to the wrapper
    let inode: &mut fs::INode = unsafe { &mut *old_dentry.d_inode().cast() };
    let target_inode = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
    inode.update_ctime();
    let target_inode = target_inode.update_ctime(inode.get_atime()).flush().fence();

    let target_inode = target_inode.inc_link_count()?.flush().fence();
    // TODO: this should really go in the caller, but if another part of this function fails
    // then the vfs and persistent inodes will have link counts that are out of sync.
    // should have some kind of rollback mechanism in case of failure.
    unsafe {
        let ctime = bindings::current_time(inode.get_inner());
        (*inode.get_inner()).i_ctime = ctime;
        bindings::inc_nlink(inode.get_inner());
    }
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    let pd = get_free_dentry(sbi, &parent_inode)?;
    let pd = pd.set_name(dentry.d_name(), false)?.flush().fence();

    let (pd, target_inode) = pd.set_file_ino(target_inode);
    let pd = pd.flush().fence();

    let parent_inode_info = parent_inode.get_inode_info()?;
    pd.index(&parent_inode_info)?;

    Ok((pd, target_inode))
}

fn hayleyfs_mkdir<'a>(
    sbi: &'a SbInfo,
    dir: &mut fs::INode,
    dentry: &fs::DEntry,
    mode: u16,
) -> Result<(
    DentryWrapper<'a, Clean, Complete>,
    InodeWrapper<'a, Clean, Complete, DirInode>, // parent
    InodeWrapper<'a, Clean, Complete, DirInode>, // new inode
)> {
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    dir.update_ctime_and_mtime();
    let parent_inode = parent_inode
        .update_ctime_and_mtime(dir.get_mtime())
        .flush()
        .fence();
    // let parent_inode_info = parent_inode.get_inode_info()?;
    let parent_inode = parent_inode.inc_link_count()?.flush().fence();
    let pd = get_free_dentry(sbi, &parent_inode)?;
    let pd = pd.set_name(dentry.d_name(), true)?.flush().fence();
    let (dentry, parent, inode) = init_dentry_with_new_dir_inode(sbi, dir, pd, parent_inode, mode)?;
    let parent_inode_info = parent.get_inode_info()?;
    dentry.index(&parent_inode_info)?;
    Ok((dentry, parent, inode))
}

fn hayleyfs_rmdir<'a>(
    sbi: &'a SbInfo,
    dir: &mut fs::INode,
    dentry: &fs::DEntry,
) -> Result<(
    InodeWrapper<'a, Clean, Complete, DirInode>, // target
    InodeWrapper<'a, Clean, DecLink, DirInode>,  // parent
    DentryWrapper<'a, Clean, Free>,
)> {
    let inode = dentry.d_inode();
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    dir.update_ctime_and_mtime();
    let parent_inode = parent_inode
        .update_ctime_and_mtime(dir.get_mtime())
        .flush()
        .fence();
    let parent_inode_info = parent_inode.get_inode_info()?;
    let dentry_info = parent_inode_info.lookup_dentry(dentry.d_name())?;
    match dentry_info {
        Some(dentry_info) => {
            // check if the directory we are trying to delete is empty
            let pi = sbi.get_init_dir_inode_by_vfs_inode(inode)?;
            {
                let delete_dir_info = pi.get_inode_info()?;
                if delete_dir_info.has_dentries() {
                    return Err(ENOTEMPTY);
                }
            }

            // if it is, start deleting
            let pd = DentryWrapper::get_init_dentry(dentry_info)?;

            // clear dentry inode
            parent_inode_info.delete_dentry(dentry_info)?;
            let pd = pd.clear_ino().flush().fence();

            // decrement parent link count
            // we should be able to reuse the regular dec_link_count function (it's a different
            // transition in Alloy). According to Alloy we can wait for the next fence.
            // but that is hard to coordinate with the vectors, so we just do an extra
            // let parent_pi = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
            let parent_inode = parent_inode.dec_link_count(&pd)?.flush().fence();

            let pi = pi.dec_link_count(&pd)?.flush().fence();

            let pi = pi.set_unmap_page_state()?;

            let pi = rmdir_delete_pages(sbi, pi)?;

            // deallocate the dentry
            let pd = pd.dealloc_dentry().flush();

            let (pi, pd) = fence_all!(pi, pd);

            // if the page that the freed dentry belongs to is now empty, free it
            let parent_page = pd.try_dealloc_parent_page(sbi);
            if let Ok(parent_page) = parent_page {
                // have to grab parent_inode_info again because the borrow checker thinks
                // that it is owned by parent_inode, which has been moved
                let parent_inode_info = parent_inode.get_inode_info()?;
                let parent_page = parent_page.unmap().flush().fence();
                let parent_page = parent_page.dealloc(sbi).flush().fence();
                sbi.page_allocator.dealloc_dir_page(&parent_page)?;
                parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
            }

            unsafe {
                bindings::clear_nlink(inode);
                bindings::drop_nlink(dir.get_inner());
            }
            Ok((pi, parent_inode, pd))
        }
        None => Err(ENOENT),
    }
}

pub(crate) fn rmdir_delete_pages<'a>(
    sbi: &'a SbInfo,
    // delete_dir_info: &HayleyFsDirInodeInfo,
    pi: InodeWrapper<'a, Clean, UnmapPages, DirInode>,
) -> Result<InodeWrapper<'a, InFlight, Complete, DirInode>> {
    match sbi.mount_opts.write_type {
        Some(WriteType::Iterator) | None => {
            let pages = iterator_rmdir_delete_pages(sbi, &pi)?;
            Ok(pi.iterator_dealloc(pages).flush())
        }
        _ => {
            let pages = runtime_rmdir_delete_pages(sbi, &pi)?;
            Ok(pi.runtime_dealloc(pages).flush())
        }
    }
}

fn iterator_rmdir_delete_pages<'a>(
    sbi: &'a SbInfo,
    pi: &InodeWrapper<'a, Clean, UnmapPages, DirInode>,
) -> Result<DirPageListWrapper<Clean, Free>> {
    let delete_dir_info = pi.get_inode_info()?;
    if delete_dir_info.get_ino() != pi.get_ino() {
        pr_info!(
            "ERROR: delete_dir_info inode {:?} does not match pi inode {:?}\n",
            delete_dir_info.get_ino(),
            pi.get_ino()
        );
        return Err(EINVAL);
    }
    let pages = DirPageListWrapper::get_dir_pages_to_unmap(delete_dir_info)?;
    let pages = pages.unmap(sbi)?.fence().dealloc(sbi)?.fence().mark_free();
    sbi.page_allocator.dealloc_dir_page_list(&pages)?;
    Ok(pages)
}

fn runtime_rmdir_delete_pages<'a>(
    sbi: &'a SbInfo,
    pi: &InodeWrapper<'a, Clean, UnmapPages, DirInode>,
) -> Result<Vec<DirPageWrapper<'a, Clean, Free>>> {
    let delete_dir_info = pi.get_inode_info()?;
    if delete_dir_info.get_ino() != pi.get_ino() {
        pr_info!(
            "ERROR: delete_dir_info inode {:?} does not match pi inode {:?}\n",
            delete_dir_info.get_ino(),
            pi.get_ino()
        );
        return Err(EINVAL);
    }
    // deallocate pages (if any) belonging to the inode
    // NOTE: we do this in a series of vectors to reduce the number of
    // total flushes. Unclear if this saves us time, or if the overhead
    // of more flushes is less than the time it takes to manage the vecs.
    // We need to do some evaluation of this
    let pages = delete_dir_info.get_all_pages()?;
    let mut unmap_vec = Vec::new();
    let mut to_dealloc = Vec::new();
    let mut deallocated = Vec::new();
    for page in pages.keys() {
        // the pages have already been removed from the inode's page vector
        let page = DirPageWrapper::mark_to_unmap(sbi, page)?;
        unmap_vec.try_push(page)?;
    }
    for page in unmap_vec.drain(..) {
        let page = page.unmap().flush();
        to_dealloc.try_push(page)?;
    }
    let mut to_dealloc = fence_all_vecs!(to_dealloc);
    for page in to_dealloc.drain(..) {
        let page = page.dealloc(sbi).flush();
        deallocated.try_push(page)?;
    }
    let deallocated = fence_all_vecs!(deallocated);
    for page in &deallocated {
        sbi.page_allocator.dealloc_dir_page(page)?;
    }
    let freed_pages = DirPageWrapper::mark_pages_free(deallocated)?;
    Ok(freed_pages)
}

fn hayleyfs_rename<'a>(
    sbi: &'a SbInfo,
    old_dir: &mut fs::INode,
    old_dentry: &fs::DEntry,
    new_dir: &mut fs::INode,
    new_dentry: &fs::DEntry,
    _flags: u32,
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_name = old_dentry.d_name();
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(old_dir.get_inner())?;
    old_dir.update_ctime_and_mtime();
    let parent_inode = parent_inode
        .update_ctime_and_mtime(old_dir.get_mtime())
        .flush()
        .fence();

    let old_dentry_info = {
        let parent_inode_info = parent_inode.get_inode_info()?;
        parent_inode_info.lookup_dentry(old_name)?
    };
    match old_dentry_info {
        None => Err(ENOENT),
        Some(old_dentry_info) => {
            let test_dentry = DentryWrapper::get_init_dentry(old_dentry_info);
            match test_dentry {
                Ok(_) => {}
                Err(_) => pr_info!(
                    "dentry {:?} is in the index but not on the device\n",
                    old_name
                ),
            }

            let inode_type = sbi.check_inode_type_by_vfs_inode(old_dentry.d_inode())?;
            // let new_parent_inode = sbi.get_init_dir_inode_by_vfs_inode(new_dir.get_inner())?;
            // TODO: this leads to unnecessary updates for single-dir renames. only do this if old_dir
            // and new_dir are actually different.
            new_dir.update_ctime_and_mtime();
            let parent_inode = parent_inode
                .update_ctime_and_mtime(new_dir.get_mtime())
                .flush()
                .fence();
            match inode_type {
                InodeType::REG | InodeType::SYMLINK => {
                    let new_parent_inode = if new_dir.i_ino() == old_dir.i_ino() {
                        None
                    } else {
                        Some(sbi.get_init_dir_inode_by_vfs_inode(new_dir.get_inner())?)
                    };
                    reg_inode_rename(
                        sbi,
                        old_dentry,
                        new_dentry,
                        parent_inode,
                        new_parent_inode,
                        &old_dentry_info,
                    )
                }
                InodeType::DIR => {
                    let new_dir = sbi.get_init_dir_inode_by_vfs_inode(new_dir.get_inner())?;
                    dir_inode_rename(
                        sbi,
                        old_dentry,
                        new_dentry,
                        parent_inode,
                        new_dir,
                        &old_dentry_info,
                    )
                }
                _ => {
                    pr_info!("attempted to rename inode with invalid type\n");
                    Err(EINVAL)
                }
            }
        }
    }
}

fn reg_inode_rename<'a>(
    sbi: &'a SbInfo,
    old_dentry: &fs::DEntry,
    new_dentry: &fs::DEntry,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: Option<InodeWrapper<'a, Clean, Start, DirInode>>,
    old_dentry_info: &DentryInfo,
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_name = cstr_to_filename_array(old_dentry.d_name());
    let new_name = new_dentry.d_name();
    // TODO: move unsafe cast to the wrapper
    let old_inode: &mut fs::INode = unsafe { &mut *old_dentry.d_inode().cast() };
    // TODO: move unsafe cast to the wrapper
    let new_inode: &mut fs::INode = unsafe { &mut *new_dentry.d_inode().cast() }; // TODO: what if there is no new inode...?

    let old_dir_inode_info = old_dir.get_inode_info()?;
    let new_dentry_info = if let Some(ref new_dir) = new_dir {
        let new_dir_inode_info = new_dir.get_inode_info()?;
        new_dir_inode_info.lookup_dentry(new_name)?
    } else {
        old_dir_inode_info.lookup_dentry(new_name)?
    };

    match new_dentry_info {
        Some(new_dentry_info) => {
            // overwriting a dentry
            let new_pi = sbi.get_init_reg_inode_by_vfs_inode(new_inode.get_inner())?;
            new_inode.update_ctime();
            let new_pi = new_pi.update_ctime(new_inode.get_ctime()).flush().fence();
            let (src_dentry, dst_dentry) = if let Some(new_dir) = new_dir {
                let (src_dentry, dst_dentry) = rename_overwrite_dentry_file_inode(
                    sbi,
                    old_dentry_info,
                    &new_dentry_info,
                    &new_pi,
                    &new_dir,
                )?;
                rename_overwrite_file_inode_crossdir(
                    sbi, src_dentry, dst_dentry, new_pi, old_dir, new_dir, &old_name,
                )?
            } else {
                let (src_dentry, dst_dentry) = rename_overwrite_dentry_file_inode(
                    sbi,
                    old_dentry_info,
                    &new_dentry_info,
                    &new_pi,
                    &old_dir,
                )?;
                rename_overwrite_file_inode(
                    sbi, src_dentry, dst_dentry, new_pi, old_dir, &old_name,
                )?
            };
            Ok((src_dentry, dst_dentry))
        }
        None => {
            // creating a new dentry
            let pi = sbi.get_init_reg_inode_by_vfs_inode(old_inode.get_inner())?;
            old_inode.update_ctime();
            let pi = pi.update_ctime(old_inode.get_ctime()).flush().fence();
            let (src_dentry, dst_dentry) = if let Some(new_dir) = new_dir {
                let dst_dentry = get_free_dentry(sbi, &new_dir)?;
                let dst_dentry = dst_dentry.set_name(new_name, false)?.flush().fence();
                let (src_dentry, dst_dentry) =
                    rename_new_dentry_file_inode(sbi, dst_dentry, old_dentry_info, &pi, &new_dir)?;
                rename_new_file_inode_crossdir(
                    sbi, src_dentry, dst_dentry, old_dir, new_dir, &old_name,
                )?
            } else {
                let dst_dentry = get_free_dentry(sbi, &old_dir)?;
                let dst_dentry = dst_dentry.set_name(new_name, false)?.flush().fence();
                let (src_dentry, dst_dentry) =
                    rename_new_dentry_file_inode(sbi, dst_dentry, old_dentry_info, &pi, &old_dir)?;
                rename_new_file_inode_single_dir(sbi, src_dentry, dst_dentry, old_dir, &old_name)?
            };
            Ok((src_dentry, dst_dentry))
        }
    }
}

// TODO: might want to split up into same dir and crossdir
// just because in single dir cases old_dir and new_dir will actually
// point to the same inode which is not ideal
fn dir_inode_rename<'a>(
    sbi: &'a SbInfo,
    old_dentry: &fs::DEntry,
    new_dentry: &fs::DEntry,
    mut old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dentry_info: &DentryInfo,
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_name = cstr_to_filename_array(old_dentry.d_name());
    let new_name = new_dentry.d_name();
    // TODO: move unsafe cast to the wrapper
    let old_inode: &mut fs::INode = unsafe { &mut *old_dentry.d_inode().cast() };
    // TODO: move unsafe cast to the wrapper
    let new_inode: &mut fs::INode = unsafe { &mut *new_dentry.d_inode().cast() };

    let old_dir_inode_info = old_dir.get_inode_info()?;

    if !new_inode.get_inner().is_null() {
        // TODO: ideally we wouldn't do this twice
        let new_pi = sbi.get_init_dir_inode_by_vfs_inode(new_inode.get_inner())?;
        let new_pi_info = new_pi.get_inode_info()?;
        if new_pi_info.has_dentries() {
            return Err(ENOTEMPTY);
        }
    }

    // directory rename is different for same dir vs cross dir
    if old_dir.get_ino() == new_dir.get_ino() {
        let new_dentry_info = old_dir_inode_info.lookup_dentry(new_name)?;
        // same dir
        match new_dentry_info {
            Some(new_dentry_info) => {
                // overwriting another dentry in the same dir
                let new_pi = sbi.get_init_dir_inode_by_vfs_inode(new_inode.get_inner())?;
                new_inode.update_ctime();
                let new_pi = new_pi.update_ctime(new_inode.get_ctime()).flush().fence();
                let (src_dentry, dst_dentry) = rename_overwrite_dentry_dir_inode_single_dir(
                    sbi,
                    old_dentry_info,
                    &new_dentry_info,
                    &new_pi,
                    &mut old_dir,
                )?;
                let (src_dentry, dst_dentry) = rename_overwrite_dir_inode_single_dir(
                    sbi, src_dentry, dst_dentry, new_pi, old_dir, &old_name,
                )?;
                Ok((src_dentry, dst_dentry))
            }
            None => {
                // creating a new dentry in the same dir
                let pi = sbi.get_init_dir_inode_by_vfs_inode(old_inode.get_inner())?;
                old_inode.update_ctime();
                let pi = pi.update_ctime(old_inode.get_ctime()).flush().fence();
                let dst_dentry = get_free_dentry(sbi, &old_dir)?;
                let dst_dentry = dst_dentry.set_name(new_name, true)?.flush().fence();
                let (src_dentry, dst_dentry) = rename_new_dentry_dir_inode_single_dir(
                    sbi,
                    dst_dentry,
                    old_dentry_info,
                    &pi,
                    &mut old_dir,
                )?;
                let (src_dentry, dst_dentry) = rename_new_inode_dir_inode_single_dir(
                    sbi, src_dentry, dst_dentry, pi, old_dir, &old_name,
                )?;
                Ok((src_dentry, dst_dentry))
            }
        }
    } else {
        // crossdir
        let new_dir_inode_info = new_dir.get_inode_info()?;
        let new_dentry_info = new_dir_inode_info.lookup_dentry(new_name)?;
        match new_dentry_info {
            Some(new_dentry_info) => {
                // overwriting a dentry in a different directory
                let new_pi = sbi.get_init_dir_inode_by_vfs_inode(new_inode.get_inner())?;
                new_inode.update_ctime();
                let new_pi = new_pi.update_ctime(new_inode.get_ctime()).flush().fence();
                let (src_dentry, dst_dentry) = rename_overwrite_dentry_dir_inode_single_dir(
                    sbi,
                    old_dentry_info,
                    &new_dentry_info,
                    &new_pi,
                    &mut old_dir,
                )?;
                let (src_dentry, dst_dentry) = rename_overwrite_dir_inode_crossdir(
                    sbi, src_dentry, dst_dentry, new_pi, old_dir, new_dir, &old_name,
                )?;
                Ok((src_dentry, dst_dentry))
            }
            None => {
                // creating a new dentry in a different directory
                let pi = sbi.get_init_dir_inode_by_vfs_inode(old_inode.get_inner())?;
                old_inode.update_ctime();
                let pi = pi.update_ctime(old_inode.get_ctime()).flush().fence();
                let dst_dentry = get_free_dentry(sbi, &new_dir)?;
                let dst_dentry = dst_dentry.set_name(new_name, true)?.flush().fence();
                let (src_dentry, dst_dentry, new_dir) = rename_new_dentry_dir_inode_crossdir(
                    sbi,
                    dst_dentry,
                    old_dentry_info,
                    &pi,
                    new_dir,
                )?;
                let (src_dentry, dst_dentry) = rename_new_inode_dir_inode_crossdir(
                    sbi, src_dentry, dst_dentry, pi, old_dir, new_dir, &old_name,
                )?;
                Ok((src_dentry, dst_dentry))
            }
        }
    }
}

// TODO: all these rename functions need some SERIOUS refactoring
// TODO: indexes should be updated before deallocation
fn rename_overwrite_dentry_file_inode<'a>(
    sbi: &'a SbInfo,
    old_dentry_info: &DentryInfo,
    new_dentry_info: &DentryInfo,
    rename_inode: &InodeWrapper<'a, Clean, Start, RegInode>,
    dst_parent_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    // overwriting another file, potentially deleting its inode
    let src_dentry = DentryWrapper::get_init_dentry(*old_dentry_info)?;
    let dst_dentry = DentryWrapper::get_init_dentry(*new_dentry_info)?;
    set_and_init_rename_ptr_file_inode(sbi, src_dentry, dst_dentry, rename_inode, dst_parent_inode)
}

fn rename_overwrite_dentry_dir_inode_single_dir<'a>(
    sbi: &'a SbInfo,
    old_dentry_info: &DentryInfo,
    new_dentry_info: &DentryInfo,
    rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
    dst_parent_inode: &mut InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    // overwriting another file, potentially deleting its inode
    let src_dentry = DentryWrapper::get_init_dentry(*old_dentry_info)?;
    let dst_dentry = DentryWrapper::get_init_dentry(*new_dentry_info)?;
    set_and_init_rename_ptr_dir_inode_regular(
        sbi,
        src_dentry,
        dst_dentry,
        rename_inode,
        dst_parent_inode,
    )
}

fn rename_new_dentry_dir_inode_single_dir<'a>(
    sbi: &'a SbInfo,
    dst_dentry: DentryWrapper<'a, Clean, Alloc>,
    old_dentry_info: &DentryInfo,
    rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
    dst_parent_inode: &mut InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    let src_dentry = DentryWrapper::get_init_dentry(*old_dentry_info)?;
    set_and_init_rename_ptr_dir_inode_regular(
        sbi,
        src_dentry,
        dst_dentry,
        rename_inode,
        dst_parent_inode,
    )
}

fn rename_new_dentry_dir_inode_crossdir<'a>(
    sbi: &'a SbInfo,
    dst_dentry: DentryWrapper<'a, Clean, Alloc>,
    old_dentry_info: &DentryInfo,
    rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
    dst_parent_inode: InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
    InodeWrapper<'a, Clean, IncLink, DirInode>,
)> {
    let src_dentry = DentryWrapper::get_init_dentry(*old_dentry_info)?;
    let dst_parent_inode = dst_parent_inode.inc_link_count()?.flush().fence();
    unsafe {
        bindings::inc_nlink(dst_parent_inode.get_vfs_inode()?);
    }
    set_and_init_rename_ptr_dir_inode_crossdir(
        sbi,
        src_dentry,
        dst_dentry,
        rename_inode,
        dst_parent_inode,
    )
}

fn rename_new_dentry_file_inode<'a>(
    sbi: &'a SbInfo,
    dst_dentry: DentryWrapper<'a, Clean, Alloc>,
    old_dentry_info: &DentryInfo,
    rename_inode: &InodeWrapper<'a, Clean, Start, RegInode>,
    dst_parent_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    let src_dentry = DentryWrapper::get_init_dentry(*old_dentry_info)?;
    set_and_init_rename_ptr_file_inode(sbi, src_dentry, dst_dentry, rename_inode, dst_parent_inode)
}

fn set_and_init_rename_ptr_file_inode<'a, S: StartOrAlloc + core::fmt::Debug>(
    sbi: &'a SbInfo,
    src_dentry: DentryWrapper<'a, Clean, Start>,
    dst_dentry: DentryWrapper<'a, Clean, S>,
    rename_inode: &InodeWrapper<'a, Clean, Start, RegInode>,
    dst_parent_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    // set and initialize rename pointer to atomically switch the live dentry
    let (src_dentry, dst_dentry) = dst_dentry.set_rename_pointer(sbi, src_dentry);
    let dst_dentry = dst_dentry.flush().fence();
    let (src_dentry, dst_dentry) =
        dst_dentry.init_rename_pointer_file_inode(src_dentry, rename_inode, dst_parent_inode);
    let dst_dentry = dst_dentry.flush().fence();

    // clear src dentry's inode
    let src_dentry = src_dentry.clear_ino().flush().fence();

    Ok((src_dentry, dst_dentry))
}

fn set_and_init_rename_ptr_dir_inode_regular<'a, S: StartOrAlloc>(
    sbi: &'a SbInfo,
    src_dentry: DentryWrapper<'a, Clean, Start>,
    dst_dentry: DentryWrapper<'a, Clean, S>,
    rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
    dst_parent_inode: &mut InodeWrapper<'a, Clean, Start, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
)> {
    // set and initialize rename pointer to atomically switch the live dentry
    let (src_dentry, dst_dentry) = dst_dentry.set_rename_pointer(sbi, src_dentry);
    let dst_dentry = dst_dentry.flush().fence();
    let (src_dentry, dst_dentry) =
        dst_dentry.init_rename_pointer_dir_regular(src_dentry, rename_inode, dst_parent_inode)?;
    let dst_dentry = dst_dentry.flush().fence();

    // clear src dentry's inode
    let src_dentry = src_dentry.clear_ino().flush().fence();

    Ok((src_dentry, dst_dentry))
}

fn set_and_init_rename_ptr_dir_inode_crossdir<'a, S: StartOrAlloc>(
    sbi: &'a SbInfo,
    src_dentry: DentryWrapper<'a, Clean, Start>,
    dst_dentry: DentryWrapper<'a, Clean, S>,
    rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
    dst_parent_inode: InodeWrapper<'a, Clean, IncLink, DirInode>,
) -> Result<(
    DentryWrapper<'a, Clean, ClearIno>,
    DentryWrapper<'a, Clean, InitRenamePointer>,
    InodeWrapper<'a, Clean, IncLink, DirInode>,
)> {
    // set and initialize rename pointer to atomically switch the live dentry
    let (src_dentry, dst_dentry) = dst_dentry.set_rename_pointer(sbi, src_dentry);
    let dst_dentry = dst_dentry.flush().fence();
    let (src_dentry, dst_dentry) =
        dst_dentry.init_rename_pointer_dir_crossdir(src_dentry, rename_inode, &dst_parent_inode);
    let dst_dentry = dst_dentry.flush().fence();

    // clear src dentry's inode
    let src_dentry = src_dentry.clear_ino().flush().fence();

    Ok((src_dentry, dst_dentry, dst_parent_inode))
}

fn rename_overwrite_file_inode<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, RegInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // decrement link count of the inode whose dentry is being overwritten
    // this is the inode being unlinked, not the parent directory
    let new_pi = new_pi.dec_link_count_rename(&dst_dentry)?.flush();
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush();
    let (new_pi, dst_dentry) = fence_all!(new_pi, dst_dentry);
    rename_overwrite_deallocation_file_inode(sbi, src_dentry, dst_dentry, new_pi, old_dir, old_name)
}

fn rename_overwrite_file_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, RegInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // decrement link count of the inode whose dentry is being overwritten
    // this is the inode being unlinked, not the parent directory
    let new_pi = new_pi.dec_link_count_rename(&dst_dentry)?.flush();
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush();
    let (new_pi, dst_dentry) = fence_all!(new_pi, dst_dentry);
    rename_overwrite_deallocation_file_inode_crossdir(
        sbi, src_dentry, dst_dentry, new_pi, old_dir, new_dir, old_name,
    )
}

fn rename_overwrite_dir_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_overwrite_deallocation_dir_inode_single_dir(
        sbi, src_dentry, dst_dentry, new_pi, old_dir, old_name,
    )
}

fn rename_overwrite_dir_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_overwrite_deallocation_dir_inode_crossdir(
        sbi, src_dentry, dst_dentry, new_pi, old_dir, new_dir, old_name,
    )
}

fn rename_new_inode_dir_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, IncLink, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_new_dentry_deallocation_dir_inode_crossdir(
        sbi, src_dentry, dst_dentry, new_pi, old_dir, new_dir, old_name,
    )
}

fn rename_new_inode_dir_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // clear the rename pointer in the dst dentry, since the src has been invalidated
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_new_dentry_deallocation_dir_inode_single_dir(
        sbi, src_dentry, dst_dentry, new_pi, old_dir, old_name,
    )
}

fn rename_new_file_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_deallocation_file_inode_single_dir(sbi, src_dentry, dst_dentry, old_dir, old_name)
}

fn rename_new_file_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, InitRenamePointer>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let dst_dentry = dst_dentry.clear_rename_pointer(&src_dentry).flush().fence();
    rename_deallocation_file_inode_crossdir(sbi, src_dentry, dst_dentry, old_dir, new_dir, old_name)
}

fn rename_overwrite_deallocation_file_inode<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    new_pi: InodeWrapper<'a, Clean, DecLink, RegInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_parent_inode_info = old_dir.get_inode_info()?;
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?
    old_parent_inode_info.atomic_add_and_delete_dentry(&dst_dentry, old_name)?;
    // finish deallocating the new inode and its pages
    // TODO: this should be done in evict_inode, right?
    // finish_unlink(sbi, new_pi)?;
    unsafe {
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
    }

    Ok((src_dentry, dst_dentry))
}

fn rename_overwrite_deallocation_file_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    new_pi: InodeWrapper<'a, Clean, DecLink, RegInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_parent_inode_info = old_dir.get_inode_info()?;
    let new_parent_inode_info = new_dir.get_inode_info()?;
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?
    old_parent_inode_info.atomic_add_and_delete_dentry_crossdir(
        new_parent_inode_info,
        &dst_dentry,
        old_name,
    )?;
    // finish deallocating the new inode and its pages

    unsafe {
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
    }

    Ok((src_dentry, dst_dentry))
}

fn rename_overwrite_deallocation_dir_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // decrement link count because we are getting rid of a dir
    let old_dir = old_dir.dec_link_count(&src_dentry)?.flush().fence();
    unsafe {
        bindings::drop_nlink(old_dir.get_vfs_inode()?);
    }
    // TODO: should zero the link count here
    let new_pi = new_pi.dec_link_count(&src_dentry)?.flush().fence();
    unsafe {
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
    }
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // let delete_dir_info = new_pi.get_inode_info()?; // TODO: this will probably fail
    sbi.inodes_to_free.insert(new_pi.get_ino())?;
    // let new_pi = new_pi.set_unmap_page_state()?;
    // let _new_pi = rmdir_delete_pages(sbi, new_pi)?;
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    let old_parent_inode_info = old_dir.get_inode_info()?;
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?

    old_parent_inode_info.atomic_add_and_delete_dentry(&dst_dentry, old_name)?;

    Ok((src_dentry, dst_dentry))
}

fn rename_overwrite_deallocation_dir_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // decrement link count because we are getting rid of a dir
    let old_dir = old_dir.dec_link_count(&src_dentry)?.flush().fence();
    unsafe {
        bindings::drop_nlink(old_dir.get_vfs_inode()?);
    }
    // TODO: should zero the link count here
    let new_pi = new_pi.dec_link_count(&src_dentry)?.flush().fence();
    unsafe {
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
        bindings::drop_nlink(new_pi.get_vfs_inode()?);
    }
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // let delete_dir_info = new_pi.get_inode_info()?; // TODO: this will probably fail
    sbi.inodes_to_free.insert(new_pi.get_ino())?;
    // let new_pi = new_pi.set_unmap_page_state()?;
    // let _new_pi = rmdir_delete_pages(sbi, new_pi)?;
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    let old_parent_inode_info = old_dir.get_inode_info()?;
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?

    let new_parent_inode_info = new_dir.get_inode_info()?;
    old_parent_inode_info.atomic_add_and_delete_dentry_crossdir(
        new_parent_inode_info,
        &dst_dentry,
        old_name,
    )?;

    Ok((src_dentry, dst_dentry))
}

fn rename_new_dentry_deallocation_dir_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    _new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_parent_inode_info = old_dir.get_inode_info()?;
    // decrement link count because we are getting rid of a dir
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();

    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?
    old_parent_inode_info.atomic_add_and_delete_dentry(&dst_dentry, old_name)?;

    Ok((src_dentry, dst_dentry))
}

fn rename_new_dentry_deallocation_dir_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    _new_pi: InodeWrapper<'a, Clean, Start, DirInode>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, IncLink, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    // decrement link count because we are getting rid of a dir
    let old_dir = old_dir.dec_link_count(&src_dentry)?.flush().fence();
    unsafe {
        bindings::drop_nlink(old_dir.get_vfs_inode()?);
    }
    let old_parent_inode_info = old_dir.get_inode_info()?;
    let new_parent_inode_info = new_dir.get_inode_info()?;
    // decrement link count because we are getting rid of a dir
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();

    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    // TODO is this still right?
    old_parent_inode_info.atomic_add_and_delete_dentry_crossdir(
        new_parent_inode_info,
        &dst_dentry,
        old_name,
    )?;

    Ok((src_dentry, dst_dentry))
}

fn rename_deallocation_file_inode_single_dir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_parent_inode_info = old_dir.get_inode_info()?;
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    old_parent_inode_info.atomic_add_and_delete_dentry(&dst_dentry, old_name)?;

    Ok((src_dentry, dst_dentry))
}

fn rename_deallocation_file_inode_crossdir<'a>(
    sbi: &SbInfo,
    src_dentry: DentryWrapper<'a, Clean, ClearIno>,
    dst_dentry: DentryWrapper<'a, Clean, Complete>,
    old_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    new_dir: InodeWrapper<'a, Clean, Start, DirInode>,
    old_name: &[u8; MAX_FILENAME_LEN],
) -> Result<(
    DentryWrapper<'a, Clean, Free>,
    DentryWrapper<'a, Clean, Complete>,
)> {
    let old_parent_inode_info = old_dir.get_inode_info()?;
    let src_dentry = src_dentry.dealloc_dentry().flush().fence();
    // if the page that the freed dentry belongs to is now empty, free it
    let parent_page = src_dentry.try_dealloc_parent_page(sbi);
    if let Ok(parent_page) = parent_page {
        old_parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
        let parent_page = parent_page.unmap().flush().fence();
        let parent_page = parent_page.dealloc(sbi).flush().fence();
        sbi.page_allocator.dealloc_dir_page(&parent_page)?;
    }
    // atomically update the volatile index
    let new_parent_inode_info = new_dir.get_inode_info()?;
    old_parent_inode_info.atomic_add_and_delete_dentry_crossdir(
        new_parent_inode_info,
        &dst_dentry,
        old_name,
    )?;

    Ok((src_dentry, dst_dentry))
}

#[allow(dead_code)]
fn hayleyfs_unlink<'a>(
    sbi: &'a SbInfo,
    dir: &mut fs::INode,
    dentry: &fs::DEntry,
) -> Result<(
    InodeWrapper<'a, Clean, DecLink, RegInode>,
    DentryWrapper<'a, Clean, Free>,
)> {
    init_timing!(unlink_full_declink);
    start_timing!(unlink_full_declink);
    init_timing!(unlink_full_delete);
    start_timing!(unlink_full_delete);
    // TODO: move unsafe cast to the wrapper
    let inode: &mut fs::INode = unsafe { &mut *dentry.d_inode().cast() };
    init_timing!(unlink_lookup);
    start_timing!(unlink_lookup);
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    dir.update_ctime_and_mtime();
    let parent_inode = parent_inode
        .update_ctime_and_mtime(dir.get_ctime())
        .flush()
        .fence();
    let parent_inode_info = parent_inode.get_inode_info()?;

    // use volatile index to find the persistent dentry
    let dentry_info = parent_inode_info.lookup_dentry(dentry.d_name())?;

    end_timing!(UnlinkLookup, unlink_lookup);
    if let Some(dentry_info) = dentry_info {
        // FIXME?: right now we don't enforce that the dentry has to have pointed
        // to the inode - theoretically an unrelated directory entry being
        // deallocated could be used to decrement an inode's link count

        init_timing!(dec_link_count);
        start_timing!(dec_link_count);
        // obtain target inode and then invalidate the directory entry
        let pd = DentryWrapper::get_init_dentry(dentry_info)?;
        parent_inode_info.delete_dentry(dentry_info)?;
        let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
        inode.update_ctime();
        let pi = pi.update_ctime(inode.get_ctime()).flush().fence();
        let pd = pd.clear_ino().flush().fence();

        // decrement the inode's link count
        // according to Alloy we can share the fence with dentry deallocation
        let pi = pi.dec_link_count(&pd)?.flush();
        unsafe {
            bindings::drop_nlink(inode.get_inner());
        }

        // deallocate the dentry
        let pd = pd.dealloc_dentry().flush();

        let (pi, pd) = fence_all!(pi, pd);

        end_timing!(DecLinkCount, dec_link_count);

        // if the page that the freed dentry belongs to is now empty, free it
        let parent_page = pd.try_dealloc_parent_page(sbi);
        if let Ok(parent_page) = parent_page {
            parent_inode_info.delete(DirPageInfo::new(parent_page.get_page_no()))?;
            let parent_page = parent_page.unmap().flush().fence();
            let parent_page = parent_page.dealloc(sbi).flush().fence();
            sbi.page_allocator.dealloc_dir_page(&parent_page)?;
        }
        // we don't finish the unlink here because the file may still be open somewhere

        end_timing!(UnlinkFullDecLink, unlink_full_declink);
        Ok((pi, pd))
    } else {
        Err(ENOENT)
    }
}

pub(crate) fn finish_unlink<'a>(
    sbi: &'a SbInfo,
    pi: InodeWrapper<'a, Clean, DecLink, RegInode>,
) -> Result<InodeWrapper<'a, Clean, Complete, RegInode>> {
    match sbi.mount_opts.write_type {
        Some(WriteType::Iterator) | None => iterator_finish_unlink(sbi, pi),
        _ => runtime_finish_unlink(sbi, pi),
    }
}

fn iterator_finish_unlink<'a>(
    sbi: &'a SbInfo,
    pi: InodeWrapper<'a, Clean, DecLink, RegInode>,
) -> Result<InodeWrapper<'a, Clean, Complete, RegInode>> {
    let result = pi.try_complete_unlink_iterator()?;
    if let Ok(result) = result {
        // there are still links left
        Ok(result)
    } else if let Err((pi, pages)) = result {
        // no links left - we need to deallocate all of the pages
        let pages = pages.unmap(sbi)?.fence().dealloc(sbi)?.fence().mark_free();
        sbi.page_allocator.dealloc_data_page_list(&pages)?;
        let pi = pi.iterator_dealloc(pages).flush().fence();
        Ok(pi)
    } else {
        Err(EINVAL)
    }
}

fn runtime_finish_unlink<'a>(
    sbi: &'a SbInfo,
    pi: InodeWrapper<'a, Clean, DecLink, RegInode>,
) -> Result<InodeWrapper<'a, Clean, Complete, RegInode>> {
    let result = pi.try_complete_unlink_runtime(sbi)?;
    if let Ok(result) = result {
        Ok(result)
    } else if let Err((pi, mut pages)) = result {
        // go through each page and deallocate it
        // we can drain the vector since missing a page will result in
        // a runtime panic
        // NOTE: we do this in a series of vectors to reduce the number of
        // total flushes. Unclear if this saves us time, or if the overhead
        // of more flushes is less than the time it takes to manage the vecs.
        // We need to do some evaluation of this
        init_timing!(dealloc_pages);
        start_timing!(dealloc_pages);
        let mut to_dealloc = Vec::new();

        for page in pages.drain(..) {
            // the pages have already been removed from the inode's page vector
            let page = page.unmap().flush();
            to_dealloc.try_push(page)?;
        }
        let mut to_dealloc = fence_all_vecs!(to_dealloc);
        let mut deallocated = Vec::new();
        for page in to_dealloc.drain(..) {
            let page = page.dealloc(sbi).flush();
            deallocated.try_push(page)?;
        }
        let deallocated = fence_all_vecs!(deallocated);
        for page in &deallocated {
            sbi.page_allocator.dealloc_data_page(page)?;
        }
        let freed_pages = DataPageWrapper::mark_pages_free(deallocated)?;

        // pages are now deallocated and we can use the freed pages vector
        // to deallocate the inode.
        let pi = pi.runtime_dealloc(freed_pages).flush().fence();
        end_timing!(DeallocPages, dealloc_pages);
        Ok(pi)
    } else {
        Err(EINVAL)
    }
}

fn hayleyfs_symlink<'a>(
    sbi: &'a SbInfo,
    dir: &fs::INode,
    dentry: &fs::DEntry,
    symname: *const core::ffi::c_char,
    mode: u16,
) -> Result<(
    InodeWrapper<'a, Clean, Complete, RegInode>,
    DentryWrapper<'a, Clean, Complete>,
    DataPageListWrapper<Clean, Written>,
    Box<HayleyFsRegInodeInfo>,
)> {
    let name = unsafe { CStr::from_char_ptr(symname) };
    if name.len() >= MAX_FILENAME_LEN {
        return Err(ENAMETOOLONG);
    }

    // obtain and allocate a new persistent dentry
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    let pd = get_free_dentry(sbi, &parent_inode)?;
    let pd = pd.set_name(dentry.d_name(), false)?.flush().fence();

    // obtain and allocate an inode for the symlink
    let pi = sbi.alloc_ino()?;
    let pi = InodeWrapper::get_free_reg_inode_by_ino(sbi, pi)?;

    let pi = pi.allocate_symlink_inode(dir, mode)?.flush().fence();
    let pi_info = Box::try_new(HayleyFsRegInodeInfo::new(pi.get_ino())?)?;

    // allocate a page for the symlink
    let pages = DataPageListWrapper::get_data_page_list(&pi_info, 1, 0)?;
    let pages = if let Err(pages) = pages {
        // we expect an error because the inode shouldn't have any pages yet
        pages.allocate_pages(sbi, &pi_info, 1, 0)?.fence()
    } else {
        pr_info!("ERROR: new symlink file has pages\n");
        return Err(EINVAL);
    };

    let pages = pages.set_backpointers(sbi, pi_info.get_ino())?.fence();
    // we have to zero the page so that we can properly read a null-terminated
    // string later when looking up the symlink
    let (_, pages) = pages.zero_pages(sbi, HAYLEYFS_PAGESIZE, 0)?;
    let pages = pages.fence();

    // need to set file size also which will require writing to the page I think

    // Safety: symname has to temporarily be cast to a mutable raw pointer in order to create
    // the reader. This is safe because 1) a UserSlicePtrReader does not provide any methods
    // that mutate the buffer, 2) we immediately convert the UserSlicePtr into a UserSlicePtrReader,
    // and 3) the UserSlicePtr constructor does not mutate the buffer.
    let mut name_reader =
        unsafe { UserSlicePtr::new(symname as *mut core::ffi::c_void, name.len()).reader() };
    let name_len: u64 = name_reader.len().try_into()?;
    let (bytes_written, pages) = pages.write_pages(sbi, &mut name_reader, name_len, 0)?;
    let pages = pages.fence();

    // set the file size. we'll create the VFS inode based on the persistent inode after
    // this method returns
    let (_size, pi) = pi.set_size(bytes_written, 0, &pages);

    let (pd, pi) = pd.set_file_ino(pi);
    let pd = pd.flush().fence();
    let parent_inode_info = parent_inode.get_inode_info()?;
    pd.index(&parent_inode_info)?;

    Ok((pi, pd, pages, pi_info))
}

// TODO: return a type indicating that the truncate has completed
fn hayleyfs_truncate<'a>(
    sbi: &SbInfo,
    pi: InodeWrapper<'a, Clean, Start, RegInode>,
    size: i64,
) -> Result<()> {
    // we don't have to check if pi is a regular inode because we already
    // did that when we obtained its wrapper

    let pi_size = pi.get_size();
    let new_size: u64 = size.try_into()?;
    let pi_info = pi.get_inode_info()?;

    if pi_size > new_size {
        // truncate decreases

        // first, decrease the inode's size
        unsafe {
            bindings::i_size_write(pi.get_vfs_inode()?, new_size.try_into()?);
        }
        let (new_size, pi) = pi.dec_size(new_size);
        // if the old end and the new end fit on the same page, we are done;
        // we don't have to deallocate any pages
        if new_size % HAYLEYFS_PAGESIZE == pi_size % HAYLEYFS_PAGESIZE {
            Ok(())
        } else {
            let pages =
                DataPageListWrapper::get_data_pages_to_truncate(&pi, new_size, pi_size - new_size)?;
            // TODO: don't get pi_info twice. just doing it to placate the borrow checker
            let pi_info = pi.get_inode_info()?;
            // then free the pages
            let pages = pages.unmap(sbi)?.fence();
            let pages = pages.dealloc(sbi)?.fence().mark_free();

            sbi.page_allocator.dealloc_data_page_list(&pages)?;
            // TODO: should this be done earlier or is it protected by locks?
            pi_info.remove_pages(&pages)?;
            Ok(())
        }
    } else {
        // truncate increases

        // first: allocate pages, zero them out
        // get pages from the end of the file to the end of the new region
        let pages =
            DataPageListWrapper::get_data_page_list(pi_info, new_size - pi_size, pi.get_size())?;
        let bytes_to_truncate = new_size - pi_size;
        let mut alloc_offset = pi_size;
        let (bytes_written, pages) = match pages {
            Ok(pages) => {
                // we don't need to allocate any more pages
                // just zero out the range that we are extending to
                let (bytes_written, pages) = pages.zero_pages(sbi, bytes_to_truncate, pi_size)?;
                (bytes_written, pages.fence())
            }
            Err(pages) => {
                // calculate # of pages to allocate
                let new_page_bytes = if pi_size % HAYLEYFS_PAGESIZE != 0 {
                    // if the end of the file isn't page aligned, we don't
                    // count bytes at the end of the last page
                    let bytes_at_end = HAYLEYFS_PAGESIZE - (pi_size % HAYLEYFS_PAGESIZE);
                    alloc_offset += bytes_at_end;
                    bytes_to_truncate - bytes_at_end
                } else {
                    bytes_to_truncate
                };
                let pages_to_truncate = if new_page_bytes % HAYLEYFS_PAGESIZE == 0 {
                    new_page_bytes / HAYLEYFS_PAGESIZE
                } else {
                    (new_page_bytes / HAYLEYFS_PAGESIZE) + 1 // to account for division rounding down
                };
                let pages = pages
                    .allocate_pages(sbi, &pi_info, pages_to_truncate.try_into()?, alloc_offset)?
                    .fence();
                let pages = pages.set_backpointers(sbi, pi_info.get_ino())?.fence();
                let (bytes_written, pages) = pages.zero_pages(sbi, bytes_to_truncate, pi_size)?;
                (bytes_written, pages.fence())
            }
        };

        // then: increase inode size
        let (new_size, pi) = pi.inc_size_iterator(bytes_written, pi_size, &pages);
        unsafe {
            bindings::i_size_write(pi.get_vfs_inode()?, new_size.try_into()?);
        }
        // TODO: don't get pi_info twice. just doing it to placate the borrow checker
        let pi_info = pi.get_inode_info()?;
        pi_info.insert_pages(pages)?;
        Ok(())
    }
}

fn get_free_dentry<'a, S: Initialized>(
    sbi: &'a SbInfo,
    parent_inode: &InodeWrapper<'a, Clean, S, DirInode>,
) -> Result<DentryWrapper<'a, Clean, Free>> {
    let parent_inode_info = parent_inode.get_inode_info()?;
    let result = parent_inode_info.find_page_with_free_dentry(sbi)?;
    let result = if let Some(page_info) = result {
        let dir_page = DirPageWrapper::from_page_no(sbi, page_info.get_page_no())?;
        dir_page.get_free_dentry(sbi)
    } else {
        // no pages have any free dentries
        alloc_page_for_dentry(sbi, parent_inode, parent_inode_info)
    };
    result
}

fn alloc_page_for_dentry<'a, S: Initialized>(
    sbi: &'a SbInfo,
    parent_inode: &InodeWrapper<'a, Clean, S, DirInode>,
    parent_inode_info: &HayleyFsDirInodeInfo,
) -> Result<DentryWrapper<'a, Clean, Free>> {
    // allocate a page
    // we always use single DirPageWrapper here, rather than an iterator,
    // regardless of the mount options selected, because we are only
    // allocating one page at a time. We could implement a special
    // StaticDirPageWrapper for just this case but it probably will not make
    // a noticeable difference
    let dir_page = DirPageWrapper::alloc_dir_page(sbi)?.flush().fence();
    let dir_page = dir_page.zero_page(sbi)?;
    let dir_page = dir_page
        .set_dir_page_backpointer(parent_inode)
        .flush()
        .fence();
    parent_inode_info.insert(&dir_page)?;
    let pd = dir_page.get_free_dentry(sbi)?;
    Ok(pd)
}

fn init_dentry_with_new_reg_inode<'a>(
    sbi: &'a SbInfo,
    dir: &fs::INode,
    dentry: DentryWrapper<'a, Clean, Alloc>,
    mode: u16,
) -> Result<(
    DentryWrapper<'a, Clean, Complete>,
    InodeWrapper<'a, Clean, Complete, RegInode>,
)> {
    // set up the new inode
    let new_ino = sbi.alloc_ino()?;
    let inode = InodeWrapper::get_free_reg_inode_by_ino(sbi, new_ino)?;
    let inode = inode.allocate_file_inode(dir, mode)?.flush().fence();

    // set the ino in the dentry
    let (dentry, inode) = dentry.set_file_ino(inode);
    let dentry = dentry.flush().fence();

    Ok((dentry, inode))
}

fn init_dentry_with_new_dir_inode<'a>(
    sbi: &'a SbInfo,
    inode: &fs::INode,
    dentry: DentryWrapper<'a, Clean, Alloc>,
    parent_inode: InodeWrapper<'a, Clean, IncLink, DirInode>,
    mode: u16,
) -> Result<(
    DentryWrapper<'a, Clean, Complete>,
    InodeWrapper<'a, Clean, Complete, DirInode>, // parent
    InodeWrapper<'a, Clean, Complete, DirInode>, // new inode
)> {
    // set up the new inode
    let new_ino = sbi.alloc_ino()?;
    let new_inode = InodeWrapper::get_free_dir_inode_by_ino(sbi, new_ino)?;
    let new_inode = new_inode.allocate_dir_inode(inode, mode)?.flush().fence();
    // set the ino in the dentry
    let (dentry, new_inode, parent_inode) = dentry.set_dir_ino(new_inode, parent_inode);
    let dentry = dentry.flush().fence();
    Ok((dentry, parent_inode, new_inode))
}
