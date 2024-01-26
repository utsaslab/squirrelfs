/*
 * BRIEF DESCRIPTION
 *
 * XIP operations.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <asm/cpufeature.h>
#include <asm/pgtable.h>
#include "pmfs.h"
#include "xip.h"
#include "inode.h"

static ssize_t
do_xip_mapping_read(struct address_space *mapping,
		    struct file_ra_state *_ra,
		    struct file *filp,
		    char __user *buf,
		    size_t len,
		    loff_t *ppos)
{
	struct inode *inode = mapping->host;
	pgoff_t index, end_index;
	unsigned long offset;
	loff_t isize, pos;
	size_t copied = 0, error = 0;
	timing_t memcpy_time;
	loff_t start_pos = *ppos;
	loff_t end_pos = start_pos + len - 1;
	unsigned long start_block = start_pos >> PAGE_SHIFT;
	unsigned long end_block = end_pos >> PAGE_SHIFT;
	unsigned long num_blocks = end_block - start_block + 1;
	timing_t read_find_blocks_time;

	pos = *ppos;
	index = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;

	isize = i_size_read(inode);
	if (!isize)
		goto out;

	end_index = (isize - 1) >> PAGE_SHIFT;
	do {
		unsigned long nr, left;
		void *xip_mem;
		unsigned long xip_pfn;
		int zero = 0;
		int blocks_found;

		if (index >= end_index) {
			if (index > end_index)
				goto out;
			nr = ((isize - 1) & ~PAGE_MASK) + 1;
			if (nr <= offset)
				goto out;
		}

		if (end_index - index + 1 < num_blocks)
			num_blocks = (end_index - index + 1);

		num_blocks = 1;
		PMFS_START_TIMING(read_find_blocks_t, read_find_blocks_time);
		blocks_found = pmfs_get_xip_mem(mapping, index, num_blocks, 0,
						&xip_mem, &xip_pfn);
		PMFS_END_TIMING(read_find_blocks_t, read_find_blocks_time);

		if (unlikely(blocks_found <= 0)) {
			if (blocks_found == -ENODATA || blocks_found == 0) {
				/* sparse */
				zero = 1;
			} else
				goto out;
		}

		if (blocks_found > 0) {
			if (index + blocks_found - 1 >= end_index) {
				if (index > end_index)
					goto out;

				nr = ((isize - 1) & ~PAGE_MASK) + 1;
				nr += (end_index - index) * PAGE_SIZE;
				if (nr <= offset) {
					goto out;
				}
			} else
				nr = PAGE_SIZE*blocks_found;
		}

		nr = nr - offset;
		if (nr > len - copied)
			nr = len - copied;

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			/* address based flush */ ;

		/*
		 * Ok, we have the mem, so now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		PMFS_START_TIMING(memcpy_r_t, memcpy_time);
		if (!zero)
			left = __copy_to_user(buf+copied, xip_mem+offset, nr);
		else
			left = __clear_user(buf + copied, nr);
		PMFS_END_TIMING(memcpy_r_t, memcpy_time);

		if (left) {
			error = -EFAULT;
			goto out;
		}

		copied += (nr - left);
		offset += (nr - left);
		index += offset >> PAGE_SHIFT;
		num_blocks -= offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;
	} while (copied < len);

out:
	*ppos = pos + copied;
	if (filp)
		file_accessed(filp);

	return (copied ? copied : error);
}

ssize_t
xip_file_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	if (!access_ok(buf, len))
		return -EFAULT;

	return do_xip_mapping_read(filp->f_mapping, &filp->f_ra, filp,
				   buf, len, ppos);
}

/*
 * Wrappers. We need to use the rcu read lock to avoid
 * concurrent truncate operation. No problem for write because we held
 * i_mutex.
 */
ssize_t pmfs_xip_file_read(struct file *filp, char __user *buf,
			    size_t len, loff_t *ppos)
{
	ssize_t res;
	timing_t xip_read_time;

	PMFS_START_TIMING(xip_read_t, xip_read_time);
//	rcu_read_lock();
	res = xip_file_read(filp, buf, len, ppos);
//	rcu_read_unlock();
	PMFS_END_TIMING(xip_read_t, xip_read_time);
	pmfs_dbg_verbose("%s: returning %d\n", __func__, res);
	return res;
}

static inline void pmfs_flush_edge_cachelines(loff_t pos, ssize_t len,
	void *start_addr)
{
	if (unlikely(pos & 0x7))
		pmfs_flush_buffer(start_addr, 1, false);
	if (unlikely(((pos + len) & 0x7) && ((pos & (CACHELINE_SIZE - 1)) !=
			((pos + len) & (CACHELINE_SIZE - 1)))))
		pmfs_flush_buffer(start_addr + len, 1, false);
}

noinline static size_t memcpy_to_nvmm(char *kmem, loff_t offset,
	const char __user *buf, size_t bytes)
{
	size_t copied;

	copied = bytes - __copy_from_user_inatomic_nocache(kmem +
							   offset, buf, bytes);

	return copied;
}

