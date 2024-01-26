use crate::balloc::*;
use crate::defs::*;
use crate::h_inode::*;
use crate::typestate::*;
use crate::volatile::*;
use crate::{end_timing, fence_vec, init_timing, start_timing};
use core::{ffi, marker::Sync, ptr, sync::atomic::Ordering};
use kernel::prelude::*;
use kernel::{
    bindings, error, file, fs,
    io_buffer::{IoBufferReader, IoBufferWriter},
    iomap, mm,
};

pub(crate) struct Adapter {}

impl<T: Sync> file::OpenAdapter<T> for Adapter {
    unsafe fn convert(_inode: *mut bindings::inode, _file: *mut bindings::file) -> *const T {
        ptr::null_mut()
    }
}

pub(crate) struct FileOps;
#[vtable]
impl file::Operations for FileOps {
    fn open(_context: &(), file: &file::File) -> Result<()> {
        let ret = unsafe { bindings::generic_file_open(file.inode(), file.get_inner()) };
        if ret < 0 {
            Err(error::Error::from_kernel_errno(ret))
        } else {
            Ok(())
        }
    }

    fn release(_data: (), _file: &file::File) {}

    fn fsync(_data: (), file: &file::File, start: u64, end: u64, _datasync: bool) -> Result<u32> {
        if unsafe { bindings::mapping_mapped((*file.get_inner()).f_mapping) != 0 } {
            // if the file is mmapped, flush all cache lines for the mapped area.
            // just obtain the data page list wrapper for the specified region, flush its data,
            // and fence.
            // this is a bit wasteful if the location/lengths are not page aligned, but easier to
            // implement.

            let inode: &mut fs::INode = unsafe { &mut *file.inode().cast() };
            let sb = inode.i_sb();
            unsafe { bindings::sb_start_write(sb) };
            // TODO: safety
            let fs_info_raw = unsafe { (*sb).s_fs_info };
            let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
            let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
            let pi_info = pi.get_inode_info()?;
            let len = end - start;

            let result = hayleyfs_msync(sbi, pi_info, start, len);
            match result {
                Ok(_) => Ok(0),
                Err(e) => Err(e),
            }
        } else {
            Ok(0)
        }
    }

    fn write(
        _data: (),
        file: &file::File,
        reader: &mut impl IoBufferReader,
        offset: u64,
    ) -> Result<usize> {
        // TODO: cleaner way to set up the semaphore with Rust RwSemaphore
        let inode: &mut fs::INode = unsafe { &mut *file.inode().cast() };
        let sb = inode.i_sb();
        unsafe { bindings::sb_start_write(sb) };
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
        unsafe { bindings::inode_lock(inode.get_inner()) };
        let result = hayleyfs_write(sbi, inode, reader, offset);
        unsafe { bindings::inode_unlock(inode.get_inner()) };
        unsafe { bindings::sb_end_write(sb) };
        match result {
            Ok((bytes_written, _)) => Ok(bytes_written.try_into()?),
            Err(e) => Err(e),
        }
    }

    fn read(
        _data: (),
        file: &file::File,
        writer: &mut impl IoBufferWriter,
        offset: u64,
    ) -> Result<usize> {
        // TODO: cleaner way to set up the semaphore with Rust RwSemaphore
        let inode: &mut fs::INode = unsafe { &mut *file.inode().cast() };
        let sb = inode.i_sb();
        unsafe { bindings::sb_start_write(sb) };
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        // TODO: it's probably not safe to just grab s_fs_info and
        // get a mutable reference to one of the dram indexes
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
        unsafe { bindings::inode_lock_shared(inode.get_inner()) };
        let result = hayleyfs_read(sbi, file, inode, writer, offset);
        unsafe { bindings::inode_unlock_shared(inode.get_inner()) };
        unsafe { bindings::sb_end_write(sb) }
        match result {
            Ok(r) => Ok(r.try_into()?),
            Err(e) => Err(e),
        }
    }

