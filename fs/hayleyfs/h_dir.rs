use crate::balloc::*;
use crate::defs::*;
use crate::h_inode::*;
use crate::pm::*;
use crate::typestate::*;
use crate::volatile::*;
use core::{ffi, marker::PhantomData, mem};
use kernel::prelude::*;
use kernel::{bindings, dir, file, fs};

#[repr(C)]
#[derive(Debug)]
pub(crate) struct HayleyFsDentry {
    is_dir: u16,
    name: [u8; MAX_FILENAME_LEN],
    ino: InodeNum,
    rename_ptr: u64,
}

impl HayleyFsDentry {
    // Getters are not unsafe; only modifying HayleyFsDentry is unsafe
    pub(crate) fn get_ino(&self) -> InodeNum {
        self.ino
    }

    pub(crate) fn is_rename_ptr_null(&self) -> bool {
        // self.rename_ptr.is_null()
        self.rename_ptr == 0
    }

    pub(crate) fn is_dir(&self) -> bool {
        self.is_dir == 1
    }

    pub(crate) fn has_name(&self) -> bool {
        for char in self.name {
            if char != 0 {
                return true;
            }
        }
        false
    }

    pub(crate) fn get_name(&self) -> [u8; MAX_FILENAME_LEN] {
        self.name
    }

    #[allow(dead_code)]
    pub(crate) fn get_name_as_cstr(&self) -> &CStr {
        unsafe { CStr::from_char_ptr(self.get_name().as_ptr() as *const core::ffi::c_char) }
    }

    pub(crate) fn is_free(&self) -> bool {
        self.ino == 0 && self.is_rename_ptr_null() && !self.has_name()
    }
}

#[allow(dead_code)]
#[derive(Debug)]
pub(crate) struct DentryWrapper<'a, State, Op> {
    state: PhantomData<State>,
    op: PhantomData<Op>,
    dentry: &'a mut HayleyFsDentry,
}

impl<'a, State, Op> PmObjWrapper for DentryWrapper<'a, State, Op> {}

impl<'a, State, Op> DentryWrapper<'a, State, Op> {
    // needs to be public so we can use it to obtain the page that a
    // dentry belongs to
    pub(crate) fn get_dentry_offset(&self, sbi: &'a SbInfo) -> u64 {
        let dentry_virtual_addr = self.dentry as *const HayleyFsDentry as u64;
        let device_virtual_addr = sbi.get_virt_addr() as u64;
        dentry_virtual_addr - device_virtual_addr
    }

    pub(crate) fn get_dentry_info(&self) -> DentryInfo {
        DentryInfo::new(
            self.dentry.ino,
            Some(self.dentry as *const _ as *const ffi::c_void),
            self.dentry.name,
            self.dentry.is_dir(),
        )
    }

    #[allow(dead_code)]
    pub(crate) fn get_name(&self) -> [u8; MAX_FILENAME_LEN] {
        self.dentry.name.clone()
    }

    #[allow(dead_code)]
    pub(crate) fn get_name_as_cstr(&self) -> &CStr {
        self.dentry.get_name_as_cstr()
    }
}

impl<'a> DentryWrapper<'a, Clean, Recovering> {
    pub(crate) fn from_dinfo(info: DentryInfo) -> Result<Self> {
        // use the virtual address in the DentryInfo to look up the
        // persistent dentry
        let dentry: &mut HayleyFsDentry =
            unsafe { &mut *(info.get_virt_addr().unwrap() as *mut HayleyFsDentry) };
        Ok(Self {
            state: PhantomData,
            op: PhantomData,
            dentry,
        })
    }

    pub(crate) fn rename_ptr(&self, sbi: &SbInfo) -> Result<Option<&'a mut HayleyFsDentry>> {
        if self.dentry.rename_ptr == 0 {
            Ok(None)
        } else {
            let base = sbi.get_virt_addr() as *const u8;
            let off: isize = self.dentry.rename_ptr.try_into().unwrap();

            let pg_sz: isize = HAYLEYFS_PAGESIZE.try_into().unwrap();
            let start = pg_sz * (sbi.get_data_pages_start_page() as isize);
            let end = pg_sz * (sbi.get_size() as isize);

            if off < start || off >= end {
                Err(ESPIPE) // XXX: ?
            } else {
                let ptr = unsafe { base.offset(off) } as *mut HayleyFsDentry;
                Ok(unsafe { ptr.as_mut() })
            }
        }
    }

    pub(crate) fn into_init_rename(
        self,
        sbi: &SbInfo,
    ) -> Result<DentryWrapper<'a, Clean, InitRenamePointer>> {
        if let Some(_) = self.rename_ptr(sbi)? {
            let dst: DentryWrapper<'a, Clean, InitRenamePointer> = DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            };
            Ok(dst)
        } else {
            Err(ENOTDIR) // XXX: ?
        }
    }

    // Step 2 of rename recovery, given a dst
    pub(crate) fn src_to_recovering(
        dst: &DentryWrapper<'a, Clean, InitRenamePointer>,
        src: &'a mut HayleyFsDentry,
    ) -> Result<DentryWrapper<'a, Clean, Recovering>> {
        if dst.get_dentry_info().get_ino() != src.get_ino() {
            Ok(DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src,
            })
        } else {
            pr_info!("recover_src failed: no virt_addr");
            Err(EINVAL)
        }
    }

    pub(crate) fn src_to_renamed(
        dst: &DentryWrapper<'a, Clean, InitRenamePointer>,
        src: &'a mut HayleyFsDentry,
    ) -> Result<DentryWrapper<'a, Clean, Renamed>> {
        if dst.get_dentry_info().get_ino() == src.get_ino() {
            Ok(DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src,
            })
        } else {
            pr_info!("recover_src failed: no virt_addr");
            Err(EINVAL)
        }
    }
}