static ssize_t
__pmfs_xip_file_write(struct address_space *mapping, const char __user *buf,
          size_t count, loff_t pos, loff_t *ppos)
{
	struct inode    *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	long        status = 0;
	size_t      bytes;
	ssize_t     written = 0;
	struct pmfs_inode *pi;
	timing_t memcpy_time, write_time;
	loff_t start_pos = pos;
	loff_t end_pos = start_pos + count - 1;
	unsigned long end_block = end_pos >> sb->s_blocksize_bits;
	timing_t write_find_blocks_time;

	PMFS_START_TIMING(internal_write_t, write_time);
	pi = pmfs_get_inode(sb, inode->i_ino);
	do {
		unsigned long index;
		unsigned long offset;
		size_t copied;
		void *xmem;
		unsigned long xpfn;
		int blocks_found;
		unsigned long num_blocks;

		index = pos >> sb->s_blocksize_bits;
		num_blocks = end_block - index + 1;

		PMFS_START_TIMING(write_find_block_t, write_find_blocks_time);
		blocks_found = pmfs_get_xip_mem(mapping, index,
						num_blocks, 1,
						&xmem, &xpfn);
		PMFS_END_TIMING(write_find_block_t, write_find_blocks_time);
		if (blocks_found <= 0) {
			break;
		}

		offset = (pos & (sb->s_blocksize - 1)); /* Within page */
		bytes = (sb->s_blocksize*blocks_found) - offset;
		if (bytes > count)
			bytes = count;


		PMFS_START_TIMING(memcpy_w_t, memcpy_time);
		pmfs_xip_mem_protect(sb, xmem + offset, bytes, 1);
		copied = memcpy_to_nvmm((char *)xmem, offset, buf, bytes);
		pmfs_xip_mem_protect(sb, xmem + offset, bytes, 0);
		PMFS_END_TIMING(memcpy_w_t, memcpy_time);

		/* if start or end dest address is not 8 byte aligned, 
	 	 * __copy_from_user_inatomic_nocache uses cacheable instructions
	 	 * (instead of movnti) to write. So flush those cachelines. */
		pmfs_flush_edge_cachelines(pos, copied, xmem + offset);

        	if (likely(copied > 0)) {
			status = copied;

			if (status >= 0) {
				written += status;
				count -= status;
				pos += status;
				buf += status;
			}
		}
		if (unlikely(copied != bytes))
			if (status >= 0)
				status = -EFAULT;
		if (status < 0)
			break;
	} while (count);
	*ppos = pos;
	/*
 	* No need to use i_size_read() here, the i_size
 	* cannot change under us because we hold i_mutex.
 	*/
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		pmfs_update_isize(inode, pi);
	}

	PMFS_END_TIMING(internal_write_t, write_time);
	return written ? written : status;
}

/* optimized path for file write that doesn't require a transaction. In this
 * path we don't need to allocate any new data blocks. So the only meta-data
 * modified in path is inode's i_size, i_ctime, and i_mtime fields */
static ssize_t pmfs_file_write_fast(struct super_block *sb, struct inode *inode,
	struct pmfs_inode *pi, const char __user *buf, size_t count, loff_t pos,
	loff_t *ppos, u64 block)
{
	void *xmem = pmfs_get_block(sb, block);
	size_t copied, ret = 0, offset;
	timing_t memcpy_time;
	bool strong_guarantees = PMFS_SB(sb)->s_mount_opt & PMFS_MOUNT_STRICT;
	bool barrier = strong_guarantees ? true : false;

	offset = pos & (sb->s_blocksize - 1);

	PMFS_START_TIMING(memcpy_w_t, memcpy_time);
	pmfs_xip_mem_protect(sb, xmem + offset, count, 1);
	copied = memcpy_to_nvmm((char *)xmem, offset, buf, count);
	pmfs_xip_mem_protect(sb, xmem + offset, count, 0);
	PMFS_END_TIMING(memcpy_w_t, memcpy_time);

	pmfs_flush_edge_cachelines(pos, copied, xmem + offset);

	if (likely(copied > 0)) {
		pos += copied;
		ret = copied;
	}
	if (unlikely(copied != count && copied == 0))
		ret = -EFAULT;
	*ppos = pos;
	inode->i_ctime = inode->i_mtime = current_time(inode);
	if (pos > inode->i_size) {
		/* make sure written data is persistent before updating
	 	* time and size */
		PERSISTENT_MARK();
		i_size_write(inode, pos);
		PERSISTENT_BARRIER();
		pmfs_memunlock_inode(sb, pi);
		pmfs_update_time_and_size(inode, pi);
		pmfs_memlock_inode(sb, pi);
	} else {
		u64 c_m_time;
		/* update c_time and m_time atomically. We don't need to make the data
		 * persistent because the expectation is that the close() or an explicit
		 * fsync will do that. */
		c_m_time = (inode->i_ctime.tv_sec & 0xFFFFFFFF);
		c_m_time = c_m_time | (c_m_time << 32);
		pmfs_memunlock_inode(sb, pi);
		pmfs_memcpy_atomic(&pi->i_ctime, &c_m_time, 8);
		pmfs_memlock_inode(sb, pi);
	}
	pmfs_flush_buffer(pi, 1, barrier);
	return ret;
}

/*
 * blk_off is used in different ways depending on whether the edge block is
 * at the beginning or end of the write. If it is at the beginning, we copy from
 * start-of-block to 'blk_off'. If it is the end block, we copy from 'blk_off' to
 * end-of-block
 */
static inline void pmfs_copy_to_edge_blk (struct super_block *sb, struct
				       pmfs_inode *pi, bool over_blk, unsigned long block, size_t blk_off,
				       bool is_end_blk, void *buf)
{
	void *ptr;
	size_t count;
	unsigned long blknr;
	u64 bp = 0;

	if (over_blk) {
		blknr = block >> (pmfs_inode_blk_shift(pi) -
			sb->s_blocksize_bits);
		__pmfs_find_data_blocks(sb, pi, blknr, &bp, 1);
		ptr = pmfs_get_block(sb, bp);
		if (ptr != NULL) {
			if (is_end_blk) {
				ptr = ptr + blk_off - (blk_off % 8);
				count = pmfs_inode_blk_size(pi) -
					blk_off + (blk_off % 8);
			} else
				count = blk_off + (8 - (blk_off % 8));

			pmfs_memunlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
			memcpy_to_nvmm(ptr, 0, buf, count);
			pmfs_memlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
		}
	}
}

