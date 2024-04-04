/*
 * PMFS emulated persistence. This file contains code to 
 * handle data blocks of various sizes efficiently.
 *
 * Persistent Memory File System
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "pmfs.h"
#include "inode.h"

#define PAGES_PER_2MB 512
#define PAGES_PER_2MB_MASK (PAGES_PER_2MB - 1)
#define IS_BLOCK_2MB_ALIGNED(block) \
	(!(block & PAGES_PER_2MB_MASK))

void pmfs_init_header(struct super_block *sb,
		      struct pmfs_inode_info_header *sih, u16 i_mode)
{
	sih->i_size = 0;
	sih->ino = 0;
	sih->i_blocks = 0;
	sih->rb_tree = RB_ROOT;
	sih->vma_tree = RB_ROOT;
	sih->num_vmas = 0;
	sih->i_mode = i_mode;
	sih->i_flags = 0;
	sih->i_blk_type = PMFS_DEFAULT_BLOCK_TYPE;
	sih->last_dentry = NULL;
}

static inline void set_scan_bm(unsigned long bit,
	struct single_scan_bm *scan_bm)
{
	set_bit(bit, scan_bm->bitmap);
}

inline void set_bm(unsigned long bit, struct scan_bitmap *bm,
	enum bm_type type)
{
	switch (type) {
	case BM_4K:
		set_scan_bm(bit, &bm->scan_bm_4K);
		break;
	case BM_2M:
		set_scan_bm(bit, &bm->scan_bm_2M);
		break;
	case BM_1G:
		set_scan_bm(bit, &bm->scan_bm_1G);
		break;
	default:
		break;
	}
}

static void pmfs_clear_datablock_inode(struct super_block *sb)
{
	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_BLOCKNODE_IN0);
	pmfs_transaction_t *trans;
	__le64 root = pi->root;
	unsigned int height = pi->height;
	unsigned int btype = pi->i_blk_type;
	unsigned long last_blocknr;

	/* pi->i_size can not be zero */
	last_blocknr = (le64_to_cpu(pi->i_size) - 1) >>
					pmfs_inode_blk_shift(pi);

	/* 2 log entry for inode */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES, pmfs_get_cpuid(sb));
	if (IS_ERR(trans))
		return;
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	pmfs_memunlock_inode(sb, pi);
	memset(pi, 0, MAX_DATA_PER_LENTRY);
	pmfs_memlock_inode(sb, pi);

	/* commit the transaction */
	pmfs_commit_transaction(sb, trans);

	pmfs_free_inode_subtree(sb, root, height, btype, last_blocknr);
}

static void pmfs_clear_inodelist_inode(struct super_block *sb)
{
	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_INODELIST_IN0);
	pmfs_transaction_t *trans;
	__le64 root = pi->root;
	unsigned int height = pi->height;
	unsigned int btype = pi->i_blk_type;
	unsigned long last_blocknr;

	/* pi->i_size can not be zero */
	last_blocknr = (le64_to_cpu(pi->i_size) - 1) >>
					pmfs_inode_blk_shift(pi);

	/* 2 log entry for inode */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES, pmfs_get_cpuid(sb));
	if (IS_ERR(trans))
		return;
	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	pmfs_memunlock_inode(sb, pi);
	memset(pi, 0, MAX_DATA_PER_LENTRY);
	pmfs_memlock_inode(sb, pi);

	/* commit the transaction */
	pmfs_commit_transaction(sb, trans);

	pmfs_free_inode_subtree(sb, root, height, btype, last_blocknr);
}

static int pmfs_failure_insert_inodetree(struct super_block *sb,
	unsigned long ino_low, unsigned long ino_high)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct inode_map *inode_map;
	struct pmfs_range_node *prev = NULL, *next = NULL;
	struct pmfs_range_node *new_node;
	unsigned long internal_low, internal_high;
	int cpu;
	struct rb_root *tree;
	int ret;

	if (ino_low > ino_high) {
		pmfs_err(sb, "%s: ino low %lu, ino high %lu\n",
			 __func__, ino_low, ino_high);
		BUG();
	}

	cpu = ino_low % sbi->cpus;
	if (ino_high % sbi->cpus != cpu) {
		pmfs_err(sb, "%s: ino low %lu, ino high %lu\n",
			 __func__, ino_low, ino_high);
		BUG();
	}

	internal_low = ino_low / sbi->cpus;
	internal_high = ino_high / sbi->cpus;
	inode_map = &sbi->inode_maps[cpu];
	tree = &inode_map->inode_inuse_tree;
	mutex_lock(&inode_map->inode_table_mutex);

	ret = pmfs_find_free_slot(tree, internal_low, internal_high,
				  &prev, &next);
	if (ret) {
		pmfs_dbg("%s: ino %lu - %lu already exists!: %d\n",
			 __func__, ino_low, ino_high, ret);
		mutex_unlock(&inode_map->inode_table_mutex);
		return ret;
	}

	if (prev && next && (internal_low == prev->range_high + 1) &&
			(internal_high + 1 == next->range_low)) {
		/* fits the hole */
		rb_erase(&next->node, tree);
		inode_map->num_range_node_inode--;
		prev->range_high = next->range_high;
		pmfs_free_inode_node(sb, next);
		goto finish;
	}
	if (prev && (internal_low == prev->range_high + 1)) {
		/* Aligns left */
		prev->range_high += internal_high - internal_low + 1;
		goto finish;
	}
	if (next && (internal_high + 1 == next->range_low)) {
		/* Aligns right */
		next->range_low -= internal_high - internal_low + 1;
		goto finish;
	}

	/* Aligns somewhere in the middle */
	new_node = pmfs_alloc_inode_node(sb);
	PMFS_ASSERT(new_node);
	new_node->range_low = internal_low;
	new_node->range_high = internal_high;
	ret = pmfs_insert_inodetree(sbi, new_node, cpu);
	if (ret) {
		pmfs_err(sb, "%s failed\n", __func__);
		pmfs_free_inode_node(sb, new_node);
		goto finish;
	}
	inode_map->num_range_node_inode++;

finish:
	mutex_unlock(&inode_map->inode_table_mutex);
	return ret;
}