impl<'a> DentryWrapper<'a, Clean, Free> {
    /// Safety
    /// The provided dentry must be free (completely zeroed out).
    pub(crate) unsafe fn wrap_free_dentry(dentry: &'a mut HayleyFsDentry) -> Self {
        Self {
            state: PhantomData,
            op: PhantomData,
            dentry: dentry,
        }
    }

    /// CStr are guaranteed to have a `NUL` byte at the end, so we don't have to check
    /// for that.
    pub(crate) fn set_name(
        self,
        name: &CStr,
        is_dir: bool,
    ) -> Result<DentryWrapper<'a, Dirty, Alloc>> {
        if name.len() >= MAX_FILENAME_LEN {
            return Err(ENAMETOOLONG);
        }
        // copy only the number of bytes in the name
        let num_bytes = if name.len() < MAX_FILENAME_LEN {
            name.len()
        } else {
            MAX_FILENAME_LEN
        };
        let name = name.as_bytes_with_nul();
        self.dentry.name[..num_bytes].clone_from_slice(&name[..num_bytes]);
        self.dentry.is_dir = if is_dir { 1 } else { 0 };

        Ok(DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        })
    }

    pub(crate) fn try_dealloc_parent_page(
        &self,
        sbi: &'a SbInfo,
    ) -> Result<DirPageWrapper<'a, Clean, ToUnmap>> {
        // get the page that owns the dentry and check if it is empty
        // TODO: use a statically checked dir page wrapper
        let parent_page = DirPageWrapper::from_dentry(sbi, self)?;
        // from_dentry checks that the page is initialized. now we check if it is empty
        // and return an unmappable dir page wrapper if it is
        parent_page.is_empty(sbi)
    }
}

impl<'a> DentryWrapper<'a, Clean, Alloc> {
    pub(crate) fn set_file_ino<State: AddLink>(
        self,
        inode: InodeWrapper<'a, Clean, State, RegInode>,
    ) -> (
        DentryWrapper<'a, Dirty, Complete>,
        InodeWrapper<'a, Clean, Complete, RegInode>,
    ) {
        self.dentry.ino = inode.get_ino();
        (
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
            InodeWrapper::new(inode),
        )
    }

    pub(crate) fn set_dir_ino(
        self,
        new_inode: InodeWrapper<'a, Clean, Alloc, DirInode>,
        parent_inode: InodeWrapper<'a, Clean, IncLink, DirInode>,
    ) -> (
        DentryWrapper<'a, Dirty, Complete>,
        InodeWrapper<'a, Clean, Complete, DirInode>,
        InodeWrapper<'a, Clean, Complete, DirInode>,
    ) {
        self.dentry.ino = new_inode.get_ino();
        (
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
            InodeWrapper::new(new_inode),
            InodeWrapper::new(parent_inode),
        )
    }
}

impl<'a, S: StartOrAlloc> DentryWrapper<'a, Clean, S> {
    // TODO: this will also have to be defined for initialized dst dentries
    pub(crate) fn set_rename_pointer(
        self,
        sbi: &'a SbInfo,
        src_dentry: DentryWrapper<'a, Clean, Start>,
    ) -> (
        DentryWrapper<'a, Clean, Renaming>,
        DentryWrapper<'a, Dirty, SetRenamePointer>,
    ) {
        // set self's rename pointer to the PHYSICAL offset of the src dentry in the device
        let src_dentry_offset = src_dentry.get_dentry_offset(sbi);
        self.dentry.rename_ptr = src_dentry_offset;
        (
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src_dentry.dentry,
            },
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
        )
    }
}

impl<'a, Op> DentryWrapper<'a, Clean, Op> {
    #[allow(dead_code)]
    pub(crate) fn get_ino(&self) -> InodeNum {
        self.dentry.get_ino()
    }
}