/*
 * blk_off is used in different ways depending on whether the edge block is
 * at the beginning or end of the write. If it is at the beginning, we copy from
 * start-of-block to 'blk_off'. If it is the end block, we copy from 'blk_off' to
 * end-of-block
 */
static inline void pmfs_copy_from_edge_blk (struct super_block *sb, struct
				       pmfs_inode *pi, bool over_blk, unsigned long block, size_t blk_off,
				       bool is_end_blk, void **buf)
{
	void *ptr;
	size_t count;
	unsigned long blknr;
	u64 bp = 0;
	int ret;

	if (over_blk) {
		blknr = block >> (pmfs_inode_blk_shift(pi) -
			sb->s_blocksize_bits);
		__pmfs_find_data_blocks(sb, pi, blknr, &bp, 1);
		ptr = pmfs_get_block(sb, bp);
		if (ptr != NULL) {
			if (is_end_blk) {
				ptr = ptr + blk_off - (blk_off % 8);
				count = pmfs_inode_blk_size(pi) -
					blk_off + (blk_off % 8);
			} else
				count = blk_off + (8 - (blk_off % 8));

			*buf = kmalloc(count, GFP_KERNEL);
			pmfs_memunlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
			ret = __copy_to_user(*buf, ptr, count);
			pmfs_memlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
		}
	}
}

/*
 * blk_off is used in different ways depending on whether the edge block is
 * at the beginning or end of the write. If it is at the beginning, we zero from
 * start-of-block to 'blk_off'. If it is the end block, we zero from 'blk_off' to
 * end-of-block
 */
static inline void pmfs_clear_edge_blk (struct super_block *sb, struct
	pmfs_inode *pi, bool new_blk, unsigned long block, size_t blk_off,
	bool is_end_blk)
{
	void *ptr;
	size_t count;
	unsigned long blknr;
	u64 bp = 0;

	if (new_blk) {
		blknr = block >> (pmfs_inode_blk_shift(pi) -
			sb->s_blocksize_bits);
		__pmfs_find_data_blocks(sb, pi, blknr, &bp, 1);
		ptr = pmfs_get_block(sb, bp);
		if (ptr != NULL) {
			if (is_end_blk) {
				ptr = ptr + blk_off - (blk_off % 8);
				count = pmfs_inode_blk_size(pi) -
					blk_off + (blk_off % 8);
			} else
				count = blk_off + (8 - (blk_off % 8));
			pmfs_memunlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
			memset_nt(ptr, 0, count);
			pmfs_memlock_range(sb, ptr,  pmfs_inode_blk_size(pi));
		}
	}
}