    fn seek(_data: (), f: &file::File, offset: file::SeekFrom) -> Result<u64> {
        let (offset, whence) = match offset {
            file::SeekFrom::Start(off) => (off.try_into()?, bindings::SEEK_SET),
            file::SeekFrom::End(off) => (off, bindings::SEEK_END),
            file::SeekFrom::Current(off) => (off, bindings::SEEK_CUR),
        };
        let result =
            unsafe { bindings::generic_file_llseek(f.get_inner(), offset, whence.try_into()?) };
        if result < 0 {
            Err(error::Error::from_kernel_errno(result.try_into()?))
        } else {
            Ok(result.try_into()?)
        }
    }

    fn ioctl(data: (), file: &file::File, cmd: &mut file::IoctlCommand) -> Result<i32> {
        cmd.dispatch::<Self>(data, file)
    }

    fn mmap(_data: (), f: &file::File, vma: *mut bindings::vm_area_struct) -> Result<()> {
        unsafe {
            bindings::file_accessed(f.get_inner());
            bindings::vm_flags_set(vma, (bindings::VM_MIXEDMAP | bindings::VM_HUGEPAGE).into());
            (*vma).vm_ops = mm::OperationsVtable::<VmaOps>::build();
        }
        Ok(())
    }
}

#[allow(dead_code)]
fn hayleyfs_write<'a>(
    sbi: &'a SbInfo,
    // inode: RwSemaphore<&mut fs::INode>,
    inode: &mut fs::INode,
    reader: &mut impl IoBufferReader,
    offset: u64,
) -> Result<(u64, InodeWrapper<'a, Clean, IncSize, RegInode>)> {
    init_timing!(full_write);
    start_timing!(full_write);
    // TODO: give a way out if reader.len() is 0
    let len: u64 = reader.len().try_into()?;
    init_timing!(write_inode_lookup);
    start_timing!(write_inode_lookup);
    let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;

    let pi_info = pi.get_inode_info()?;
    end_timing!(WriteInodeLookup, write_inode_lookup);

    match sbi.mount_opts.write_type {
        Some(WriteType::Iterator) | None => {
            let (page_list, bytes_written) =
                iterator_write(sbi, &pi, &pi_info, reader, len, offset)?;
            let (inode_size, pi) =
                pi.inc_size_iterator(bytes_written.try_into()?, offset, &page_list);

            // update the VFS inode's size
            inode.i_size_write(inode_size.try_into()?);
            inode.update_ctime_and_mtime();
            let pi = pi.update_ctime_and_mtime(inode.get_mtime()).flush().fence();
            end_timing!(FullWrite, full_write);
            Ok((bytes_written, pi))
        }
        Some(WriteType::SinglePage) => {
            let count = if HAYLEYFS_PAGESIZE < len {
                HAYLEYFS_PAGESIZE
            } else {
                len
            };
            let (data_page, bytes_written) =
                single_page_write(sbi, &pi, &pi_info, reader, count, offset)?;

            let (inode_size, pi) =
                pi.inc_size_single_page(bytes_written.try_into()?, offset, data_page);

            // update the VFS inode's size
            inode.i_size_write(inode_size.try_into()?);
            inode.update_ctime_and_mtime();
            let pi = pi.update_ctime_and_mtime(inode.get_mtime()).flush().fence();
            end_timing!(FullWrite, full_write);
            Ok((bytes_written, pi))
        }
        Some(WriteType::RuntimeCheck) => {
            let (page_list, bytes_written) =
                runtime_checked_write(sbi, &pi, &pi_info, reader, len, offset)?;
            let (inode_size, pi) =
                pi.inc_size_runtime_check(bytes_written.try_into()?, offset, page_list);
            // update the VFS inode's size
            inode.i_size_write(inode_size.try_into()?);
            inode.update_ctime_and_mtime();
            let pi = pi.update_ctime_and_mtime(inode.get_mtime()).flush().fence();
            end_timing!(FullWrite, full_write);
            Ok((bytes_written, pi))
        }
    }
}

