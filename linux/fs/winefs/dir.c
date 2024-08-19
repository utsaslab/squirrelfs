/*
 * BRIEF DESCRIPTION
 *
 * File operations for directories.
 *
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2009-2011 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright 2003 Sony Corporation
 * Copyright 2003 Matsushita Electric Industrial Co., Ltd.
 * 2003-2004 (c) MontaVista Software, Inc. , Steve Longerbeam
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include "pmfs.h"
#include "inode.h"

/*
 *	Parent is locked.
 */

#define DT2IF(dt) (((dt) << 12) & S_IFMT)
#define IF2DT(sif) (((sif) & S_IFMT) >> 12)

struct pmfs_direntry *pmfs_find_dentry(struct super_block *sb,
				       struct pmfs_inode *pi, struct inode *inode,
				       const char *name, unsigned long name_len)
{
	struct pmfs_inode_info *si = PMFS_I(inode);
	struct pmfs_inode_info_header *sih = &si->header;
	struct pmfs_direntry *direntry = NULL;
	struct pmfs_range_node *ret_node = NULL;
	unsigned long hash;
	int found = 0;

	hash = BKDRHash(name, name_len);

	found = pmfs_find_range_node(&sih->rb_tree, hash, NODE_DIR,
				     &ret_node);
	if (found == 1 && hash == ret_node->hash)
		direntry = ret_node->direntry;

	return direntry;
}

int pmfs_insert_dir_tree(struct super_block *sb,
			 struct pmfs_inode_info_header *sih, const char *name,
			 int namelen, struct pmfs_direntry *direntry)
{
	struct pmfs_range_node *node = NULL;
	unsigned long hash;
	int ret;

	hash = BKDRHash(name, namelen);
	//pmfs_dbg("%s: insert %s hash %lu\n", __func__, name, hash);

	/* FIXME: hash collision ignored here */
	node = pmfs_alloc_dir_node(sb);
	if (!node)
		return -ENOMEM;

	node->hash = hash;
	node->direntry = direntry;
	ret = pmfs_insert_range_node(&sih->rb_tree, node, NODE_DIR);
	if (ret) {
		pmfs_free_dir_node(node);
		pmfs_dbg("%s ERROR %d: %s\n", __func__, ret, name);
	}

	return ret;
}

static int pmfs_check_dentry_match(struct super_block *sb,
	struct pmfs_direntry *dentry, const char *name, int namelen)
{
	if (dentry->name_len != namelen)
		return -EINVAL;

	return strncmp(dentry->name, name, namelen);
}

int pmfs_remove_dir_tree(struct super_block *sb,
	struct pmfs_inode_info_header *sih, const char *name, int namelen,
	struct pmfs_direntry **create_dentry)
{
	struct pmfs_direntry *entry;
	struct pmfs_range_node *ret_node = NULL;
	unsigned long hash;
	int found = 0;

	hash = BKDRHash(name, namelen);
	found = pmfs_find_range_node(&sih->rb_tree, hash, NODE_DIR,
				     &ret_node);
	if (found == 0) {
		pmfs_dbg("%s target not found: %s, length %d, "
				"hash %lu\n", __func__, name, namelen, hash);
		return -EINVAL;
	}

	entry = ret_node->direntry;
	rb_erase(&ret_node->node, &sih->rb_tree);
	pmfs_free_dir_node(ret_node);

	if (!entry) {
		pmfs_dbg("%s ERROR: %s, length %d, hash %lu\n",
			 __func__, name, namelen, hash);
		return -EINVAL;
	}

	if (entry->ino == 0 ||
	    pmfs_check_dentry_match(sb, entry, name, namelen)) {
		pmfs_dbg("%s dentry not match: %s, length %d, hash %lu\n",
			 __func__, name, namelen, hash);
		/* for debug information, still allow access to nvmm */
		pmfs_dbg("dentry: inode %llu, name %s, namelen %u, rec len %u\n",
			 le64_to_cpu(entry->ino),
			 entry->name, entry->name_len,
			 le16_to_cpu(entry->de_len));
		return -EINVAL;
	}

	if (create_dentry)
		*create_dentry = entry;

	return 0;
}

void pmfs_delete_dir_tree(struct super_block *sb,
			  struct pmfs_inode_info_header *sih)
{
	pmfs_dbg_verbose("%s: delete dir %lu\n", __func__, sih->ino);
	pmfs_destroy_range_node_tree(sb, &sih->rb_tree);
}

static int pmfs_add_dirent_to_buf(pmfs_transaction_t *trans,
	struct dentry *dentry, struct inode *inode,
	struct pmfs_direntry *de, u8 *blk_base,  struct pmfs_inode *pidir)
{
	struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned short reclen;
	int nlen, rlen;
	char *top;
	struct pmfs_inode_info *si = PMFS_I(dir);
	struct pmfs_inode_info_header *sih = &si->header;