static void pmfs_destroy_blocknode_tree(struct super_block *sb, int cpu)
{
	struct free_list *free_list;

	free_list = pmfs_get_free_list(sb, cpu);
	pmfs_destroy_range_node_tree(sb, &free_list->unaligned_block_free_tree);
	pmfs_destroy_range_node_tree(sb, &free_list->huge_aligned_block_free_tree);
}

static void pmfs_destroy_blocknode_trees(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	int i;

	for (i = 0; i < sbi->cpus; i++) {
		pmfs_destroy_blocknode_tree(sb, i);
	}
}

static int pmfs_init_blockmap_from_inode(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi = pmfs_get_inode(sb, PMFS_BLOCKNODE_IN0);
	struct free_list *free_list;
	struct pmfs_range_node_lowhigh *entry;
	struct pmfs_range_node *blknode;
	u64 curr_p;
	u64 cpuid;
	int ret = 0;
	int block_ctr = 0;
	int node_ctr = 0;
	int i = 0;
	unsigned long total_blocknodes = sbi->num_blocknode_allocated;
	unsigned long blocknode_ctr = 0;
	struct pmfs_range_node *curr;
	struct rb_node *temp;

	sbi->num_blocknode_allocated = 0;

	pmfs_dbg("total_blocknodes = %lu, num_blocks = %lu, num_free_blocks = %lu\n",
		 total_blocknodes, sbi->num_blocks, sbi->num_free_blocks);

	for (block_ctr = 0; block_ctr < pi->i_size / sbi->blocksize; block_ctr++) {
		__pmfs_find_data_blocks(sb, pi, block_ctr, &curr_p, 1);

		for (node_ctr = 0; node_ctr < RANGENODE_PER_PAGE; node_ctr++) {

			if (blocknode_ctr == total_blocknodes)
				break;

			entry = (struct pmfs_range_node_lowhigh *)pmfs_get_block(sb, curr_p);
			blknode = pmfs_alloc_blocknode(sb);
			if (blknode == NULL)
				PMFS_ASSERT(0);
			blknode->range_low = le64_to_cpu(entry->range_low);
			blknode->range_high = le64_to_cpu(entry->range_high);
			cpuid = get_block_cpuid(sbi, blknode->range_low);

			/* FIXME: Assume NR_CPUS not change */
			free_list = pmfs_get_free_list(sb, cpuid);
			if (blknode->range_low % 512 == 0 &&
			    blknode->range_high - blknode->range_low + 1 == 512) {
				ret = pmfs_insert_blocktree(&free_list->huge_aligned_block_free_tree, blknode);
				free_list->num_blocknode_huge_aligned++;
				if (free_list->num_blocknode_huge_aligned == 1) {
					free_list->first_node_huge_aligned = blknode;
				}
			} else {
				ret = pmfs_insert_blocktree(&free_list->unaligned_block_free_tree, blknode);
				free_list->num_blocknode_unaligned++;
				if (free_list->num_blocknode_unaligned == 1)
					free_list->first_node_unaligned = blknode;
			}
			if (ret) {
				pmfs_err(sb, "%s failed\n", __func__);
				pmfs_free_blocknode(sb, blknode);
				PMFS_ASSERT(0);
				pmfs_destroy_blocknode_trees(sb);
				goto out;
			}
			free_list->num_free_blocks += blknode->range_high - blknode->range_low + 1;
			curr_p += sizeof(struct pmfs_range_node_lowhigh);
			blocknode_ctr++;
		}
	}
out:

	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);

		temp = rb_first(&free_list->unaligned_block_free_tree);
		if (temp) {
			curr = container_of(temp, struct pmfs_range_node, node);
			free_list->first_node_unaligned = curr;
		}

		temp = rb_first(&free_list->huge_aligned_block_free_tree);
		if (temp) {
			curr = container_of(temp, struct pmfs_range_node, node);
			free_list->first_node_huge_aligned = curr;
		}

		/*
		 * pmfs_dbg() causes segfault when uncommented, when first_node is NULL
		 */
		/*
		pmfs_dbg("%s: free list %d: block start %lu, end %lu, "
			 "%lu free blocks. num_aligned_nodes = %lu, "
			 "num_unaligned_nodes = %lu, "
			 "huge_aligned_blocknode_start = %lu, "
			 "unaligned_blocknode_start = %lu\n",
			 __func__, i,
			 free_list->block_start,
			 free_list->block_end,
			 free_list->num_free_blocks,
			 free_list->num_blocknode_huge_aligned,
			 free_list->num_blocknode_unaligned,
			 free_list->first_node_huge_aligned->range_low,
			 free_list->first_node_unaligned->range_low);
		*/
	}
	return ret;
}

static void pmfs_destroy_inode_trees(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct inode_map *inode_map;
	int i;

	for (i = 0; i < sbi->cpus; i++) {
		inode_map = &sbi->inode_maps[i];
		pmfs_destroy_range_node_tree(sb,
					     &inode_map->inode_inuse_tree);
	}
}