ssize_t pmfs_xip_cow_file_write(struct file *filp, const char __user *buf,
          size_t len, loff_t *ppos)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode    *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	pmfs_transaction_t *trans;
	struct pmfs_inode *pi;
	ssize_t     written = 0;
	loff_t pos;
	u64 block;
	bool new_sblk = false, new_eblk = false;
	bool over_sblk = false, over_eblk = false;
	size_t count, offset, eblk_offset, ret;
	unsigned long start_blk, end_blk, num_blocks, max_logentries;
	bool same_block;
	timing_t xip_write_time, xip_write_fast_time;
	int num_blocks_found = 0;
	void *start_buf = NULL, *end_buf = NULL;
	__le64 *free_blk_list = NULL;
	__le64 *inplace_blk_list = NULL;
	__le64 **log_entries = NULL;
	__le64 *log_entry_nums = NULL;
	unsigned long num_inplace_blks = 0;
	int log_entry_idx = 0;
	int idx = 0, idx2 = 0;
	int free_blk_list_idx = 0;
	__le64 block_val = 0;

	PMFS_START_TIMING(xip_write_t, xip_write_time);

	sb_start_write(inode->i_sb);
	inode_lock(inode);

	if (!access_ok(buf, len)) {
		ret = -EFAULT;
		goto out;
	}
	pos = *ppos;

	if (filp->f_flags & O_APPEND)
		pos = i_size_read(inode);

	count = len;
	if (count == 0) {
		ret = 0;
		goto out;
	}

	pi = pmfs_get_inode(sb, inode->i_ino);

	offset = pos & (sb->s_blocksize - 1);
	num_blocks = ((count + offset - 1) >> sb->s_blocksize_bits) + 1;
	/* offset in the actual block size block */
	offset = pos & (pmfs_inode_blk_size(pi) - 1);
	start_blk = pos >> sb->s_blocksize_bits;
	end_blk = start_blk + num_blocks - 1;

	num_blocks_found = pmfs_find_data_blocks(inode, start_blk, &block, 1);

	/* Referring to the inode's block size, not 4K */
	same_block = (((count + offset - 1) >>
			pmfs_inode_blk_shift(pi)) == 0) ? 1 : 0;
	if (block && same_block) {
		PMFS_START_TIMING(xip_write_fast_t, xip_write_fast_time);
		ret = pmfs_file_write_fast(sb, inode, pi, buf, count, pos,
			ppos, block);
		PMFS_END_TIMING(xip_write_fast_t, xip_write_fast_time);
		goto out;
	}
	max_logentries = num_blocks / MAX_PTRS_PER_LENTRY + 2;
	if (max_logentries > MAX_METABLOCK_LENTRIES)
		max_logentries = MAX_METABLOCK_LENTRIES;

	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES + max_logentries, pmfs_get_cpuid(sb));
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	ret = file_remove_privs(filp);
	if (ret) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}
	inode->i_ctime = inode->i_mtime = current_time(inode);
	pmfs_update_time(inode, pi);

	/* We avoid zeroing the alloc'd range, which is going to be overwritten
	 * by this system call anyway */
	if (offset != 0) {
		pmfs_find_data_blocks(inode, start_blk, &block, 1);
		if (block == 0)
			new_sblk = true;
		else if (pos < i_size_read(inode))
			over_sblk = true;
	}

	eblk_offset = (pos + count) & (pmfs_inode_blk_size(pi) - 1);
	if (eblk_offset != 0) {
		pmfs_find_data_blocks(inode, end_blk, &block, 1);
		if (block == 0)
			new_eblk = true;
		else if ((pos + count) < i_size_read(inode))
			over_eblk = true;
	}

	pmfs_copy_from_edge_blk(sb, pi, over_sblk, start_blk, offset, false, &start_buf);
	pmfs_copy_from_edge_blk(sb, pi, over_eblk, end_blk, eblk_offset, true, &end_buf);

	inplace_blk_list = (__le64 *) kmalloc(num_blocks * sizeof(__le64), GFP_KERNEL);
	free_blk_list = (__le64 *) kmalloc(num_blocks * sizeof(__le64), GFP_KERNEL);
	log_entries = (__le64 **) kmalloc(num_blocks * sizeof(__le64), GFP_KERNEL);
	log_entry_nums = (__le64 *) kmalloc(num_blocks * sizeof(__le64), GFP_KERNEL);

	num_inplace_blks = 0;

	/* don't zero-out the allocated blocks */
	pmfs_alloc_blocks(trans, inode, start_blk, num_blocks, false,
			  ANY_CPU, 1, inplace_blk_list, &num_inplace_blks,
			  (void **)log_entries, log_entry_nums, &log_entry_idx);

	/* now zero out the edge blocks which will be partially written */
	pmfs_clear_edge_blk(sb, pi, new_sblk, start_blk, offset, false);
	pmfs_clear_edge_blk(sb, pi, new_eblk, end_blk, eblk_offset, true);

	pmfs_copy_to_edge_blk(sb, pi, over_sblk, start_blk, offset, false, start_buf);
	pmfs_copy_to_edge_blk(sb, pi, over_eblk, end_blk, eblk_offset, true, end_buf);

	if (start_buf)
		kfree(start_buf);
	if (end_buf)
		kfree(end_buf);
	start_buf = NULL;
	end_buf = NULL;

	written = __pmfs_xip_file_write(mapping, buf, count, pos, ppos);
	if (written < 0 || written != count)
		pmfs_dbg_verbose("write incomplete/failed: written %ld len %ld"
				 " pos %llx start_blk %lx num_blocks %lx\n",
				 written, count, pos, start_blk, num_blocks);

	pmfs_commit_transaction(sb, trans);

	if (num_inplace_blks > 0) {
		trans = pmfs_new_transaction(sb, max_logentries, pmfs_get_cpuid(sb));
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}

		free_blk_list_idx = 0;
		for (idx = 0; idx < log_entry_idx; idx++) {
			pmfs_add_logentry(sb, trans, (void *)log_entries[idx],
					  (size_t)log_entry_nums[idx] << 3, LE_DATA);

			for (idx2 = 0; idx2 < log_entry_nums[idx]; idx2++) {
				block_val = *(log_entries[idx] + (idx2));
				if (block_val != 0) {
					free_blk_list[free_blk_list_idx] = block_val;
					*(log_entries[idx] + (idx2)) = inplace_blk_list[free_blk_list_idx];
					free_blk_list_idx++;
				}
			}
		}

		written = __pmfs_xip_file_write(mapping, buf, count, pos, ppos);
		if (written < 0 || written != count)
			pmfs_dbg_verbose("write incomplete/failed: written %ld len %ld"
					 " pos %llx start_blk %lx num_blocks %lx\n",
					 written, count, pos, start_blk, num_blocks);

		pmfs_commit_transaction(sb, trans);

		if (free_blk_list != NULL && num_inplace_blks != 0) {
			truncate_strong_guarantees(sb, free_blk_list,
						   free_blk_list_idx,
						   pi->i_blk_type);
			kfree(free_blk_list);
			kfree(log_entries);
			kfree(log_entry_nums);
			kfree(inplace_blk_list);
			free_blk_list = NULL;
			num_inplace_blks = 0;
			log_entry_idx = 0;
		}
	}

	ret = written;
out:
	inode_unlock(inode);
	sb_end_write(inode->i_sb);
	PMFS_END_TIMING(xip_write_t, xip_write_time);

	return ret;
}