impl<'a> DentryWrapper<'a, Clean, Start> {
    pub(crate) fn get_init_dentry(info: DentryInfo) -> Result<Self> {
        // use the virtual address in the DentryInfo to look up the
        // persistent dentry
        match info.get_virt_addr() {
            Some(virt_addr) => {
                let dentry: &mut HayleyFsDentry =
                    unsafe { &mut *(virt_addr as *mut HayleyFsDentry) };

                // return an error if the dentry is not initialized
                if dentry.ino == 0 {
                    pr_info!("ERROR: dentry is invalid\n");
                    return Err(EPERM);
                };
                Ok(Self {
                    state: PhantomData,
                    op: PhantomData,
                    dentry,
                })
            }
            None => {
                pr_info!("ERROR: dentry does not have a virt addr\n");
                Err(EPERM)
            }
        }
    }
}

impl<'a> DentryWrapper<'a, Clean, Recovery> {
    // SAFETY: this function is only safe to call on orphaned directory entries during recovery.
    // it is missing validity checks because it needs to be used on invalid dentries
    pub(crate) unsafe fn get_recovery_dentry(info: &DentryInfo) -> Result<Self> {
        match info.get_virt_addr() {
            Some(virt_addr) => {
                let dentry: &mut HayleyFsDentry =
                    unsafe { &mut *(virt_addr as *mut HayleyFsDentry) };
                Ok(Self {
                    state: PhantomData,
                    op: PhantomData,
                    dentry,
                })
            }
            None => {
                pr_info!("ERROR: dentry does not have a virt addr\n");
                Err(EPERM)
            }
        }
    }

    pub(crate) fn recovery_dealloc(self) -> DentryWrapper<'a, Dirty, Free> {
        self.dentry.ino = 0;
        self.dentry.name.iter_mut().for_each(|c| *c = 0);
        self.dentry.rename_ptr = 0;
        self.dentry.is_dir = 0;

        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

impl<'a, Op: DeletableDentry> DentryWrapper<'a, Clean, Op> {
    pub(crate) fn clear_ino(self) -> DentryWrapper<'a, Dirty, ClearIno> {
        self.dentry.ino = 0;
        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

impl<'a> DentryWrapper<'a, Clean, SetRenamePointer> {
    pub(crate) fn init_rename_pointer_file_inode(
        self,
        src_dentry: DentryWrapper<'a, Clean, Renaming>,
        _rename_inode: &InodeWrapper<'a, Clean, Start, RegInode>,
        _parent_dir: &InodeWrapper<'a, Clean, Start, DirInode>,
    ) -> (
        DentryWrapper<'a, Clean, Renamed>,
        DentryWrapper<'a, Dirty, InitRenamePointer>,
    ) {
        // set self's inode to the renamed dentry's inode
        self.dentry.ino = src_dentry.get_ino();
        (
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src_dentry.dentry,
            },
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
        )
    }

    // TODO: this one should be required in a crossdir setting
    #[allow(dead_code)]
    pub(crate) fn init_rename_pointer_dir_crossdir(
        self,
        src_dentry: DentryWrapper<'a, Clean, Renaming>,
        _rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
        _dst_parent_dir: &InodeWrapper<'a, Clean, IncLink, DirInode>,
    ) -> (
        DentryWrapper<'a, Clean, Renamed>,
        DentryWrapper<'a, Dirty, InitRenamePointer>,
    ) {
        // set self's inode to the renamed dentry's inode
        self.dentry.ino = src_dentry.get_ino();
        (
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src_dentry.dentry,
            },
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
        )
    }

    pub(crate) fn init_rename_pointer_dir_regular(
        self,
        src_dentry: DentryWrapper<'a, Clean, Renaming>,
        _rename_inode: &InodeWrapper<'a, Clean, Start, DirInode>,
        dst_parent_dir: &mut InodeWrapper<'a, Clean, Start, DirInode>,
        // dst_parent_inode_info: &HayleyFsDirInodeInfo, // TODO: obtain in the fxn to make sure we use the right one
    ) -> Result<(
        DentryWrapper<'a, Clean, Renamed>,
        DentryWrapper<'a, Dirty, InitRenamePointer>,
    )> {
        let dst_parent_inode_info = dst_parent_dir.get_inode_info()?; // TODO: this might fail

        // if we created a new dentry and the src dentry is not in the dst parent,
        // we should be using the dir crossdir version
        if self.get_ino() == 0
            && dst_parent_inode_info
                .lookup_dentry(src_dentry.get_name_as_cstr())?
                .is_none()
        {
            return Err(EPERM);
        }
        // set self's inode to the renamed dentry's inode
        self.dentry.ino = src_dentry.get_ino();
        Ok((
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: src_dentry.dentry,
            },
            DentryWrapper {
                state: PhantomData,
                op: PhantomData,
                dentry: self.dentry,
            },
        ))
    }
}