fn single_page_write<'a>(
    sbi: &'a SbInfo,
    pi: &InodeWrapper<'a, Clean, Start, RegInode>,
    pi_info: &HayleyFsRegInodeInfo,
    reader: &mut impl IoBufferReader,
    count: u64,
    offset: u64,
) -> Result<(StaticDataPageWrapper<'a, Clean, Written>, u64)> {
    // let offset: usize = offset.try_into()?;

    // this is the value of the `offset` field of the page that
    // we want to write to
    let page_offset = page_offset(offset)?;

    // does this page exist yet? if not, allocate it
    init_timing!(write_lookup_page);
    start_timing!(write_lookup_page);
    let result = pi_info.find(page_offset);
    end_timing!(WriteLookupPage, write_lookup_page);
    let data_page = if let Some(page_no) = result {
        StaticDataPageWrapper::from_page_no(sbi, page_no)?
    } else {
        init_timing!(write_alloc_page);
        start_timing!(write_alloc_page);
        let page = StaticDataPageWrapper::alloc_data_page(sbi, offset)?
            .flush()
            .fence();
        let page = page.set_data_page_backpointer(&pi).flush().fence();
        // add page to the index
        // this is safe to do here because we hold a lock on this inode
        pi_info.insert_unchecked(&page)?;
        end_timing!(WriteAllocPage, write_alloc_page);
        page
    };
    let offset_in_page = offset - page_offset;
    let bytes_after_offset = HAYLEYFS_PAGESIZE - offset_in_page;
    // either write the rest of the count or write to the end of the page
    let to_write = if count < bytes_after_offset {
        count
    } else {
        bytes_after_offset
    };
    init_timing!(write_to_page);
    start_timing!(write_to_page);
    let (bytes_written, data_page) =
        data_page.write_to_page(sbi, reader, offset_in_page, to_write)?;
    let data_page = data_page.fence();
    end_timing!(WriteToPage, write_to_page);

    if bytes_written < to_write {
        pr_info!(
            "WARNING: wrote {:?} out of {:?} bytes\n",
            bytes_written,
            to_write
        );
    }
    Ok((data_page, bytes_written))
}

fn runtime_checked_write<'a>(
    sbi: &'a SbInfo,
    pi: &InodeWrapper<'a, Clean, Start, RegInode>,
    pi_info: &HayleyFsRegInodeInfo,
    reader: &mut impl IoBufferReader,
    mut len: u64,
    offset: u64,
) -> Result<(Vec<DataPageWrapper<'a, Clean, Written>>, u64)> {
    // get a list of writeable pages, either by finding an already-allocated
    // page or allocating
    let mut bytes = 0;
    let mut pages = Vec::new();
    let mut loop_offset = offset;
    while bytes < len {
        // get offset of the next page in the file
        let page_offset = page_offset(loop_offset)?;
        // determine if the file actually has the page
        let result = pi_info.find(page_offset);
        match result {
            Some(page_no) => {
                let page = DataPageWrapper::from_page_no(sbi, page_no)?;
                pages.try_push(page)?;
            }
            None => {
                // we need to allocate a page
                // TODO: error handling
                // TODO: one fence for all newly-allocated pages
                let new_page = DataPageWrapper::alloc_data_page(sbi, page_offset)?
                    .flush()
                    .fence();
                let new_page = new_page.set_data_page_backpointer(pi).flush().fence();
                pi_info.insert(&new_page)?;
                pages.try_push(new_page)?;
            }
        }
        bytes += HAYLEYFS_PAGESIZE;
        loop_offset = page_offset + HAYLEYFS_PAGESIZE;
    }

    // write to the pages
    let mut written_pages = Vec::new();
    // get offset into the first page to write to
    let mut page_offset = page_offset(offset)?;
    let mut offset_in_page = offset - page_offset;

    let mut bytes_written = 0;
    let write_size = len;
    for page in pages.drain(..) {
        if bytes_written >= write_size {
            break;
        }
        let bytes_to_write = if len < HAYLEYFS_PAGESIZE - offset_in_page {
            len
        } else {
            HAYLEYFS_PAGESIZE - offset_in_page
        };
        let (written, page) = page.write_to_page(sbi, reader, offset_in_page, bytes_to_write)?;
        bytes_written += written;
        page_offset += HAYLEYFS_PAGESIZE;
        len -= bytes_to_write;
        offset_in_page = 0;
        written_pages.try_push(page)?;
    }
    let mut written_pages = fence_vec!(written_pages);

    for page in written_pages.iter_mut() {
        page.make_drop_safe();
    }

    Ok((written_pages, bytes_written))
}