ssize_t pmfs_xip_file_write(struct file *filp, const char __user *buf,
          size_t len, loff_t *ppos)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode    *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	pmfs_transaction_t *trans;
	struct pmfs_inode *pi;
	ssize_t     written = 0;
	loff_t pos;
	u64 block;
	bool new_sblk = false, new_eblk = false;
	size_t count, offset, eblk_offset, ret;
	unsigned long start_blk, end_blk, num_blocks, max_logentries;
	bool same_block;
	timing_t xip_write_time, xip_write_fast_time;
	int num_blocks_found = 0;
	bool strong_guarantees = PMFS_SB(sb)->s_mount_opt & PMFS_MOUNT_STRICT;
	void *start_buf = NULL, *end_buf = NULL;
	__le64 *free_blk_list = NULL;
	unsigned long num_free_blks = 0;
	struct process_numa *proc_numa;
	int cpu = pmfs_get_cpuid(sb);
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	bool over_sblk = false, over_eblk = false;
	timing_t allocate_blocks_time;
	timing_t write_new_trans_time, write_commit_trans_time;

	PMFS_START_TIMING(xip_write_t, xip_write_time);

	sb_start_write(inode->i_sb);
	inode_lock(inode);

	if (!access_ok(buf, len)) {
		ret = -EFAULT;
		goto out;
	}
	pos = *ppos;

	if (filp->f_flags & O_APPEND)
		pos = i_size_read(inode);

	count = len;
	if (count == 0) {
		ret = 0;
		goto out;
	}

	pi = pmfs_get_inode(sb, inode->i_ino);

	if (sbi->num_numa_nodes > 1 && pi->numa_node != pmfs_get_numa_node(sb, cpu)) {
		proc_numa = &(sbi->process_numa[current->tgid % sbi->num_parallel_procs]);
		if (proc_numa->tgid == current->tgid)
			proc_numa->numa_node = pi->numa_node;
		else {
			proc_numa->tgid = current->tgid;
			proc_numa->numa_node = pi->numa_node;
		}

		// sched_setaffinity(current->pid, &(sbi->numa_cpus[pi->numa_node].cpumask));
	}

	if (strong_guarantees && pi->huge_aligned_file && pos < i_size_read(inode)) {
		inode_unlock(inode);
		sb_end_write(inode->i_sb);
		return pmfs_xip_cow_file_write(filp, buf, len, ppos);
	}

	offset = pos & (sb->s_blocksize - 1);
	num_blocks = ((count + offset - 1) >> sb->s_blocksize_bits) + 1;
	/* offset in the actual block size block */
	offset = pos & (pmfs_inode_blk_size(pi) - 1);
	start_blk = pos >> sb->s_blocksize_bits;
	end_blk = start_blk + num_blocks - 1;

	if (!(strong_guarantees && pos < i_size_read(inode))) {
		num_blocks_found = pmfs_find_data_blocks(inode, start_blk, &block, 1);

		/* Referring to the inode's block size, not 4K */
		same_block = (((count + offset - 1) >>
			       pmfs_inode_blk_shift(pi)) == 0) ? 1 : 0;
		if (block && same_block) {
			PMFS_START_TIMING(xip_write_fast_t, xip_write_fast_time);
			ret = pmfs_file_write_fast(sb, inode, pi, buf, count, pos,
						    ppos, block);
			PMFS_END_TIMING(xip_write_fast_t, xip_write_fast_time);
			goto out;
		}
	}

	max_logentries = num_blocks / MAX_PTRS_PER_LENTRY + 2;
	if (max_logentries > MAX_METABLOCK_LENTRIES)
		max_logentries = MAX_METABLOCK_LENTRIES;

	PMFS_START_TIMING(write_new_trans_t, write_new_trans_time);
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES + max_logentries, pmfs_get_cpuid(sb));
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);
	PMFS_END_TIMING(write_new_trans_t, write_new_trans_time);

	ret = file_remove_privs(filp);
	if (ret) {
		pmfs_abort_transaction(sb, trans);
		goto out;
	}
	inode->i_ctime = inode->i_mtime = current_time(inode);
	pmfs_update_time(inode, pi);

	/* We avoid zeroing the alloc'd range, which is going to be overwritten
	 * by this system call anyway */
	if (offset != 0) {
		pmfs_find_data_blocks(inode, start_blk, &block, 1);
		if (block == 0)
			new_sblk = true;
		else if (pos < i_size_read(inode))
			over_sblk = true;
	}

	eblk_offset = (pos + count) & (pmfs_inode_blk_size(pi) - 1);
	if (eblk_offset != 0) {
		pmfs_find_data_blocks(inode, end_blk, &block, 1);
		if (block == 0)
			new_eblk = true;
		else if ((pos + count) < i_size_read(inode))
			over_eblk = true;
	}

	if (strong_guarantees && pos < i_size_read(inode)) {
		pmfs_copy_from_edge_blk(sb, pi, over_sblk, start_blk, offset, false, &start_buf);
		pmfs_copy_from_edge_blk(sb, pi, over_eblk, end_blk, eblk_offset, true, &end_buf);
	}

	if (pos < i_size_read(inode)) {
		free_blk_list = (__le64 *) kmalloc(num_blocks * sizeof(__le64), GFP_KERNEL);
		num_free_blks = 0;
	}

	/* don't zero-out the allocated blocks */
	PMFS_START_TIMING(allocate_blocks_t, allocate_blocks_time);
	pmfs_alloc_blocks(trans, inode, start_blk, num_blocks, false,
			  ANY_CPU, 1, free_blk_list, &num_free_blks,
			  NULL, NULL, NULL);
	PMFS_END_TIMING(allocate_blocks_t, allocate_blocks_time);

	/* now zero out the edge blocks which will be partially written */
	pmfs_clear_edge_blk(sb, pi, new_sblk, start_blk, offset, false);
	pmfs_clear_edge_blk(sb, pi, new_eblk, end_blk, eblk_offset, true);

	if (strong_guarantees && pos < i_size_read(inode)) {
		pmfs_copy_to_edge_blk(sb, pi, over_sblk, start_blk, offset, false, start_buf);
		pmfs_copy_to_edge_blk(sb, pi, over_eblk, end_blk, eblk_offset, true, end_buf);
	}

	if (start_buf)
		kfree(start_buf);
	if (end_buf)
		kfree(end_buf);

	written = __pmfs_xip_file_write(mapping, buf, count, pos, ppos);
	if (written < 0 || written != count)
		pmfs_dbg_verbose("write incomplete/failed: written %ld len %ld"
				 " pos %llx start_blk %lx num_blocks %lx\n",
				 written, count, pos, start_blk, num_blocks);

	PMFS_START_TIMING(write_commit_trans_t, write_commit_trans_time);
	pmfs_commit_transaction(sb, trans);
	PMFS_END_TIMING(write_commit_trans_t, write_commit_trans_time);

	if (free_blk_list != NULL && num_free_blks != 0) {
		truncate_strong_guarantees(sb, free_blk_list, num_free_blks, pi->i_blk_type);
		kfree(free_blk_list);
		free_blk_list = NULL;
		num_free_blks = 0;
	}

	ret = written;