	/*
	 * This portion sweeps through all the directory
	 * entries to find a free slot to insert this new
	 * directory entry. Needs to be optimized
	 */
	reclen = PMFS_DIR_REC_LEN(namelen);

	if (!de) {
		top = blk_base + dir->i_sb->s_blocksize - reclen;

		if (sih->last_dentry == NULL) {
			return -ENOSPC;
		}

		de = (struct pmfs_direntry *)(sih->last_dentry);
		rlen = le16_to_cpu(de->de_len);
		if (de->ino) {
			nlen = PMFS_DIR_REC_LEN(de->name_len);
			if (rlen - nlen >= reclen) {
				goto found_free_slot;
			}
			else {
				return -ENOSPC;
			}
		}

		if ((char *)de > top)
			return -ENOSPC;

		pmfs_memunlock_block(dir->i_sb, blk_base);
		de->de_len = blk_base + dir->i_sb->s_blocksize - (u8*)de;
		pmfs_memlock_block(dir->i_sb, blk_base);
	}

 found_free_slot:
	if (de->ino) {
		struct pmfs_direntry *de1;
		pmfs_add_logentry(dir->i_sb, trans, &de->de_len,
				  sizeof(de->de_len), LE_DATA);
		nlen = PMFS_DIR_REC_LEN(de->name_len);
		de1 = (struct pmfs_direntry *)((char *) de + nlen);
		pmfs_memunlock_block(dir->i_sb, blk_base);
		de1->de_len = blk_base + dir->i_sb->s_blocksize - (u8*)de1;
		de->de_len = cpu_to_le16(nlen);
		pmfs_memlock_block(dir->i_sb, blk_base);
		de = de1;
	} else {
		pmfs_add_logentry(dir->i_sb, trans, &de->ino,
				  sizeof(de->ino), LE_DATA);
	}

	pmfs_memunlock_block(dir->i_sb, blk_base);
	if (inode) {
		de->ino = cpu_to_le64(inode->i_ino);
	} else {
		de->ino = 0;
	}

	sih->last_dentry = (struct pmfs_direntry *) (de);

	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	pmfs_memlock_block(dir->i_sb, blk_base);
	pmfs_flush_buffer(de, reclen, false);
	/*
	 * XXX shouldn't update any times until successful
	 * completion of syscall, but too many callers depend
	 * on this.
	 */
	dir->i_mtime = dir->i_ctime = current_time(dir);
	/*dir->i_version++; */

	pmfs_memunlock_inode(dir->i_sb, pidir);
	pidir->i_mtime = cpu_to_le32(dir->i_mtime.tv_sec);
	pidir->i_ctime = cpu_to_le32(dir->i_ctime.tv_sec);
	pmfs_memlock_inode(dir->i_sb, pidir);

	pmfs_insert_dir_tree(dir->i_sb, sih, name, namelen, de);
	return 0;
}

/* adds a directory entry pointing to the inode. assumes the inode has
 * already been logged for consistency
 */
int pmfs_add_entry(pmfs_transaction_t *trans, struct dentry *dentry,
		struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct super_block *sb = dir->i_sb;
	int retval = -EINVAL;
	unsigned long block, blocks;
	struct pmfs_direntry *de;
	char *blk_base;
	struct pmfs_inode *pidir;
	u64 bp = 0;

	if (!dentry->d_name.len)
		return -EINVAL;

	pidir = pmfs_get_inode(sb, dir->i_ino);
	pmfs_add_logentry(sb, trans, pidir, MAX_DATA_PER_LENTRY, LE_DATA);

	blocks = dir->i_size >> sb->s_blocksize_bits;
	block = blocks - 1;

	//for (block = 0; block < blocks; block++) {

	if (block >= 0) {
		pmfs_find_data_blocks(dir, block, &bp, 1);
		blk_base =
			pmfs_get_block(sb, bp);
		if (!blk_base) {
			retval = -EIO;
			goto out;
		}
		retval = pmfs_add_dirent_to_buf(trans, dentry, inode,
						NULL, blk_base, pidir);
		if (retval != -ENOSPC)
			goto out;
	}
	//}

	retval = pmfs_alloc_blocks_weak(trans, dir, blocks, 1, false,
					ANY_CPU, 0);
	if (retval)
		goto out;

	dir->i_size += dir->i_sb->s_blocksize;
	pmfs_update_isize(dir, pidir);

	pmfs_find_data_blocks(dir, blocks, &bp, 1);
	blk_base = pmfs_get_block(sb, bp);
	if (!blk_base) {
		retval = -ENOSPC;
		goto out;
	}
	/* No need to log the changes to this de because its a new block */
	de = (struct pmfs_direntry *)blk_base;
	pmfs_memunlock_block(sb, blk_base);
	de->ino = 0;
	//de->de_len = PMFS_DIR_REC_LEN(dentry->d_name.len);
	de->de_len = dir->i_sb->s_blocksize;
	pmfs_memlock_block(sb, blk_base);
	/* Since this is a new block, no need to log changes to this block */
	retval = pmfs_add_dirent_to_buf(NULL, dentry, inode, de, blk_base,
					pidir);
out:
	return retval;
}