#[allow(dead_code)]
fn iterator_write<'a>(
    sbi: &'a SbInfo,
    pi: &InodeWrapper<'a, Clean, Start, RegInode>,
    pi_info: &HayleyFsRegInodeInfo,
    reader: &mut impl IoBufferReader,
    count: u64,
    offset: u64,
) -> Result<(DataPageListWrapper<Clean, Written>, u64)> {
    // TODO: if we have to allocate some pages we don't write to, they should
    // probably get zeroed out
    let (alloc_count, alloc_offset) = if offset > pi.get_size() {
        (count + offset - pi.get_size(), pi.get_size())
    } else {
        (count, offset)
    };
    let page_list = DataPageListWrapper::get_data_page_list(pi_info, alloc_count, alloc_offset)?;
    let page_list = match page_list {
        Ok(page_list) => page_list, // all pages are already allocated
        Err(page_list) => {
            let pages_to_write = get_num_pages_in_region(alloc_count, alloc_offset);
            let pages_left = pages_to_write - page_list.len();
            let allocation_offset =
                page_offset(alloc_offset)? + page_list.len() * HAYLEYFS_PAGESIZE;
            let page_list = page_list
                .allocate_pages(sbi, &pi_info, pages_left.try_into()?, allocation_offset)?
                .fence();
            let page_list = page_list.set_backpointers(sbi, pi.get_ino())?.fence();
            page_list
        }
    };
    let (bytes_written, page_list) = page_list.write_pages(sbi, reader, count, offset)?;
    let page_list = page_list.fence();
    Ok((page_list, bytes_written))
}

#[allow(dead_code)]
fn hayleyfs_read(
    sbi: &SbInfo,
    file: &file::File,
    inode: &mut fs::INode,
    writer: &mut impl IoBufferWriter,
    mut offset: u64,
) -> Result<u64> {
    init_timing!(full_read);
    start_timing!(full_read);
    let mut count: u64 = writer.len().try_into()?;
    init_timing!(read_inode_lookup);
    start_timing!(read_inode_lookup);
    let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
    let pi_info = pi.get_inode_info()?;
    end_timing!(ReadInodeLookup, read_inode_lookup);
    let size: u64 = inode.i_size_read().try_into()?;
    count = if count < size { count } else { size };
    if offset >= size {
        return Ok(0);
    }
    let mut bytes_left_in_file = size - offset; // # of bytes that can be read
    let mut bytes_read = 0;
    while count > 0 {
        let page_offset = page_offset(offset)?;

        let offset_in_page = offset - page_offset;
        let bytes_left_in_page = HAYLEYFS_PAGESIZE - offset_in_page;
        let bytes_after_offset = if bytes_left_in_file <= bytes_left_in_page {
            bytes_left_in_file
        } else {
            bytes_left_in_page
        };

        // either read the rest of the count or write to the end of the page
        let to_read = if count < bytes_after_offset {
            count
        } else {
            bytes_after_offset
        };
        if to_read == 0 {
            break;
        }
        init_timing!(page_lookup);
        start_timing!(page_lookup);
        // if the page exists, read from it. Otherwise, return zeroes
        let result = pi_info.find(page_offset.try_into()?);
        end_timing!(LookupDataPage, page_lookup);
        if let Some(page_no) = result {
            let data_page = DataPageWrapper::from_page_no(sbi, page_no)?;
            init_timing!(read_page);
            start_timing!(read_page);
            let read = data_page.read_from_page(sbi, writer, offset_in_page, to_read)?;
            end_timing!(ReadDataPage, read_page);
            bytes_read += read;
            offset += read;
            count -= read;
            bytes_left_in_file -= read;
        } else {
            init_timing!(read_page);
            start_timing!(read_page);
            writer.clear(to_read.try_into()?)?;
            end_timing!(ReadDataPage, read_page);
            bytes_read += to_read;
            offset += to_read;
            count -= to_read;
            bytes_left_in_file -= to_read;
        }
    }
    unsafe {
        bindings::file_accessed(file.get_inner());
    }
    end_timing!(FullRead, full_read);
    Ok(bytes_read)
}