out:
	inode_unlock(inode);
	sb_end_write(inode->i_sb);
	PMFS_END_TIMING(xip_write_t, xip_write_time);

	return ret;
}

static int pmfs_find_and_alloc_blocks(struct inode *inode,
				      sector_t iblock,
				      unsigned long max_blocks,
				      u64 *bno,
				      int create)
{
	int err = -EIO;
	u64 block;
	pmfs_transaction_t *trans;
	struct pmfs_inode *pi;
	int blocks_found = 0;
	timing_t read_pmfs_find_data_blocks_time;

	if (create == 0) {
		PMFS_START_TIMING(read_pmfs_find_data_blocks_t,
				  read_pmfs_find_data_blocks_time);
		blocks_found = pmfs_find_data_blocks_read(inode,
						     iblock, &block,
						     max_blocks);
		PMFS_END_TIMING(read_pmfs_find_data_blocks_t,
				read_pmfs_find_data_blocks_time);
	} else {
		blocks_found = pmfs_find_data_blocks(inode,
						     iblock, &block,
						     max_blocks);
	}

	if (blocks_found == 0) {
		struct super_block *sb = inode->i_sb;
		if (!create) {
			err = -ENODATA;
			goto err;
		}

		pi = pmfs_get_inode(sb, inode->i_ino);
		trans = pmfs_current_transaction();
		if (trans) {
			err = pmfs_alloc_blocks_weak(trans, inode,
						     iblock,
						     max_blocks,
						     true, ANY_CPU, 0);

			if (err) {
				pmfs_dbg_verbose("[%s:%d] Alloc failed!\n",
					__func__, __LINE__);
				goto err;
			}
		} else {
			/* 1 lentry for inode, 1 lentry for inode's b-tree */
			trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES, pmfs_get_cpuid(sb));
			if (IS_ERR(trans)) {
				err = PTR_ERR(trans);
				goto err;
			}

			rcu_read_unlock();
			inode_lock(inode);

			pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY,
					  LE_DATA);
			err = pmfs_alloc_blocks_weak(trans, inode,
						     iblock,
						     max_blocks,
						     true, ANY_CPU, 0);

			pmfs_commit_transaction(sb, trans);

			inode_unlock(inode);
			rcu_read_lock();
			if (err) {
				pmfs_dbg_verbose("[%s:%d] Alloc failed!\n",
					__func__, __LINE__);
				goto err;
			}
		}

		blocks_found = pmfs_find_data_blocks(inode, iblock, &block, max_blocks);

		if (blocks_found == 0) {
			pmfs_dbg_verbose("[%s:%d] But alloc didn't fail!\n",
				  __func__, __LINE__);
			err = -ENODATA;
			goto err;
		}
	}

	pmfs_dbg_verbose("iblock 0x%lx allocated_block 0x%llx\n", iblock,
			 block);

	*bno = block;
	err = 0;
 err:
	return blocks_found;
}

int pmfs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
	unsigned int flags, struct iomap *iomap, bool taking_lock)
{
	struct pmfs_sb_info *sbi = PMFS_SB(inode->i_sb);
	unsigned int blkbits = inode->i_blkbits;
	unsigned long first_block = offset >> blkbits;
	unsigned long max_blocks = (length + (1 << blkbits) - 1) >> blkbits;
	bool new = false;
	u64 bno;
	int ret;
	unsigned long diff_between_devs, byte_offset_in_dax;
	unsigned long first_virt_end, second_virt_start;

	pmfs_dbg_verbose("%s: calling find_and_alloc_blocks. first_block = %lu "
			 "max_blocks = %lu. length = %lld\n", __func__,
			 first_block, max_blocks, length);

	ret = pmfs_find_and_alloc_blocks(inode,
				   first_block,
				   max_blocks,
				   &bno,
				   flags & IOMAP_WRITE);

	if (ret < 0) {
		pmfs_dbg("%s: pmfs_dax_get_blocks failed %d", __func__, ret);
		pmfs_dbg("%s: returning %d\n", __func__, ret);
		return ret;
	}

	iomap->flags = 0;
	iomap->bdev = inode->i_sb->s_bdev;
	iomap->dax_dev = sbi->s_dax_dev;
	iomap->offset = (u64)first_block << blkbits;

	if (ret == 0) {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->length = 1 << blkbits;
	} else {
		iomap->type = IOMAP_MAPPED;
		iomap->addr = bno; //(sector_t)(bno >> 9);//<< (blkbits - 9));
		iomap->length = (u64)ret << blkbits;
		iomap->flags |= IOMAP_F_MERGED;
	}


	if (sbi->num_numa_nodes == 2) {
		byte_offset_in_dax = bno;
		if (byte_offset_in_dax >= sbi->initsize) {
			first_virt_end = (unsigned long) sbi->virt_addr +
				(unsigned long) sbi->pmem_size;
			second_virt_start = (unsigned long) sbi->virt_addr_2;
			diff_between_devs = second_virt_start - first_virt_end;
			byte_offset_in_dax -= diff_between_devs;
			iomap->addr = byte_offset_in_dax; //(sector_t)byte_offset_in_dax >> 9;
		}
	}

	if (new)
		iomap->flags |= IOMAP_F_NEW;

	pmfs_dbg_verbose("%s: iomap->flags %d, iomap->offset %lld, iomap->addr %lu, "
			 "iomap->length %llu\n", __func__, iomap->flags, iomap->offset,
			 iomap->addr, iomap->length);

	return 0;
}