#define CPUID_MASK 0xff00000000000000
static int pmfs_init_inode_list_from_inode(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi = pmfs_get_inode(sb, PMFS_INODELIST_IN0);
	struct pmfs_range_node_lowhigh *entry;
	struct pmfs_range_node *range_node;
	u64 curr_p;
	u64 cpuid;
	int ret = 0;
	int block_ctr = 0;
	unsigned long total_inode_nodes = sbi->num_inodenode_allocated;
	unsigned long num_inode_node = 0;
	unsigned long inode_node_ctr = 0;
	struct inode_map *inode_map;

	sbi->num_inodenode_allocated = 0;
	sbi->s_inodes_used_count = 0;

	pmfs_dbg("%s: total inode nodes = %lu, pi->i_size = %lu\n", __func__, total_inode_nodes, pi->i_size);
	for (block_ctr = 0; block_ctr < pi->i_size / sbi->blocksize; block_ctr++) {
		__pmfs_find_data_blocks(sb, pi, block_ctr, &curr_p, 1);

    for (inode_node_ctr = 0; inode_node_ctr < RANGENODE_PER_PAGE; inode_node_ctr++) {

			if (num_inode_node == total_inode_nodes)
				break;

			entry = (struct pmfs_range_node_lowhigh *)pmfs_get_block(sb, curr_p);
			range_node = pmfs_alloc_inode_node(sb);
			if (range_node == NULL)
				PMFS_ASSERT(0);

			cpuid = (entry->range_low & CPUID_MASK) >> 56;
			if (cpuid >= sbi->cpus) {
				pmfs_err(sb, "Invalid cpuid %lu\n", cpuid);
				pmfs_free_inode_node(sb, range_node);
				PMFS_ASSERT(0);
				pmfs_destroy_inode_trees(sb);
				goto out;
			}

			range_node->range_low = entry->range_low & ~CPUID_MASK;
			range_node->range_high = entry->range_high;
			ret = pmfs_insert_inodetree(sbi, range_node, cpuid);
			if (ret) {
				pmfs_err(sb, "%s failed\n", __func__);
				pmfs_free_inode_node(sb, range_node);
				PMFS_ASSERT(0);
				pmfs_destroy_inode_trees(sb);
				goto out;
			}

			sbi->s_inodes_used_count +=
				range_node->range_high - range_node->range_low + 1;
			num_inode_node++;

			inode_map = &sbi->inode_maps[cpuid];
			inode_map->num_range_node_inode++;
			if (!inode_map->first_inode_range)
				inode_map->first_inode_range = range_node;

			curr_p += sizeof(struct pmfs_range_node_lowhigh);
		}
	}

	pmfs_dbg("%s: s_inodes_used_count = %lu\n",
		 __func__, sbi->s_inodes_used_count);
out:
	return ret;
}

// static bool pmfs_can_skip_full_scan(struct super_block *sb)
// {
// 	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_BLOCKNODE_IN0);
// 	struct pmfs_super_block *super = pmfs_get_super(sb);
// 	struct pmfs_sb_info *sbi = PMFS_SB(sb);
// 	__le64 root;
// 	unsigned int height, btype;
// 	unsigned long last_blocknr;

// 	if (!pi->root)
// 		return false;

// 	sbi->num_blocknode_allocated =
// 		le64_to_cpu(super->s_num_blocknode_allocated);
// 	sbi->num_inodenode_allocated =
// 		le64_to_cpu(super->s_num_inodenode_allocated);
// 	sbi->num_free_blocks = le64_to_cpu(super->s_num_free_blocks);
// 	sbi->s_inodes_count = le32_to_cpu(super->s_inodes_count);
// 	sbi->s_free_inodes_count = le32_to_cpu(super->s_free_inodes_count);
// 	sbi->s_inodes_used_count = le32_to_cpu(super->s_inodes_used_count);
// 	sbi->s_free_inode_hint = le32_to_cpu(super->s_free_inode_hint);

// 	pmfs_init_blockmap_from_inode(sb);

// 	root = pi->root;
// 	height = pi->height;
// 	btype = pi->i_blk_type;
// 	/* pi->i_size can not be zero */
// 	last_blocknr = (le64_to_cpu(pi->i_size) - 1) >>
// 					pmfs_inode_blk_shift(pi);

// 	/* Clearing the datablock inode */
// 	pmfs_clear_datablock_inode(sb);

// 	pmfs_free_inode_subtree(sb, root, height, btype, last_blocknr);

// 	return true;
// }

static int pmfs_allocate_datablock_block_inode(pmfs_transaction_t *trans,
	struct super_block *sb, struct pmfs_inode *pi, unsigned long num_blocks)
{
	int errval;

	pmfs_memunlock_inode(sb, pi);
	pi->i_mode = 0;
	pi->i_links_count = cpu_to_le16(1);
	pi->i_blk_type = PMFS_BLOCK_TYPE_4K;
	pi->i_flags = 0;
	pi->height = 0;
	pi->i_dtime = 0;
	pi->i_size = cpu_to_le64(num_blocks << sb->s_blocksize_bits);
	pmfs_memlock_inode(sb, pi);

	errval = __pmfs_alloc_blocks_wrapper(trans, sb, pi, 0,
					     num_blocks, false, 0, 0);
	return errval;
}

static u64 pmfs_append_range_node_entry(struct super_block *sb,
	struct pmfs_range_node *curr, void *p, unsigned long cpuid)
{
	struct pmfs_range_node_lowhigh *entry;

	entry = p;

	//pmfs_memunlock_range(sb, entry, size);
	entry->range_low = cpu_to_le64(curr->range_low);
	if (cpuid) {
		PMFS_ASSERT(((entry->range_low & (cpuid << 56)) == 0))
		entry->range_low |= cpu_to_le64(cpuid << 56);
	}
	entry->range_high = cpu_to_le64(curr->range_high);
	//pmfs_memlock_range(sb, entry, size);
	pmfs_dbg_verbose("append entry block low 0x%lx, high 0x%lx\n",
		 curr->range_low, curr->range_high);

	pmfs_flush_buffer(entry, sizeof(struct pmfs_range_node_lowhigh), true);
	return 0;
}

static u64 pmfs_save_range_nodes(struct super_block *sb, struct pmfs_inode *pi,
	struct rb_root *tree, u64 blocknode_ctr, unsigned long cpuid)
{
	struct pmfs_range_node *curr;
	struct rb_node *temp;
	size_t size = sizeof(struct pmfs_range_node_lowhigh);
	u64 blocknr = 0;
	unsigned long p;
	u64 bp;
	int i;

	if (blocknode_ctr != 0) {
		blocknr = blocknode_ctr / RANGENODE_PER_PAGE;
		__pmfs_find_data_blocks(sb, pi, blocknr, &bp, 1);
		p = (unsigned long)pmfs_get_block(sb, bp);
		for (i = 0; i < (blocknode_ctr % RANGENODE_PER_PAGE); i++) {
			p += (unsigned long) size;
		}
	}