pub(crate) struct VmaOps;
#[vtable]
impl mm::Operations for VmaOps {
    fn fault(vmf: &mut bindings::vm_fault) -> bindings::vm_fault_t {
        Self::huge_fault(vmf, bindings::page_entry_size_PE_SIZE_PTE)
    }

    fn huge_fault(
        vmf: &mut bindings::vm_fault,
        pe: bindings::page_entry_size,
    ) -> bindings::vm_fault_t {
        let mut pfn: bindings::pfn_t = bindings::pfn_t { val: 0 };
        let mut error: i32 = 0;

        unsafe {
            // TODO: also update persistent timestamp!!
            if (*vmf).flags & bindings::fault_flag_FAULT_FLAG_WRITE != 0 {
                bindings::file_update_time((*(*vmf).__bindgen_anon_1.vma).vm_file);
            }

            bindings::dax_iomap_fault(
                vmf,
                pe,
                &mut pfn,
                &mut error,
                iomap::OperationsVtable::<IomapOps>::build(),
            )
        }
    }
}

pub(crate) struct IomapOps;
#[vtable]
impl iomap::Operations for IomapOps {
    fn iomap_begin(
        inode: &fs::INode,
        pos: i64,
        length: i64,
        flags: u32,
        iomap: *mut bindings::iomap,
        _srcmap: *mut bindings::iomap,
    ) -> Result<i32> {
        // TODO: safe wrapper
        let blkbits = unsafe { (*inode.get_inner()).i_blkbits };
        let first_block = pos >> blkbits;

        let sb = inode.i_sb();
        // TODO: safety
        let fs_info_raw = unsafe { (*sb).s_fs_info };
        let sbi = unsafe { &mut *(fs_info_raw as *mut SbInfo) };
        let create = flags & bindings::IOMAP_WRITE != 0;

        let (num_pages, addr) =
            hayleyfs_iomap_get_blocks(sbi, inode, first_block << blkbits, length, create)?;

        // TODO: safe wrapper around iomap
        unsafe {
            (*iomap).flags = 0;
            (*iomap).bdev = (*sb).s_bdev;
            (*iomap).dax_dev = sbi.get_dax_dev();
            (*iomap).offset = first_block << blkbits;
        }

        if num_pages == 0 {
            unsafe {
                (*iomap).type_ = bindings::IOMAP_HOLE.try_into()?;
                (*iomap).addr = bindings::IOMAP_NULL_ADDR.try_into()?;
                (*iomap).length = 1 << blkbits;
            }
        } else {
            unsafe {
                (*iomap).type_ = bindings::IOMAP_MAPPED.try_into()?;
                (*iomap).addr = addr as u64;
                (*iomap).length = (num_pages * HAYLEYFS_PAGESIZE).try_into()?;
                let new_flag: u16 = bindings::IOMAP_F_MERGED.try_into()?;
                (*iomap).flags |= new_flag;
            }
        }

        Ok(0)
    }

    fn iomap_end(
        inode: &fs::INode,
        _pos: i64,
        length: i64,
        written: isize,
        flags: u32,
        iomap: *mut bindings::iomap,
    ) -> Result<i32> {
        unsafe {
            if u32::from((*iomap).type_) == bindings::IOMAP_MAPPED
                && written < length.try_into()?
                && flags & bindings::IOMAP_WRITE != 0
            {
                bindings::truncate_pagecache(inode.get_inner(), inode.i_size_read());
            }
        }
        Ok(0)
    }
}