int pmfs_iomap_end(struct inode *inode, loff_t offset, loff_t length,
	ssize_t written, unsigned int flags, struct iomap *iomap)
{
	if (iomap->type == IOMAP_MAPPED &&
			written < length &&
			(flags & IOMAP_WRITE))
		truncate_pagecache(inode, inode->i_size);
	return 0;
}


static int pmfs_iomap_begin_lock(struct inode *inode, loff_t offset,
	loff_t length, unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	return pmfs_iomap_begin(inode, offset, length, flags, iomap, true);
}

static struct iomap_ops pmfs_iomap_ops_lock = {
	.iomap_begin	= pmfs_iomap_begin_lock,
	.iomap_end	= pmfs_iomap_end,
};

static inline int __pmfs_get_block(struct inode *inode, pgoff_t pgoff,
				   unsigned long max_blocks, int create, u64 *result)
{
	int rc = 0;

	rc = pmfs_find_and_alloc_blocks(inode, (sector_t)pgoff, max_blocks, result,
					create);
	return rc;
}

int pmfs_get_xip_mem(struct address_space *mapping, pgoff_t pgoff,
		     unsigned long max_blocks, int create,
		      void **kmem, unsigned long *pfn)
{
	int rc;
	u64 block = 0;
	struct inode *inode = mapping->host;
	timing_t read__pmfs_get_block_time;

	if (create == 0) {
		PMFS_START_TIMING(read__pmfs_get_block_t, read__pmfs_get_block_time);
	}
	rc = __pmfs_get_block(inode, pgoff, max_blocks, create, &block);
	if (rc <= 0) {
		pmfs_dbg1("[%s:%d] rc(%d), sb->physaddr(0x%llx), block(0x%llx),"
			" pgoff(0x%lx), flag(0x%x), PFN(0x%lx)\n", __func__,
			__LINE__, rc, PMFS_SB(inode->i_sb)->phys_addr,
			block, pgoff, create, *pfn);
		return rc;
	}
	if (create == 0) {
		PMFS_END_TIMING(read__pmfs_get_block_t, read__pmfs_get_block_time);
	}

	*kmem = pmfs_get_block(inode->i_sb, block);
	*pfn = pmfs_get_pfn(inode->i_sb, block);

	pmfs_dbg_mmapvv("[%s:%d] sb->physaddr(0x%llx), block(0x%llx),"
		" pgoff(0x%lx), flag(0x%x), PFN(0x%lx)\n", __func__, __LINE__,
		PMFS_SB(inode->i_sb)->phys_addr, block, pgoff, create, *pfn);
	return rc;
}

static vm_fault_t pmfs_xip_huge_file_fault(struct vm_fault *vmf,
				    enum page_entry_size pe_size)
{
	vm_fault_t ret;
	int error = 0;
	pfn_t pfn;
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	pmfs_dbg_verbose("%s: inode %lu, pgoff %lu, pe_size %d\n",
			 __func__, inode->i_ino, vmf->pgoff, pe_size);

	if (vmf->flags & FAULT_FLAG_WRITE)
		file_update_time(vmf->vma->vm_file);

	ret = dax_iomap_fault(vmf, pe_size, &pfn, &error, &pmfs_iomap_ops_lock);

	return ret;

}

static vm_fault_t pmfs_dax_pfn_mkwrite(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	pmfs_dbg_mmapvv("%s: inode %lu, pgoff %lu, flags 0x%x\n",
			__func__, inode->i_ino, vmf->pgoff, vmf->flags);

	return pmfs_xip_huge_file_fault(vmf, PE_SIZE_PTE);
}

static vm_fault_t pmfs_dax_fault(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	pmfs_dbg_verbose("%s: inode %lu, pgoff %lu, flags 0x%x\n",
		  __func__, inode->i_ino, vmf->pgoff, vmf->flags);

	return pmfs_xip_huge_file_fault(vmf, PE_SIZE_PTE);
}

static inline int pmfs_rbtree_compare_vma(struct vma_item *curr,
	struct vm_area_struct *vma)
{
	if (vma < curr->vma)
		return -1;
	if (vma > curr->vma)
		return 1;

	return 0;
}