	/* Save in increasing order */
	temp = rb_first(tree);
	while (temp) {
		curr = container_of(temp, struct pmfs_range_node, node);

		if (blocknode_ctr % RANGENODE_PER_PAGE == 0) {
			/* Assumes that the blocknode_ctr is perfect multiple of RANGENODE_PER_PAGE */
			blocknr = blocknode_ctr / RANGENODE_PER_PAGE;
			__pmfs_find_data_blocks(sb, pi, blocknr, &bp, 1);
			p = (unsigned long)pmfs_get_block(sb, bp);
		}

		pmfs_append_range_node_entry(sb, curr,
					     (void *)p, cpuid);
		p += (unsigned long)size;
		temp = rb_next(temp);
		rb_erase(&curr->node, tree);
		pmfs_free_range_node(curr);
		blocknode_ctr++;
	}

	return blocknode_ctr;
}

static u64 pmfs_save_free_list_blocknodes(struct super_block *sb, int cpu,
					  struct pmfs_inode *pi, u64 blocknode_ctr)
{
	struct free_list *free_list;

	free_list = pmfs_get_free_list(sb, cpu);
	blocknode_ctr = pmfs_save_range_nodes(sb, pi,
					      &free_list->unaligned_block_free_tree,
					      blocknode_ctr,
					      0);
	blocknode_ctr = pmfs_save_range_nodes(sb, pi,
					      &free_list->huge_aligned_block_free_tree,
					      blocknode_ctr,
					      0);
	return blocknode_ctr;
}

void pmfs_save_blocknode_mappings(struct super_block *sb)
{
	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_BLOCKNODE_IN0);
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	int i;
	struct pmfs_super_block *super;
	pmfs_transaction_t *trans;
	struct free_list *free_list;
	unsigned long num_blocknode = 0;
	unsigned long blocknode_ctr = 0;
	int errval;
	unsigned long num_pages;
	struct inode_map *inode_map;
	unsigned long num_inode_nodes = 0;

	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		num_blocknode += free_list->num_blocknode_unaligned + free_list->num_blocknode_huge_aligned;
	}

	num_pages = num_blocknode / RANGENODE_PER_PAGE;
	if (num_blocknode % RANGENODE_PER_PAGE)
		num_pages++;

	pmfs_dbg_verbose("%s: num blocknodes to save = %lu. num pages = %lu\n",
		 __func__, num_blocknode, num_pages);

	/*
	num_blocks = ((sbi->num_blocknode_allocated * sizeof(struct
		pmfs_range_node_lowhigh) - 1) >> sb->s_blocksize_bits) + 1;
	*/
	/* 2 log entry for inode, 2 lentry for super-block */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES + MAX_METABLOCK_LENTRIES + MAX_SB_LENTRIES, 0);
	if (IS_ERR(trans))
		return;

	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	errval = pmfs_allocate_datablock_block_inode(trans, sb, pi, num_pages);
	pi->pmfs_ino = PMFS_BLOCKNODE_IN0;

	if (errval != 0) {
		pmfs_dbg("Error saving the blocknode mappings: %d\n", errval);
		pmfs_abort_transaction(sb, trans);
		return;
	}

	for (i = 0; i < sbi->cpus; i++) {
		blocknode_ctr = pmfs_save_free_list_blocknodes(sb, i, pi, blocknode_ctr);
	}

  /* There might have been more allocations made for
   * datablock inode, reducing number of free blocknodes
   */
	PMFS_ASSERT(blocknode_ctr <= num_blocknode);

	for (i = 0; i < sbi->cpus; i++) {
		inode_map = &sbi->inode_maps[i];
		num_inode_nodes += inode_map->num_range_node_inode;
	}

	pmfs_dbg("%s: Saving number of allocator blocknodes = %lu, number of inode blocknodes = %lu\n", __func__, blocknode_ctr, num_inode_nodes);

	/* 
	 * save the total allocated blocknode mappings 
	 * in super block
	 */
	super = pmfs_get_super(sb);
	pmfs_add_logentry(sb, trans, &super->s_wtime,
			PMFS_FAST_MOUNT_FIELD_SIZE, LE_DATA);

	pmfs_memunlock_range(sb, &super->s_wtime, PMFS_FAST_MOUNT_FIELD_SIZE);

	super->s_wtime = cpu_to_le32(ktime_get_real_seconds());
	super->s_num_blocknode_allocated = 
			cpu_to_le64(blocknode_ctr);
	super->s_num_inodenode_allocated =
		cpu_to_le64(num_inode_nodes);
	super->s_num_free_blocks = cpu_to_le64(sbi->num_free_blocks);
	super->s_inodes_count = cpu_to_le32(sbi->s_inodes_count);
	super->s_free_inodes_count = cpu_to_le32(sbi->s_free_inodes_count);
	super->s_inodes_used_count = cpu_to_le32(sbi->s_inodes_used_count);
	super->s_free_inode_hint = cpu_to_le32(sbi->s_free_inode_hint);

	pmfs_memlock_range(sb, &super->s_wtime, PMFS_FAST_MOUNT_FIELD_SIZE);
	/* commit the transaction */
	pmfs_commit_transaction(sb, trans);
}

void pmfs_save_inode_list(struct super_block *sb)
{
	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_INODELIST_IN0);
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	pmfs_transaction_t *trans;
	struct inode_map *inode_map;
	unsigned long num_nodes = 0;
	unsigned long blocknode_ctr = 0;
	int i;
	int errval;
	unsigned long num_pages;

	for (i = 0; i < sbi->cpus; i++) {
		inode_map = &sbi->inode_maps[i];
		num_nodes += inode_map->num_range_node_inode;
	}

	num_pages = num_nodes / RANGENODE_PER_PAGE;
	if (num_nodes % RANGENODE_PER_PAGE)
		num_pages++;

	pmfs_dbg_verbose("%s: num inode nodes = %lu. num pages = %lu\n",
		 __func__, num_nodes, num_pages);

	/*
	num_blocks = ((sbi->num_blocknode_allocated * sizeof(struct
		pmfs_range_node_lowhigh) - 1) >> sb->s_blocksize_bits) + 1;
	*/

	/* 2 log entry for inode, 2 lentry for super-block */
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES + MAX_SB_LENTRIES, 0);
	if (IS_ERR(trans))
		return;

	pmfs_add_logentry(sb, trans, pi, MAX_DATA_PER_LENTRY, LE_DATA);

	errval = pmfs_allocate_datablock_block_inode(trans, sb, pi, num_pages);
	pi->pmfs_ino = PMFS_INODELIST_IN0;

	if (errval != 0) {
		pmfs_dbg("Error saving the blocknode mappings: %d\n", errval);
		pmfs_abort_transaction(sb, trans);
		return;
	}

	for (i = 0; i < sbi->cpus; i++) {
		inode_map = &sbi->inode_maps[i];
		blocknode_ctr = pmfs_save_range_nodes(sb, pi,
						      &inode_map->inode_inuse_tree,
						      blocknode_ctr,
						      i);
	}

	PMFS_ASSERT(blocknode_ctr == num_nodes)

	/* commit the transaction */
	pmfs_commit_transaction(sb, trans);
}