// returns the number of blocks in this section and the starting block number(?)
// NOTE: page offset should be a multiple of page size
fn hayleyfs_iomap_get_blocks(
    sbi: &SbInfo,
    inode: &fs::INode,
    page_offset: i64,
    length: i64,
    create: bool,
) -> Result<(u64, *mut ffi::c_void)> {
    let pagesize_i64: i64 = HAYLEYFS_PAGESIZE.try_into()?;
    if page_offset % pagesize_i64 != 0 {
        pr_info!("ERROR: page offset {:?} is not page aligned\n", page_offset);
        return Err(EINVAL);
    }
    // get the data page list for the requested range
    let pi = sbi.get_init_reg_inode_by_vfs_inode(inode.get_inner())?;
    let pi_info = pi.get_inode_info()?;

    let page_list = DataPageListWrapper::get_data_page_list(
        pi_info,
        length.try_into()?,
        page_offset.try_into()?,
    )?;
    match page_list {
        Ok(page_list) => {
            // in this case, we found the number of pages we want. don't need to allocate
            // even if create == true, just return the number of pages and the start
            get_num_pages_and_offset(page_list)
        }
        Err(page_list) => {
            // in this case, we did not get the requested number of pages
            // if create == false, just return the existing contiguous section
            if create == false {
                get_num_pages_and_offset(page_list)
            } else {
                // otherwise, allocate the rest of the pages, set their backpointers,
                // then see how many contiguous pages we can obtain
                let num_pages =
                    get_num_pages_in_region(length.try_into()?, page_offset.try_into()?);
                let pages_left = num_pages - page_list.len();
                let len_i64: i64 = page_list.len().try_into()?;
                let allocation_offset = page_offset + len_i64 * pagesize_i64;
                let page_list = page_list
                    .allocate_pages(
                        sbi,
                        &pi_info,
                        pages_left.try_into()?,
                        allocation_offset.try_into()?,
                    )?
                    .fence();
                let page_list = page_list.set_backpointers(sbi, pi.get_ino())?.fence();
                // TODO: increase page size? Not sure exactly when that should happen
                get_num_pages_and_offset(page_list)
            }
        }
    }
}

fn get_num_pages_and_offset<S: PagesExist>(
    page_list: DataPageListWrapper<Clean, S>,
) -> Result<(u64, *mut ffi::c_void)> {
    let num_phys_contiguous_pages = page_list.num_contiguous_pages_from_start();
    if num_phys_contiguous_pages == 0 {
        return Err(ENODATA);
    }
    // can safely unwrap because we know there is at least one page in the list
    let addr = page_list.first_page_virt_addr().unwrap();
    Ok((num_phys_contiguous_pages, addr))
}

fn get_num_pages_in_region(count: u64, offset: u64) -> u64 {
    if offset % HAYLEYFS_PAGESIZE == 0 {
        if count % HAYLEYFS_PAGESIZE == 0 {
            count / HAYLEYFS_PAGESIZE
        } else {
            (count / HAYLEYFS_PAGESIZE) + 1
        }
    } else {
        let first_page_bytes = HAYLEYFS_PAGESIZE - (offset % HAYLEYFS_PAGESIZE);
        let remaining_bytes = if count < first_page_bytes {
            count
        } else {
            count - first_page_bytes
        };
        if remaining_bytes % HAYLEYFS_PAGESIZE == 0 {
            (remaining_bytes / HAYLEYFS_PAGESIZE) + 1 // +1 for the first page
        } else {
            (remaining_bytes / HAYLEYFS_PAGESIZE) + 2 // +2 for the first and last page
        }
    }
}

fn hayleyfs_msync(
    sbi: &SbInfo,
    pi_info: &HayleyFsRegInodeInfo,
    start: u64,
    len: u64,
) -> Result<DataPageListWrapper<Clean, Msynced>> {
    let data_pages = DataPageListWrapper::get_data_page_list(pi_info, len, start)?;
    let data_pages = match data_pages {
        Ok(data_pages) => data_pages, // we got all of the requested pages
        Err(_) => {
            pr_info!("ERROR: did not obtain the expected number of pages in msync on inode\n",);
            return Err(EINVAL);
        }
    };
    data_pages.msync_pages(sbi)
}