impl<'a> DentryWrapper<'a, Clean, InitRenamePointer> {
    pub(crate) fn clear_rename_pointer<SrcState: RenameSource>(
        self,
        _src_dentry: &DentryWrapper<'a, Clean, SrcState>,
        // ) -> DentryWrapper<'a, Dirty, ClearRenamePointer> {
    ) -> DentryWrapper<'a, Dirty, Complete> {
        self.dentry.rename_ptr = 0;
        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

impl<'a> DentryWrapper<'a, Clean, ClearIno> {
    pub(crate) fn dealloc_dentry(self) -> DentryWrapper<'a, Dirty, Free> {
        if self.dentry.rename_ptr != 0 {
            pr_info!(
                "WARNING: dentry {:?} has non zero rename pointer\n",
                self.dentry.name
            );
        }
        self.dentry.name.iter_mut().for_each(|c| *c = 0);
        self.dentry.is_dir = 0;

        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

impl<'a> DentryWrapper<'a, Clean, Complete> {
    // TODO: maybe should take completed inode as well? or ino dentry insert should
    // take the wrappers directly
    pub(crate) fn index(&self, parent_inode_info: &HayleyFsDirInodeInfo) -> Result<()> {
        let dentry_info = DentryInfo::new(
            self.dentry.ino,
            Some(self.dentry as *const _ as *const ffi::c_void),
            self.dentry.name,
            self.dentry.is_dir(),
        );
        parent_inode_info.insert_dentry(dentry_info)
    }
}

impl<'a, Op> DentryWrapper<'a, Dirty, Op> {
    pub(crate) fn flush(self) -> DentryWrapper<'a, InFlight, Op> {
        hayleyfs_flush_buffer(self.dentry, mem::size_of::<HayleyFsDentry>(), false);
        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

impl<'a, Op> DentryWrapper<'a, InFlight, Op> {
    pub(crate) fn fence(self) -> DentryWrapper<'a, Clean, Op> {
        sfence();
        DentryWrapper {
            state: PhantomData,
            op: PhantomData,
            dentry: self.dentry,
        }
    }
}

pub(crate) struct DirOps;
#[vtable]
impl dir::Operations for DirOps {
    fn iterate(f: &file::File, ctx: *mut bindings::dir_context) -> Result<u32> {
        let inode: &mut fs::INode = unsafe { &mut *f.inode().cast() };
        let sb = inode.i_sb();
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
        let result = hayleyfs_readdir(sbi, inode, ctx);
        match result {
            Ok(r) => Ok(r),
            Err(e) => Err(e),
        }
    }

    fn fsync(
        _data: (),
        _file: &file::File,
        _start: u64,
        _end: u64,
        _datasync: bool,
    ) -> Result<u32> {
        Ok(0)
    }

    fn ioctl(data: (), file: &file::File, cmd: &mut file::IoctlCommand) -> Result<i32> {
        cmd.dispatch::<Self>(data, file)
    }
}

pub(crate) fn hayleyfs_readdir(
    sbi: &SbInfo,
    dir: &mut fs::INode,
    ctx: *mut bindings::dir_context,
) -> Result<u32> {
    // get all dentries currently in this inode
    let parent_inode = sbi.get_init_dir_inode_by_vfs_inode(dir.get_inner())?;
    let parent_inode_info = parent_inode.get_inode_info()?;
    move_dir_inode_tree_to_map(sbi, &parent_inode_info)?;

    let dentries = parent_inode_info.get_all_dentries()?;
    let num_dentries: i64 = dentries.len().try_into()?;
    unsafe {
        if (*ctx).pos >= num_dentries {
            return Ok(0);
        }
    }
    let mut i = 0;
    let cur_pos: usize = unsafe { (*ctx).pos.try_into()? };
    for j in cur_pos..dentries.len() {
        let dentry = match dentries.get(j) {
            Some(dentry) => dentry,
            None => {
                unsafe { (*ctx).pos += i };
                return Ok(0);
            }
        };
        let name = dentry.get_name_as_cstr();
        let file_type = match sbi.check_inode_type_by_inode_num(dentry.get_ino())? {
            InodeType::REG => bindings::DT_REG,
            InodeType::DIR => bindings::DT_DIR,
            InodeType::SYMLINK => bindings::DT_LNK,
            _ => {
                pr_info!("ERROR: unrecognized inode type\n");
                return Err(EINVAL);
            }
        };
        let result = unsafe {
            bindings::dir_emit(
                ctx,
                name.as_char_ptr(),
                name.len().try_into()?,
                parent_inode_info.get_ino(),
                file_type,
            )
        };
        if !result {
            unsafe { (*ctx).pos += i };
            return Ok(0);
        }
        i += 1;
    }
    unsafe { (*ctx).pos += i };
    Ok(0)
}