static int pmfs_insert_blocknode_map(struct super_block *sb,
	int cpuid, unsigned long low, unsigned long high)
{
	struct free_list *free_list;
	struct rb_root *tree;
	struct pmfs_range_node *blknode = NULL;
	unsigned long num_blocks = 0;
	int ret;

	num_blocks = high - low + 1;
	pmfs_dbg_verbose("%s: cpu %d, low %lu, high %lu, num %lu\n",
		__func__, cpuid, low, high, num_blocks);
	free_list = pmfs_get_free_list(sb, cpuid);
	tree = &(free_list->unaligned_block_free_tree);

	blknode = pmfs_alloc_blocknode(sb);
	if (blknode == NULL)
		return -ENOMEM;
	blknode->range_low = low;
	blknode->range_high = high;
	ret = pmfs_insert_blocktree(tree, blknode);
	if (ret) {
		pmfs_err(sb, "%s failed\n", __func__);
		pmfs_free_blocknode(sb, blknode);
		goto out;
	}
	if (!free_list->first_node_unaligned)
		free_list->first_node_unaligned = blknode;
	free_list->num_blocknode_unaligned++;
	free_list->num_free_blocks += num_blocks;
out:
	return ret;
}

static int __pmfs_build_blocknode_map(struct super_block *sb,
	unsigned long *bitmap, unsigned long bsize, unsigned long scale)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list *free_list;
	unsigned long next = 0;
	unsigned long low = 0;
	unsigned long start, end;
	int cpuid = 0;

	/*
	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		free_list->num_free_blocks = 0;
	}
	*/

	free_list = pmfs_get_free_list(sb, cpuid);
	start = free_list->block_start;
	end = free_list->block_end + 1;
	while (1) {
		next = find_next_zero_bit(bitmap, end, start);
		if (next == bsize)
			break;
		if (next == end) {
			if (cpuid == sbi->cpus - 1)
				break;

			cpuid++;
			free_list = pmfs_get_free_list(sb, cpuid);
			start = free_list->block_start;
			end = free_list->block_end + 1;
			continue;
		}

		low = next;
		next = find_next_bit(bitmap, end, next);
		if (pmfs_insert_blocknode_map(sb, cpuid,
				low << scale, (next << scale) - 1)) {
			pmfs_dbg("Error: could not insert %lu - %lu\n",
				low << scale, ((next << scale) - 1));
		}
		start = next;
		if (next == bsize)
			break;
		if (next == end) {
			if (cpuid == sbi->cpus - 1)
				break;

			cpuid++;
			free_list = pmfs_get_free_list(sb, cpuid);
			start = free_list->block_start;
			end = free_list->block_end + 1;
		}
	}
	return 0;
}

static void pmfs_update_4K_map(struct super_block *sb,
	struct scan_bitmap *bm,	unsigned long *bitmap,
	unsigned long bsize, unsigned long scale)
{
	unsigned long next = 0;
	unsigned long low = 0;
	int i;

	while (1) {
		next = find_next_bit(bitmap, bsize, next);
		if (next == bsize)
			break;
		low = next;
		next = find_next_zero_bit(bitmap, bsize, next);
		for (i = (low << scale); i < (next << scale); i++)
			set_bm(i, bm, BM_4K);
		if (next == bsize)
			break;
	}
}

struct scan_bitmap *global_bm[MAX_CPUS];

static int pmfs_build_blocknode_map(struct super_block *sb,
	unsigned long initsize, unsigned long initsize_2)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct scan_bitmap *bm;
	struct scan_bitmap *final_bm;
	unsigned long *src, *dst;
	int i, j;
	int num;
	int ret;

	final_bm = kzalloc(sizeof(struct scan_bitmap), GFP_KERNEL);
	if (!final_bm)
		return -ENOMEM;

	final_bm->scan_bm_4K.bitmap_size =
				((initsize + initsize_2) >> (PAGE_SHIFT + 0x3));

	/* Alloc memory to hold the block alloc bitmap */
	final_bm->scan_bm_4K.bitmap = kvzalloc(final_bm->scan_bm_4K.bitmap_size,
					      GFP_KERNEL);

	if (!final_bm->scan_bm_4K.bitmap) {
		kfree(final_bm);
		return -ENOMEM;
	}

	/*
	 * We are using free lists. Set 2M and 1G blocks in 4K map,
	 * and use 4K map to rebuild block map.
	 */
	for (i = 0; i < sbi->cpus; i++) {
		bm = global_bm[i];
		pmfs_update_4K_map(sb, bm, bm->scan_bm_2M.bitmap,
			bm->scan_bm_2M.bitmap_size * 8, PAGE_SHIFT_2M - 12);
		pmfs_update_4K_map(sb, bm, bm->scan_bm_1G.bitmap,
			bm->scan_bm_1G.bitmap_size * 8, PAGE_SHIFT_1G - 12);
	}

	/* Merge per-CPU bms to the final single bm */
	num = final_bm->scan_bm_4K.bitmap_size / sizeof(unsigned long);
	if (final_bm->scan_bm_4K.bitmap_size % sizeof(unsigned long))
		num++;

	for (i = 0; i < sbi->cpus; i++) {
		bm = global_bm[i];
		src = (unsigned long *)bm->scan_bm_4K.bitmap;
		dst = (unsigned long *)final_bm->scan_bm_4K.bitmap;

		for (j = 0; j < num; j++)
			dst[j] |= src[j];
	}

	ret = __pmfs_build_blocknode_map(sb, final_bm->scan_bm_4K.bitmap,
			final_bm->scan_bm_4K.bitmap_size * 8, PAGE_SHIFT - 12);

	kvfree(final_bm->scan_bm_4K.bitmap);
	kfree(final_bm);

	return ret;
}