/* removes a directory entry pointing to the inode. assumes the inode has
 * already been logged for consistency
 */
int pmfs_remove_entry(pmfs_transaction_t *trans, struct dentry *de,
		struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct inode *dir = de->d_parent->d_inode;
	struct pmfs_inode *pidir;
	struct qstr *entry = &de->d_name;
	struct pmfs_direntry *res_entry;
	int retval = -EINVAL;
	char *blk_base = NULL;
	struct pmfs_inode_info *si = PMFS_I(dir);
	struct pmfs_inode_info_header *sih = &si->header;

	if (!de->d_name.len)
		return -EINVAL;

	retval = pmfs_remove_dir_tree(sb, sih, entry->name, entry->len,
				      &res_entry);

	pmfs_add_logentry(sb, trans, &res_entry->ino,
			  sizeof(res_entry->ino), LE_DATA);
	pmfs_memunlock_block(sb, blk_base);
	res_entry->ino = 0;
	pmfs_memlock_block(sb, blk_base);

	/*

	blocks = dir->i_size >> sb->s_blocksize_bits;

	for (block = 0; block < blocks; block++) {
		blk_base =
			pmfs_get_block(sb, pmfs_find_data_block(dir, block));
		if (!blk_base)
			goto out;
		if (pmfs_search_dirblock(blk_base, dir, entry,
					  block << sb->s_blocksize_bits,
					  &res_entry, &prev_entry) == 1)
			break;
	}

	if (block == blocks)
		goto out;
	if (prev_entry) {
		pmfs_add_logentry(sb, trans, &prev_entry->de_len,
				sizeof(prev_entry->de_len), LE_DATA);
		pmfs_memunlock_block(sb, blk_base);
		prev_entry->de_len =
			cpu_to_le16(le16_to_cpu(prev_entry->de_len) +
				    le16_to_cpu(res_entry->de_len));
		pmfs_memlock_block(sb, blk_base);
	} else {
		pmfs_add_logentry(sb, trans, &res_entry->ino,
				sizeof(res_entry->ino), LE_DATA);
		pmfs_memunlock_block(sb, blk_base);
		res_entry->ino = 0;
		pmfs_memlock_block(sb, blk_base);
	}

	*/

	/*dir->i_version++; */
	dir->i_ctime = dir->i_mtime = current_time(dir);

	pidir = pmfs_get_inode(sb, dir->i_ino);
	pmfs_add_logentry(sb, trans, pidir, MAX_DATA_PER_LENTRY, LE_DATA);

	pmfs_memunlock_inode(sb, pidir);
	pidir->i_mtime = cpu_to_le32(dir->i_mtime.tv_sec);
	pidir->i_ctime = cpu_to_le32(dir->i_ctime.tv_sec);
	pmfs_memlock_inode(sb, pidir);
	retval = 0;
//out:
	return retval;
}

static int pmfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi;
	char *blk_base;
	unsigned long offset;
	struct pmfs_direntry *de;
	ino_t ino;
	timing_t readdir_time;

	PMFS_START_TIMING(readdir_t, readdir_time);

	offset = ctx->pos & (sb->s_blocksize - 1);
	while (ctx->pos < inode->i_size) {
		unsigned long blk = ctx->pos >> sb->s_blocksize_bits;
		u64 bp = 0;
		pmfs_find_data_blocks(inode, blk, &bp, 1);
		blk_base =
			pmfs_get_block(sb, bp);
		if (!blk_base) {
			pmfs_dbg("directory %lu contains a hole at offset %lld\n",
				inode->i_ino, ctx->pos);
			ctx->pos += sb->s_blocksize - offset;
			continue;
		}

		while (ctx->pos < inode->i_size
		       && offset < sb->s_blocksize) {

			de = (struct pmfs_direntry *)(blk_base + offset);

			if (!pmfs_check_dir_entry("pmfs_readdir", inode, de,
						   blk_base, offset)) {
				/* On error, skip to the next block. */
				ctx->pos = ALIGN(ctx->pos, sb->s_blocksize);
				break;
			}
			offset += le16_to_cpu(de->de_len);
			if (de->ino) {
				ino = le64_to_cpu(de->ino);
				pi = pmfs_get_inode(sb, ino);
				if (!dir_emit(ctx, de->name, de->name_len,
					ino, IF2DT(le16_to_cpu(pi->i_mode))))
					return 0;
			}
			ctx->pos += le16_to_cpu(de->de_len);
		}
		offset = 0;
	}
	PMFS_END_TIMING(readdir_t, readdir_time);
	return 0;
}

const struct file_operations pmfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate	= pmfs_readdir,
	.fsync		= noop_fsync,
	.unlocked_ioctl = pmfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= pmfs_compat_ioctl,
#endif
};