int pmfs_insert_write_vma(struct vm_area_struct *vma)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	struct pmfs_inode_info *si = PMFS_I(inode);
	struct pmfs_inode_info_header *sih = &si->header;
	struct super_block *sb = inode->i_sb;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	unsigned long flags = VM_SHARED | VM_WRITE;
	struct vma_item *item, *curr;
	struct rb_node **temp, *parent;
	int compVal;
	int insert = 0;
	int ret = 0;
	struct pmfs_inode *pi;
	struct process_numa *proc_numa;
	int cpu = pmfs_get_cpuid(sb);


	if ((vma->vm_flags & flags) != flags)
		return 0;

	item = pmfs_alloc_vma_item(sb);
	if (!item) {
		return -ENOMEM;
	}

	item->vma = vma;

	pmfs_dbg_verbose("Inode %lu insert vma %p, start 0x%lx, end 0x%lx, pgoff %lu\n",
			 inode->i_ino, vma, vma->vm_start, vma->vm_end,
			 vma->vm_pgoff);

	pi = pmfs_get_inode(sb, inode->i_ino);

	if (sbi->num_numa_nodes > 1 && pi->numa_node != sbi->cpu_numa_node[cpu]) {
		proc_numa = &(sbi->process_numa[current->tgid % sbi->num_parallel_procs]);
		if (proc_numa->tgid == current->tgid)
			proc_numa->numa_node = pi->numa_node;
		else {
			proc_numa->tgid = current->tgid;
			proc_numa->numa_node = pi->numa_node;
		}

		// sched_setaffinity(current->pid, &(sbi->numa_cpus[pi->numa_node].cpumask));
	}

	inode_lock(inode);

	temp = &(sih->vma_tree.rb_node);
	parent = NULL;

	while (*temp) {
		curr = container_of(*temp, struct vma_item, node);
		compVal = pmfs_rbtree_compare_vma(curr, vma);
		parent = *temp;

		if (compVal == -1) {
			temp = &((*temp)->rb_left);
		} else if (compVal == 1) {
			temp = &((*temp)->rb_right);
		} else {
			pmfs_dbg("%s: vma %p already exists\n",
				__func__, vma);
			kfree(item);
			goto out;
		}
	}

	rb_link_node(&item->node, parent, temp);
	rb_insert_color(&item->node, &sih->vma_tree);

	sih->num_vmas++;
	if (sih->num_vmas == 1)
		insert = 1;

out:
	inode_unlock(inode);

	return ret;
}

static int pmfs_remove_write_vma(struct vm_area_struct *vma)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	struct pmfs_inode_info *si = PMFS_I(inode);
	struct pmfs_inode_info_header *sih = &si->header;
	struct super_block *sb = inode->i_sb;
	struct vma_item *curr = NULL;
	struct rb_node *temp;
	int compVal;
	int found = 0;
	int remove = 0;

	inode_lock(inode);

	temp = sih->vma_tree.rb_node;
	while (temp) {
		curr = container_of(temp, struct vma_item, node);
		compVal = pmfs_rbtree_compare_vma(curr, vma);

		if (compVal == -1) {
			temp = temp->rb_left;
		} else if (compVal == 1) {
			temp = temp->rb_right;
		} else {
			rb_erase(&curr->node, &sih->vma_tree);
			found = 1;
			break;
		}
	}

	if (found) {
		sih->num_vmas--;
		if (sih->num_vmas == 0)
			remove = 1;
	}

	inode_unlock(inode);

	if (found) {
		pmfs_dbg_verbose("Inode %lu remove vma %p, start 0x%lx, end 0x%lx, pgoff %lu\n",
				 inode->i_ino,	curr->vma, curr->vma->vm_start,
				 curr->vma->vm_end, curr->vma->vm_pgoff);
		pmfs_free_vma_item(sb, curr);
	}

	return 0;
}

static void pmfs_vma_open(struct vm_area_struct *vma)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;

	pmfs_dbg_mmap4k("[%s:%d] inode %lu, MMAP 4KPAGE vm_start(0x%lx), vm_end(0x%lx), vm pgoff %lu, %lu blocks, vm_flags(0x%lx), vm_page_prot(0x%lx)\n",
			__func__, __LINE__,
			inode->i_ino, vma->vm_start, vma->vm_end,
			vma->vm_pgoff,
			(vma->vm_end - vma->vm_start) >> PAGE_SHIFT,
			vma->vm_flags,
			pgprot_val(vma->vm_page_prot));

	pmfs_insert_write_vma(vma);
}

static void pmfs_vma_close(struct vm_area_struct *vma)
{
	pmfs_dbg_verbose("[%s:%d] MMAP 4KPAGE vm_start(0x%lx), vm_end(0x%lx), vm_flags(0x%lx), vm_page_prot(0x%lx)\n",
		  __func__, __LINE__, vma->vm_start, vma->vm_end,
		  vma->vm_flags, pgprot_val(vma->vm_page_prot));

	vma->original_write = 0;
	pmfs_remove_write_vma(vma);
}

static const struct vm_operations_struct pmfs_xip_vm_ops = {
	.fault	= pmfs_dax_fault,
	.huge_fault = pmfs_xip_huge_file_fault,
	.page_mkwrite = pmfs_dax_fault,
	.pfn_mkwrite = pmfs_dax_pfn_mkwrite,
	.open = pmfs_vma_open,
	.close = pmfs_vma_close,
};

int pmfs_xip_file_mmap(struct file *file, struct vm_area_struct *vma)
{
//	BUG_ON(!file->f_mapping->a_ops->get_xip_mem);

	file_accessed(file);

	// vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	vm_flags_set(vma, VM_MIXEDMAP | VM_HUGEPAGE);

	vma->vm_ops = &pmfs_xip_vm_ops;

	pmfs_insert_write_vma(vma);

	pmfs_dbg_mmap4k("[%s:%d] MMAP 4KPAGE vm_start(0x%lx),"
			" vm_end(0x%lx), vm_flags(0x%lx), "
			"vm_page_prot(0x%lx)\n", __func__,
			__LINE__, vma->vm_start, vma->vm_end,
			vma->vm_flags, pgprot_val(vma->vm_page_prot));

	return 0;
}