static void free_bm(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct scan_bitmap *bm;
	int i;

	for (i = 0; i < sbi->cpus; i++) {
		bm = global_bm[i];
		if (bm) {
			kvfree(bm->scan_bm_4K.bitmap);
			kvfree(bm->scan_bm_2M.bitmap);
			kvfree(bm->scan_bm_1G.bitmap);
			kfree(bm);
		}
	}
}

static int alloc_bm(struct super_block *sb, unsigned long initsize, unsigned long initsize_2)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct scan_bitmap *bm;
	int i;

	for (i = 0; i < sbi->cpus; i++) {
		bm = kzalloc(sizeof(struct scan_bitmap), GFP_KERNEL);
		if (!bm)
			return -ENOMEM;

		global_bm[i] = bm;

		bm->scan_bm_4K.bitmap_size =
				((initsize + initsize_2) >> (PAGE_SHIFT + 0x3));
		bm->scan_bm_2M.bitmap_size =
				((initsize + initsize_2) >> (PAGE_SHIFT_2M + 0x3));
		bm->scan_bm_1G.bitmap_size =
				((initsize + initsize_2) >> (PAGE_SHIFT_1G + 0x3));

		/* Alloc memory to hold the block alloc bitmap */
		bm->scan_bm_4K.bitmap = kvzalloc(bm->scan_bm_4K.bitmap_size,
			GFP_KERNEL);
		bm->scan_bm_2M.bitmap = kvzalloc(bm->scan_bm_2M.bitmap_size,
			GFP_KERNEL);
		bm->scan_bm_1G.bitmap = kvzalloc(bm->scan_bm_1G.bitmap_size,
			GFP_KERNEL);

		if (!bm->scan_bm_4K.bitmap || !bm->scan_bm_2M.bitmap ||
				!bm->scan_bm_1G.bitmap)
			return -ENOMEM;
	}

	return 0;
}

/************************** PMFS recovery ****************************/

#define MAX_PGOFF	262144

struct task_ring {
	u64 addr0[512];
	u64 addr1[512];		/* Second inode address */
	int num;
	int inodes_used_count;
	u64 *entry_array;
	u64 *nvmm_array;
};

static struct task_ring *task_rings;
static struct task_struct **threads;
wait_queue_head_t finish_wq;
int *finished;

static void pmfs_inode_crawl_recursive(struct super_block *sb,
				struct scan_bitmap *bm, unsigned long block,
				u32 height, u8 btype)
{
	__le64 *node;
	unsigned int i;

	if (height == 0) {
		/* This is the data block */
		if (btype == PMFS_BLOCK_TYPE_4K) {
			set_bit(block >> PAGE_SHIFT, bm->scan_bm_4K.bitmap);
		} else if (btype == PMFS_BLOCK_TYPE_2M) {
			set_bit(block >> PAGE_SHIFT_2M, bm->scan_bm_2M.bitmap);
		} else {
			set_bit(block >> PAGE_SHIFT_1G, bm->scan_bm_1G.bitmap);
		}
		return;
	}

	node = pmfs_get_block(sb, block);
	set_bit(block >> PAGE_SHIFT, bm->scan_bm_4K.bitmap);
	for (i = 0; i < (1 << META_BLK_SHIFT); i++) {
		if (node[i] == 0)
			continue;
		pmfs_inode_crawl_recursive(sb, bm,
			le64_to_cpu(node[i]), height - 1, btype);
	}
}

static inline void pmfs_inode_crawl(struct super_block *sb,
				    struct scan_bitmap *bm,
				    struct pmfs_inode *pi,
				    struct task_ring *ring)
{
	if (pi->root == 0)
		return;

	ring->inodes_used_count++;
	pmfs_inode_crawl_recursive(sb, bm, le64_to_cpu(pi->root), pi->height,
					pi->i_blk_type);
}

// static void pmfs_inode_table_crawl_recursive(struct super_block *sb,
// 				struct scan_bitmap *bm, unsigned long block,
// 				u32 height, u32 btype)
// {
// #if 0
// 	__le64 *node;
// 	unsigned int i;
// 	struct pmfs_inode *pi;
// 	struct pmfs_sb_info *sbi = PMFS_SB(sb);

// 	node = pmfs_get_block(sb, block);

// 	if (height == 0) {
// 		unsigned int inodes_per_block = INODES_PER_BLOCK(btype);
// 		if (likely(btype == PMFS_BLOCK_TYPE_2M))
// 			set_bit(block >> PAGE_SHIFT_2M, bm->scan_bm_2M.bitmap);
// 		else
// 			set_bit(block >> PAGE_SHIFT, bm->scan_bm_4K.bitmap);

// 		sbi->s_inodes_count += inodes_per_block;
// 		for (i = 0; i < inodes_per_block; i++) {
// 			pi = (struct pmfs_inode *)((void *)node +
//                                                         PMFS_INODE_SIZE * i);
// 			if (le16_to_cpu(pi->i_links_count) == 0 &&
//                         	(le16_to_cpu(pi->i_mode) == 0 ||
//                          	le32_to_cpu(pi->i_dtime))) {
// 					/* Empty inode */
// 					continue;
// 			}
// 			sbi->s_inodes_used_count++;
// 			pmfs_inode_crawl(sb, bm, pi);
// 		}
// 		return;
// 	}

// 	set_bit(block >> PAGE_SHIFT, bm->scan_bm_4K.bitmap);
// 	for (i = 0; i < (1 << META_BLK_SHIFT); i++) {
// 		if (node[i] == 0)
// 			continue;
// 		pmfs_inode_table_crawl_recursive(sb, bm,
// 			le64_to_cpu(node[i]), height - 1, btype);
// 	}
// #endif
// }

static void free_resources(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct task_ring *ring;
	int i;

	if (task_rings) {
		for (i = 0; i < sbi->cpus; i++) {
			ring = &task_rings[i];
			vfree(ring->entry_array);
			vfree(ring->nvmm_array);
			ring->entry_array = NULL;
			ring->nvmm_array = NULL;
		}
	}

	kfree(task_rings);
	kfree(threads);
	kfree(finished);
}

static int failure_thread_func(void *data);

static int allocate_resources(struct super_block *sb, int cpus)
{
	struct task_ring *ring;
	int i;

	task_rings = kcalloc(cpus, sizeof(struct task_ring), GFP_KERNEL);
	if (!task_rings)
		goto fail;

	for (i = 0; i < cpus; i++) {
		ring = &task_rings[i];

		ring->nvmm_array = vzalloc(sizeof(u64) * MAX_PGOFF);
		if (!ring->nvmm_array)
			goto fail;

		ring->entry_array = vmalloc(sizeof(u64) * MAX_PGOFF);
		if (!ring->entry_array)
			goto fail;
	}

	threads = kcalloc(cpus, sizeof(struct task_struct *), GFP_KERNEL);
	if (!threads)
		goto fail;

	finished = kcalloc(cpus, sizeof(int), GFP_KERNEL);
	if (!finished)
		goto fail;

	init_waitqueue_head(&finish_wq);

	for (i = 0; i < cpus; i++) {
		threads[i] = kthread_create(failure_thread_func,
						sb, "recovery thread");
		kthread_bind(threads[i], i);
	}

	return 0;

fail:
	free_resources(sb);
	return -ENOMEM;
}

static void wait_to_finish(int cpus)
{
	int i;

	for (i = 0; i < cpus; i++) {
		while (finished[i] == 0) {
			wait_event_interruptible_timeout(finish_wq, false,
							msecs_to_jiffies(1));
		}
	}
}

/*********************** Failure recovery *************************/

static inline int pmfs_failure_update_inodetree(struct super_block *sb,
	struct pmfs_inode *pi, unsigned long *ino_low, unsigned long *ino_high)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	if (*ino_low == 0) {
		*ino_low = *ino_high = pi->pmfs_ino;
	} else {
		if (pi->pmfs_ino == *ino_high + sbi->cpus) {
			*ino_high = pi->pmfs_ino;
		} else {
			/* A new start */
			pmfs_failure_insert_inodetree(sb, *ino_low, *ino_high);
			*ino_low = *ino_high = pi->pmfs_ino;
		}
	}

	return 0;
}

static int failure_thread_func(void *data)
{
	struct super_block *sb = data;
	struct pmfs_inode_info_header sih;
	struct task_ring *ring;
	struct pmfs_inode *pi, fake_pi;
	unsigned long num_inodes_per_page;
	unsigned long ino_low, ino_high;
	unsigned int data_bits;
	u64 curr, curr1;
	int cpuid = pmfs_get_cpuid(sb);
	unsigned long i;
	unsigned long max_size = 0;
	u64 pi_addr = 0;
	int ret = 0;
	int count;

	pi = pmfs_get_inode_table(sb);
	data_bits = blk_type_to_shift[pi->i_blk_type];
	num_inodes_per_page = 1 << (data_bits - PMFS_INODE_BITS);

	ring = &task_rings[cpuid];
	pmfs_init_header(sb, &sih, 0);

	for (count = 0; count < ring->num; count++) {
		curr = ring->addr0[count];
		curr1 = ring->addr1[count];
		ino_low = ino_high = 0;

		/*
		 * Note: The inode log page is allocated in 2MB
		 * granularity, but not aligned on 2MB boundary.
		 */
		for (i = 0; i < 512; i++)
			set_bm((curr >> PAGE_SHIFT) + i,
			       global_bm[cpuid], BM_4K);

		for (i = 0; i < num_inodes_per_page; i++) {
			pi_addr = curr + i * PMFS_INODE_SIZE;
			ret = pmfs_get_reference(sb, pi_addr, &fake_pi,
						 (void **)&pi,
						 sizeof(struct pmfs_inode));
			if (ret) {
				pmfs_dbg("Recover pi @ 0x%llx failed\n",
					 pi_addr);
				continue;
			}

			if (pi->pmfs_ino < PMFS_NORMAL_INODE_START)
				continue;

			if (le16_to_cpu(fake_pi.i_links_count) == 0 &&
			    (le16_to_cpu(fake_pi.i_mode) == 0 ||
			     le32_to_cpu(fake_pi.i_dtime))) {
				continue;
			}

			pmfs_dbg_verbose("%s: Valid inode. "
					 "Now crawling. ino = %lu\n",
					 __func__, pi->pmfs_ino);

			pmfs_inode_crawl(sb, global_bm[cpuid], pi, ring);
			pmfs_failure_update_inodetree(sb, pi,
						      &ino_low, &ino_high);
			if (sih.i_size > max_size)
				max_size = sih.i_size;
		}

		if (ino_low && ino_high)
			pmfs_failure_insert_inodetree(sb, ino_low, ino_high);
	}

	finished[cpuid] = 1;
	wake_up_interruptible(&finish_wq);
	// do_exit(ret);
	return ret;
}

static int pmfs_failure_recovery_crawl(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode_info_header sih;
	struct inode_table *inode_table;
	struct task_ring *ring;
	struct pmfs_inode *pi;
	unsigned long curr_addr;
	u64 curr;
	int num_tables;
	int ret = 0;
	int count;
	int cpuid;

	pi = pmfs_get_inode(sb, PMFS_ROOT_INO);

	num_tables = 1;

	for (cpuid = 0; cpuid < sbi->cpus; cpuid++) {
		ring = &task_rings[cpuid];
		inode_table = pmfs_get_inode_table_log(sb, cpuid);

		if (!inode_table)
			return -EINVAL;

		count = 0;
		curr = inode_table->log_head;
		while (curr) {
			if (ring->num >= 512) {
				pmfs_err(sb, "%s: ring size too small\n",
					 __func__);
				return -EINVAL;
			}

			ring->addr0[count] = curr;

			count++;

			curr_addr = (unsigned long)pmfs_get_block(sb,
								  curr);
			/* Next page resides at the last 8 bytes */
			curr_addr += 2097152 - 8;
			curr = *(u64 *)(curr_addr);

			if (count > ring->num)
				ring->num = count;
		}
	}

	for (cpuid = 0; cpuid < sbi->cpus; cpuid++)
		wake_up_process(threads[cpuid]);

	pmfs_init_header(sb, &sih, 0);

	pmfs_inode_crawl(sb, global_bm[0], pi, ring);
	return ret;
}

int pmfs_failure_recovery(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct task_ring *ring;
	int ret;
	int i;

	sbi->s_inodes_used_count = 0;
	if (pmfs_init_inode_inuse_list(sb) < 0)
		return -EINVAL;

	ret = allocate_resources(sb, sbi->cpus);
	if (ret)
		return ret;

	ret = pmfs_failure_recovery_crawl(sb);

	wait_to_finish(sbi->cpus);

	for (i = 0; i < sbi->cpus; i++) {
		ring = &task_rings[i];
		sbi->s_inodes_used_count += ring->inodes_used_count;
	}

	free_resources(sb);
	pmfs_dbg("Failure recovery total recovered %lu\n",
		 sbi->s_inodes_used_count - PMFS_NORMAL_INODE_START);

	return ret;
}


/*********************** Recovery entrance *************************/

/* Return TRUE if we can do a normal unmount recovery */
static bool pmfs_try_normal_recovery(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi =  pmfs_get_inode(sb, PMFS_BLOCKNODE_IN0);
	struct pmfs_super_block *super = pmfs_get_super(sb);
	int ret;

	if (pi->i_size == 0) {
		return false;
	}

	sbi->num_blocknode_allocated =
		le64_to_cpu(super->s_num_blocknode_allocated);
	sbi->num_inodenode_allocated =
		le64_to_cpu(super->s_num_inodenode_allocated);
	sbi->num_free_blocks = le64_to_cpu(super->s_num_free_blocks);
	sbi->s_inodes_count = le32_to_cpu(super->s_inodes_count);
	sbi->s_free_inodes_count = le32_to_cpu(super->s_free_inodes_count);
	sbi->s_inodes_used_count = le32_to_cpu(super->s_inodes_used_count);
	sbi->s_free_inode_hint = le32_to_cpu(super->s_free_inode_hint);

	ret = pmfs_init_blockmap_from_inode(sb);

	if (ret) {
		pmfs_err(sb, "init blockmap failed, fall back to failure recovery\n");
		return false;
	}

        /* Clearing the datablock inode */
	pmfs_clear_datablock_inode(sb);

	ret = pmfs_init_inode_list_from_inode(sb);
	if (ret) {
		pmfs_err(sb, "init inode list failed, fall back to failure recovery\n");
		pmfs_destroy_blocknode_trees(sb);
		return false;
	}
	/* Clearing the datablock inode */
	pmfs_clear_inodelist_inode(sb);

	return true;
}

/*
 * Recovery routine has three tasks:
 * 1. Restore inuse inode list;
 * 2. Restore the NVMM allocator.
 */
int pmfs_recovery(struct super_block *sb, size_t size, size_t size_2)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_super_block *super = pmfs_get_super(sb);
	unsigned long initsize = le64_to_cpu(super->s_size_1);
	bool value = false;
	int ret = 0;
	unsigned long initsize_2 = le64_to_cpu(super->s_size_2);
	unsigned long journal_data_start = 0;
	struct timespec64 start, end;
	unsigned long blocksize;
	unsigned long num_blocks_1;
	int bits;

	sbi->block_start[0] = (unsigned long)0;
	sbi->block_end[0] = ((unsigned long)(size) >> PAGE_SHIFT);
	num_blocks_1 = ((unsigned long)size >> PAGE_SHIFT);

	if (sbi->num_numa_nodes == 2) {
		sbi->block_start[1] = num_blocks_1 +
			(((unsigned long)sbi->virt_addr_2 -
			  ((unsigned long)sbi->virt_addr + sbi->initsize)) / PAGE_SIZE);

		sbi->block_end[1] = sbi->block_start[1] +
			((unsigned long)(size_2) >> PAGE_SHIFT);

		if (sbi->block_start[0] % 512 != 0 ||
		    sbi->block_start[1] % 512 != 0) {
			BUG();
		}
	}

	sbi->num_free_blocks = ((unsigned long)(size + size_2) >> PAGE_SHIFT);
	sbi->num_blocks = ((unsigned long)(size + size_2) >> PAGE_SHIFT);

	pmfs_dbg_verbose("pmfs: Default block size set to 4K\n");
	blocksize = sbi->blocksize = PMFS_DEF_BLOCK_SIZE_4K;

	bits = fls(blocksize) - 1;
	sb->s_blocksize_bits = bits;
	sb->s_blocksize = (1 << bits);
	blocksize = sb->s_blocksize;

	if (sbi->blocksize && sbi->blocksize != blocksize)
		sbi->blocksize = blocksize;

	/* Always check recovery time */
	if (measure_timing == 0)
		ktime_get_raw_ts64(&start);

	/* initialize free list info */
	journal_data_start = (INODE_TABLE0_START + INODE_TABLE_NUM_BLOCKS) *
		sbi->blocksize;
	pmfs_init_blockmap(sb, journal_data_start + (sbi->cpus*sbi->jsize), 1);

	value = pmfs_try_normal_recovery(sb);
	if (value) {
		pmfs_dbg("PMFS: Normal shutdown\n");
	} else {
		pmfs_dbg("PMFS: Failure recovery\n");
		ret = alloc_bm(sb, initsize, initsize_2);
		if (ret)
			goto out;

		sbi->s_inodes_used_count = 0;
		ret = pmfs_failure_recovery(sb);
		if (ret)
			goto out;

		ret = pmfs_build_blocknode_map(sb, initsize, initsize_2);
	}

out:
	if (measure_timing == 0) {
		ktime_get_raw_ts64(&end);
		Timingstats[recovery_t] +=
			(end.tv_sec - start.tv_sec) * 1000000000 +
			(end.tv_nsec - start.tv_nsec);
	}

	if (!value)
		free_bm(sb);

	if (sbi->s_inodes_used_count < PMFS_NORMAL_INODE_START)
		sbi->s_inodes_used_count = PMFS_NORMAL_INODE_START;

	return ret;
}
