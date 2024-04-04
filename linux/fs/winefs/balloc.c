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
#include <linux/random.h>
#include "pmfs.h"
#include "inode.h"

#define PAGES_PER_2MB 512
#define PAGES_PER_2MB_MASK (PAGES_PER_2MB - 1)
#define IS_BLOCK_2MB_ALIGNED(block) \
	(!(block & PAGES_PER_2MB_MASK))
#define IS_DATABLOCKS_2MB_ALIGNED(num_blocks)	\
	(!(num_blocks & PAGES_PER_2MB_MASK))

int pmfs_alloc_block_free_lists(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list *free_list;
	int i;

	sbi->free_lists = kcalloc(sbi->cpus, sizeof(struct free_list),
				  GFP_KERNEL);

	if (!sbi->free_lists)
		return -ENOMEM;

	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		free_list->unaligned_block_free_tree = RB_ROOT;
		free_list->huge_aligned_block_free_tree = RB_ROOT;
		spin_lock_init(&free_list->s_lock);
		free_list->index = i;
	}

	return 0;
}

// Initialize a free list.  Each CPU gets an equal share of the block space to
// manage.
static void pmfs_init_free_list(struct super_block *sb,
	struct free_list *free_list, int index)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	unsigned long per_list_blocks;
	int second_node_cpuid = 0;
	unsigned long start_block = 0;
	int effective_index = 0;

	if (sbi->num_numa_nodes == 2) {
		second_node_cpuid = sbi->cpus / 2;
	}

	per_list_blocks = sbi->num_blocks / sbi->cpus;

	if (sbi->num_numa_nodes == 2) {
		if (sbi->cpus == 96 || sbi->cpus == 32) {
			if (index >= second_node_cpuid) {
				start_block = sbi->block_start[1];
				effective_index = index - second_node_cpuid;
			} else {
				start_block = 0;
				effective_index = index;
			}
		}
	} else {
		start_block = 0;
		effective_index = index;
	}

	free_list->block_start = start_block + (per_list_blocks * effective_index);
	free_list->block_end = free_list->block_start +
					per_list_blocks - 1;

	if (sbi->num_numa_nodes == 2) {
		if (index < second_node_cpuid && free_list->block_start >= sbi->block_start[1]) {
			pmfs_dbg("%s: Wrong NUMA setting: CPU id = %d, free_list->block_start = %lu",
				 __func__, index, free_list->block_start);
		}

		if (index == second_node_cpuid - 1) {
			sbi->block_end[0] = free_list->block_end + 1;
			sbi->initsize = (sbi->block_end[0] - sbi->block_start[0]) * PAGE_SIZE;
		}

		if (index == sbi->cpus - 1) {
			sbi->block_end[1] = free_list->block_end + 1;
			sbi->initsize_2 = (sbi->block_end[1] - sbi->block_start[1]) * PAGE_SIZE;
			sbi->num_blocks = (sbi->initsize + sbi->initsize_2) / PAGE_SIZE;
			sbi->num_free_blocks = sbi->num_blocks - sbi->head_reserved_blocks;
		}
	}

	if (index == 0)
		free_list->block_start += sbi->head_reserved_blocks;

	free_list->num_free_blocks = 0;
	free_list->num_blocknode_unaligned = 0;
	free_list->num_blocknode_huge_aligned = 0;
	free_list->first_node_unaligned = NULL;
	free_list->first_node_huge_aligned = NULL;
}

void pmfs_delete_free_lists(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	/* Each tree is freed in save_blocknode_mappings */
	kfree(sbi->free_lists);
	sbi->free_lists = NULL;
}

static void swap_free_lists(struct super_block *sb, int first_list,
			    int second_list) {
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list temp_free_list;

	memcpy(&temp_free_list, &sbi->free_lists[first_list], sizeof(struct free_list));
	memcpy(&sbi->free_lists[first_list], &sbi->free_lists[second_list], sizeof(struct free_list));
	memcpy(&sbi->free_lists[second_list], &temp_free_list, sizeof(struct free_list));
}

void pmfs_init_blockmap(struct super_block *sb,
			unsigned long init_used_size,
			int recovery)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct rb_root *unaligned_tree;
	struct rb_root *huge_aligned_tree;
	struct pmfs_range_node *blknode;
	struct free_list *free_list;
	int i, j;
	int ret;
	unsigned long num_used_block;
	unsigned long num_hugepages;
	unsigned long aligned_start, unaligned_start;
	unsigned long aligned_end, unaligned_end;

	num_used_block = (init_used_size + sb->s_blocksize - 1) >>
		sb->s_blocksize_bits;

	sbi->head_reserved_blocks = num_used_block;

	pmfs_dbg_verbose("%s: sbi->head_reserved_blocks = %lu\n", __func__,
			 sbi->head_reserved_blocks);

	sbi->per_list_blocks = sbi->num_blocks / sbi->cpus;
	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		unaligned_tree = &(free_list->unaligned_block_free_tree);
		huge_aligned_tree = &(free_list->huge_aligned_block_free_tree);
		pmfs_init_free_list(sb, free_list, i);

		if (recovery == 0) {
			free_list->num_free_blocks = free_list->block_end -
				free_list->block_start + 1;

			free_list->num_blocknode_unaligned = 0;
			free_list->num_blocknode_huge_aligned = 0;
			free_list->first_node_unaligned = NULL;
			free_list->first_node_huge_aligned = NULL;

			aligned_start = free_list->block_start;
			aligned_end = free_list->block_end;

			unaligned_start = unaligned_end = free_list->block_start;
			while (!(IS_BLOCK_2MB_ALIGNED(unaligned_end))) {
				unaligned_end++;
			}

			if (unaligned_end != unaligned_start) {
				blknode = pmfs_alloc_blocknode(sb);
				if (blknode == NULL)
					PMFS_ASSERT(0);
				blknode->range_low = unaligned_start;
				blknode->range_high = unaligned_end - 1;
				ret = pmfs_insert_blocktree(unaligned_tree, blknode);
				if (ret) {
					pmfs_err(sb, "%s failed\n", __func__);
					pmfs_free_blocknode(sb, blknode);
					return;
				}
				free_list->first_node_unaligned = blknode;
				free_list->num_blocknode_unaligned++;
				aligned_start = unaligned_end;
			}


			unaligned_start = unaligned_end = free_list->block_end;
			while (!(IS_BLOCK_2MB_ALIGNED(unaligned_start))) {
				unaligned_start--;
			}

			if (unaligned_end != unaligned_start) {
				blknode = pmfs_alloc_blocknode(sb);
				if (blknode == NULL)
					PMFS_ASSERT(0);
				blknode->range_low = unaligned_start;
				blknode->range_high = unaligned_end;
				ret = pmfs_insert_blocktree(unaligned_tree, blknode);
				if (ret) {
					pmfs_err(sb, "%s failed\n", __func__);
					pmfs_free_blocknode(sb, blknode);
					return;
				}
				if (free_list->first_node_unaligned == NULL)
					free_list->first_node_unaligned = blknode;
				free_list->num_blocknode_unaligned++;
				aligned_end = unaligned_start - 1;
			}

			num_hugepages = (aligned_end - aligned_start + 1) / PAGES_PER_2MB;
			pmfs_dbg_verbose("%s: number of huge pages in list %d = %lu\n",
					 __func__, i, num_hugepages);

			blknode = NULL;
			for (j = aligned_start; j < aligned_end; j += PAGES_PER_2MB) {
				blknode = pmfs_alloc_blocknode(sb);
				if (blknode == NULL)
					PMFS_ASSERT(0);
				blknode->range_low = j;
				blknode->range_high = j + PAGES_PER_2MB - 1;

				if (blknode->range_low < free_list->block_start ||
				    blknode->range_high > free_list->block_end) {
					pmfs_err(sb, "%s failed\n", __func__);
					pmfs_free_blocknode(sb, blknode);
					return;
				}

				ret = pmfs_insert_blocktree(huge_aligned_tree, blknode);
				if (ret) {
					pmfs_err(sb, "%s failed\n", __func__);
					pmfs_free_blocknode(sb, blknode);
					return;
				}
				if (j == aligned_start) {
					free_list->first_node_huge_aligned = blknode;
				}
				free_list->num_blocknode_huge_aligned++;
			}

			if (free_list->first_node_huge_aligned == NULL) {
				pmfs_err(sb, "%s failed\n", __func__);
				return;
			}
		}
	}

	if (sbi->num_numa_nodes == 2) {
		if (sbi->cpus == 96) {
			for (i = 24, j = 48; i < 48; i++, j++) {
				swap_free_lists(sb, i, j);
			}
		} else if (sbi->cpus == 32) {
			for (i = 8, j = 16; i < 16; i++, j++) {
				swap_free_lists(sb, i, j);
			}
		}
	}

	if (recovery == 0) {
		for (i = 0; i < sbi->cpus; i++) {
			free_list = pmfs_get_free_list(sb, i);
			pmfs_dbg("%s: free list %d: block start %lu, end %lu, "
				 "%lu free blocks. num_aligned_nodes = %lu, "
				 "num_unaligned_nodes = %lu, num_allocated_blocknodes = %lu, "
				 "aligned_block_start = %lu, unaligned_block_start = %lu\n",
				 __func__, i,
				 free_list->block_start,
				 free_list->block_end,
				 free_list->num_free_blocks,
				 free_list->num_blocknode_huge_aligned,
				 free_list->num_blocknode_unaligned,
				 sbi->num_blocknode_allocated,
				 free_list->first_node_huge_aligned->range_low,
				 free_list->first_node_unaligned->range_low);
		}
	}
}

static inline int pmfs_rbtree_compare_rangenode(struct pmfs_range_node *curr,
						unsigned long key, enum node_type type)
{
	if (type == NODE_DIR) {
		if (key < curr->hash)
			return -1;
		if (key > curr->hash)
			return 1;
		return 0;
	}

	/* Inode */
	if (key < curr->range_low)
		return -1;
	if (key > curr->range_high)
		return 1;

	return 0;
}

int pmfs_find_range_node(struct rb_root *tree, unsigned long key,
			 enum node_type type, struct pmfs_range_node **ret_node)
{
	struct pmfs_range_node *curr = NULL;
	struct rb_node *temp;
	int compVal;
	int ret = 0;

	temp = tree->rb_node;

	while (temp) {
		curr = container_of(temp, struct pmfs_range_node, node);
		compVal = pmfs_rbtree_compare_rangenode(curr, key, type);

		if (compVal == -1) {
			temp = temp->rb_left;
		} else if (compVal == 1) {
			temp = temp->rb_right;
		} else {
			ret = 1;
			break;
		}
	}

	*ret_node = curr;
	return ret;
}

int pmfs_insert_range_node(struct rb_root *tree,
			   struct pmfs_range_node *new_node, enum node_type type)
{
	struct pmfs_range_node *curr;
	struct rb_node **temp, *parent;
	int compVal;

	temp = &(tree->rb_node);
	parent = NULL;

	while (*temp) {
		curr = container_of(*temp, struct pmfs_range_node, node);
		compVal = pmfs_rbtree_compare_rangenode(curr,
							new_node->range_low, type);
		parent = *temp;

		if (compVal == -1) {
			temp = &((*temp)->rb_left);
		} else if (compVal == 1) {
			temp = &((*temp)->rb_right);
		} else {
			pmfs_dbg("%s: entry %lu - %lu already exists: "
				"%lu - %lu\n",
				 __func__, new_node->range_low,
				new_node->range_high, curr->range_low,
				curr->range_high);
			BUG();
			return -EINVAL;
		}
	}

	rb_link_node(&new_node->node, parent, temp);
	rb_insert_color(&new_node->node, tree);

	return 0;
}

void pmfs_destroy_range_node_tree(struct super_block *sb,
				  struct rb_root *tree)
{
	struct pmfs_range_node *curr;
	struct rb_node *temp;

	temp = rb_first(tree);
	while (temp) {
		curr = container_of(temp, struct pmfs_range_node, node);
		temp = rb_next(temp);
		rb_erase(&curr->node, tree);
		pmfs_free_range_node(curr);
	}
}

int pmfs_insert_blocktree(struct rb_root *tree,
			  struct pmfs_range_node *new_node)
{
	int ret;

	ret = pmfs_insert_range_node(tree, new_node, NODE_BLOCK);
	if (ret)
		pmfs_dbg("ERROR: %s failed %d\n", __func__, ret);

	return ret;
}

struct pmfs_range_node *pmfs_alloc_blocknode_atomic(struct super_block *sb)
{
	return pmfs_alloc_range_node_atomic(sb);
}

/* Allocate a superpage. This assumes that all big allocations have been
 * broken down to 2MB allocations. So here the num_blocks are expected to be
 * 512
 */
bool pmfs_alloc_superpage(struct super_block *sb,
	struct free_list *free_list, unsigned long num_blocks,
	unsigned long *new_blocknr)
{
	struct rb_root *tree;
	struct rb_node *temp;
	struct pmfs_range_node *curr, *node;
	bool found = 0;
	unsigned long step = 0;

	if (num_blocks != PAGES_PER_2MB) {
		pmfs_dbg("%s: wrong number of blocks. Expected = 512. Requested = %lu\n",
			 __func__, num_blocks);
		dump_stack();
		return found;
	}

	tree = &(free_list->huge_aligned_block_free_tree);
	temp = &(free_list->first_node_huge_aligned->node);

	if (temp) {
		step++;
		curr = container_of(temp, struct pmfs_range_node, node);
		*new_blocknr = curr->range_low;
		node = NULL;
		temp = rb_next(temp);
		if (temp)
			node = container_of(temp, struct pmfs_range_node, node);
		free_list->first_node_huge_aligned = node;

		/* release curr after updating {first, last}_node */
		rb_erase(&curr->node, tree);
		pmfs_free_blocknode(sb, curr);
		free_list->num_blocknode_huge_aligned--;
		found = 1;
	}

	pmfs_dbg_verbose("%s: blocknr = %lu. num_blocks = %lu\n",
			 __func__, *new_blocknr, num_blocks);

	return found;
}


/* Return how many blocks allocated */
static long pmfs_alloc_blocks_in_free_list(struct super_block *sb,
	struct free_list *free_list, unsigned short btype,
	unsigned long num_blocks,
	unsigned long *new_blocknr)
{
	struct rb_root *tree, *huge_tree;
	struct pmfs_range_node *curr, *node, *next = NULL, *blknode;
	struct rb_node *temp, *next_node;
	unsigned long curr_blocks;
	bool found = 0;
	bool found_hugeblock = 0;
	unsigned long step = 0;

	if ((!free_list->first_node_unaligned &&
	     !free_list->first_node_huge_aligned) ||
	    free_list->num_free_blocks == 0) {
		pmfs_dbg("%s: Can't alloc. free_list->first_node_unaligned=0x%p "
				 "free_list->first_node_aligned=0x%p "
				 "free_list->num_free_blocks = %lu",
				 __func__, free_list->first_node_unaligned,
				 free_list->first_node_huge_aligned,
				 free_list->num_free_blocks);
		return -ENOSPC;
	}

	pmfs_dbg_verbose("%s: Got allocation req for num_blocks = %lu\n",
			 __func__, num_blocks);

	tree = &(free_list->unaligned_block_free_tree);
	huge_tree = &(free_list->huge_aligned_block_free_tree);
	temp = &(free_list->first_node_unaligned->node);

	/* Try huge block allocation for data blocks first */
	if (IS_DATABLOCKS_2MB_ALIGNED(num_blocks)) {
		found_hugeblock = pmfs_alloc_superpage(sb, free_list,
					num_blocks, new_blocknr);
		if (found_hugeblock)
			goto success;
	}

	/* If temp is NULL, take a super page from hugepage rb tree.
	 * We only need to call this once in the beginning, because
	 * largest allocation request we will ever get will be 2MB
	 */
	if (!temp) {
		temp = &(free_list->first_node_huge_aligned->node);
		curr = container_of(temp, struct pmfs_range_node, node);

		blknode = pmfs_alloc_blocknode(sb);
		if (blknode == NULL) {
			pmfs_dbg("%s: alloc blocknode failed\n", __func__);
			return -ENOMEM;
		}
		blknode->range_low = curr->range_low;
		blknode->range_high = curr->range_high;

		if (tree == NULL) {
			pmfs_dbg("%s: unaligned tree does not exist\n", __func__);
			PMFS_ASSERT(0);
		}

		pmfs_insert_blocktree(tree, blknode);
		free_list->first_node_unaligned = blknode;

		pmfs_dbg_verbose("%s: breaking an aligned free space from hugepage rb tree "
			 "curr->range_low = %lu. curr->range_high = %lu. free_list idx = %d\n",
			 __func__, curr->range_low, curr->range_high, free_list->index);

		node = NULL;
		next_node = rb_next(temp);
		if (next_node)
			next = container_of(next_node, struct pmfs_range_node, node);
		free_list->first_node_huge_aligned = next;

		/* release curr after updating {first, last}_node */
		rb_erase(&curr->node, huge_tree);
		pmfs_free_blocknode(sb, curr);
		free_list->num_blocknode_huge_aligned--;
		free_list->num_blocknode_unaligned++;
		temp = &(free_list->first_node_unaligned->node);
	}

	next = NULL;
	node = NULL;

	/* fallback to un-aligned allocation then */
	while (temp) {
		step++;
		curr = container_of(temp, struct pmfs_range_node, node);

		curr_blocks = curr->range_high - curr->range_low + 1;

		pmfs_dbg_verbose("%s: curr->range_low = %lu. "
				 "curr->range_high = %lu. curr_blocks = %lu\n",
				 __func__, curr->range_low, curr->range_high, curr_blocks);

		if (num_blocks >= curr_blocks) {
			/* Superpage allocation must succeed */
			if (btype > 0 && num_blocks > curr_blocks)
				goto next;

			/* Otherwise, allocate the whole blocknode */
			if (curr == free_list->first_node_unaligned) {
				next_node = rb_next(temp);
				if (next_node)
					next = container_of(next_node,
						struct pmfs_range_node, node);
				free_list->first_node_unaligned = next;
			}

			rb_erase(&curr->node, tree);
			free_list->num_blocknode_unaligned--;
			num_blocks = curr_blocks;
			*new_blocknr = curr->range_low;
			pmfs_free_blocknode(sb, curr);
			found = 1;
			break;
		}

		/* Allocate partial blocknode */
		*new_blocknr = curr->range_low;
		curr->range_low += num_blocks;

		found = 1;
		break;
next:
		temp = rb_next(temp);
	}

	if (free_list->num_free_blocks < num_blocks) {
		pmfs_dbg("%s: free list %d has %lu free blocks, "
			 "but allocated %lu blocks?\n",
			 __func__, free_list->index,
			 free_list->num_free_blocks, num_blocks);
		return -ENOSPC;
	}

success:
	if ((found == 1) || (found_hugeblock == 1))
		free_list->num_free_blocks -= num_blocks;
	else {
		pmfs_dbg("%s: Can't alloc.  found = %d", __func__, found);
		return -ENOSPC;
	}

	
	return num_blocks;
}

/* Used for both block free tree and inode inuse tree */
int pmfs_find_free_slot(struct rb_root *tree, unsigned long range_low,
	unsigned long range_high, struct pmfs_range_node **prev,
	struct pmfs_range_node **next)
{
	struct pmfs_range_node *ret_node = NULL;
	struct rb_node *tmp;
	int check_prev = 0, check_next = 0;
	int ret;

	ret = pmfs_find_range_node(tree, range_low, NODE_BLOCK, &ret_node);
	if (ret) {
		pmfs_dbg("%s ERROR: %lu - %lu already in free list\n",
			__func__, range_low, range_high);
		return -EINVAL;
	}

	if (!ret_node) {
		*prev = *next = NULL;
	} else if (ret_node->range_high < range_low) {
		*prev = ret_node;
		tmp = rb_next(&ret_node->node);
		if (tmp) {
			*next = container_of(tmp, struct pmfs_range_node, node);
			check_next = 1;
		} else {
			*next = NULL;
		}
	} else if (ret_node->range_low > range_high) {
		*next = ret_node;
		tmp = rb_prev(&ret_node->node);
		if (tmp) {
			*prev = container_of(tmp, struct pmfs_range_node, node);
			check_prev = 1;
		} else {
			*prev = NULL;
		}
	} else {
		pmfs_dbg("%s ERROR: %lu - %lu overlaps with existing "
			 "node %lu - %lu\n",
			 __func__, range_low, range_high, ret_node->range_low,
			ret_node->range_high);
		return -EINVAL;
	}

	return 0;
}

static int insert_in_huge_tree(struct rb_root *huge_tree,
			       struct pmfs_range_node *node,
			       struct free_list *free_list)
{
	int ret = 0;

	if (huge_tree == NULL) {
		pmfs_dbg("%s: huge_tree does not exist\n", __func__);
		PMFS_ASSERT(0);
	}

	ret = pmfs_insert_blocktree(huge_tree, node);
	if (ret) {
		goto out;
	}

	if (!free_list->first_node_huge_aligned) {
		free_list->first_node_huge_aligned = node;

	} else if (free_list->first_node_huge_aligned->range_low > node->range_high) {
		free_list->first_node_huge_aligned = node;
	}

	free_list->num_blocknode_huge_aligned++;
 out:
	return ret;
}

static int check_and_insert_huge_aligned(struct super_block *sb,
					 struct rb_root *unaligned_tree,
					 struct rb_root *huge_tree,
					 struct pmfs_range_node *new_node,
					 struct pmfs_range_node *old_node,
					 struct free_list *free_list,
					 int *new_node_used)
{
	unsigned long block_low;
	unsigned long block_high;
	unsigned long diff_from_huge_boundary = 0;
	unsigned long num_hugepages = 0;
	int ret = 0;
	int idx = 0;
	struct pmfs_range_node *node, *next = NULL;
	struct rb_node *next_node, *temp;
	unsigned long unaligned_block_high = 0;
	unsigned long unaligned_block_low = 0;
	int new_unaligned_node_needed = 0;
	unsigned long initial_block_low = 0;
	unsigned long initial_block_high = 0;

	block_low = old_node->range_low;
	block_high = old_node->range_high;

	initial_block_low = block_low;
	initial_block_high = block_high;

	if ((block_high - block_low + 1) < PAGES_PER_2MB) {
		goto out;
	}

	/* Align the lower end to huge page boundary */
	if (!IS_BLOCK_2MB_ALIGNED(block_low)) {
		diff_from_huge_boundary = PAGES_PER_2MB - (block_low & PAGES_PER_2MB_MASK);
		block_low += diff_from_huge_boundary;
		PMFS_ASSERT(IS_BLOCK_2MB_ALIGNED(block_low));
		unaligned_block_high = block_low - 1;
	}

	/* Align the higher end to huge page boundary */
	if (!IS_BLOCK_2MB_ALIGNED((block_high+1))) {
		diff_from_huge_boundary = (block_high+1) & PAGES_PER_2MB_MASK;
		block_high -= diff_from_huge_boundary;
		PMFS_ASSERT(IS_BLOCK_2MB_ALIGNED(block_high + 1));
		unaligned_block_low = block_high + 1;
	}

	if (block_high > block_low)
		num_hugepages = (block_high - block_low + 1) / PAGES_PER_2MB;
	else
		num_hugepages = 0;

	/* If there can be no hugepages, just return */
	if (num_hugepages == 0)
		goto out;

	/* If the lower end was adjusted, adjust the old_node */
	if (unaligned_block_high == block_low - 1) {
		old_node->range_high = unaligned_block_high;
		new_unaligned_node_needed = 1;
	}

	/* If the higher end was adjusted, adjust the old_node or allocate new node */
	if (unaligned_block_low == block_high + 1) {
		if (new_unaligned_node_needed == 1) {
			node = new_node;
			if (node == NULL || unaligned_tree == NULL) {
				pmfs_dbg("%s: node or unaligned tree does not exist\n", __func__);
				PMFS_ASSERT(0);
			}

			ret = pmfs_insert_blocktree(unaligned_tree, node);
			if (ret) {
				goto out;
			}
			free_list->num_blocknode_unaligned++;
			*new_node_used = 1;
		} else {
			node = old_node;
		}
		node->range_low = unaligned_block_low;
		node->range_high = initial_block_high;
	}

	/* If nothing was adjusted, remove the old_node from unaligned tree */
	if (unaligned_block_low == 0 && unaligned_block_high == 0) {
		if (old_node == free_list->first_node_unaligned) {
			temp = &(free_list->first_node_unaligned->node);
			next_node = rb_next(temp);
			if (next_node)
				next = container_of(next_node,
						    struct pmfs_range_node, node);
			free_list->first_node_unaligned = next;
		}

		rb_erase(&old_node->node, unaligned_tree);
		free_list->num_blocknode_unaligned--;
		pmfs_free_blocknode(sb, old_node);
	}

	/* For each new huge page, allocate a range_node and insert it in huge_tree */
	for (idx = 0; idx < num_hugepages; idx++) {
		if (!(*new_node_used)) {
			node = new_node;
			*new_node_used = 1;
		} else {
			node = pmfs_alloc_blocknode(sb);
			if (node == NULL) {
				ret = -ENOMEM;
				goto out;
			}
		}

		node->range_low = block_low;
		node->range_high = block_low + PAGES_PER_2MB - 1;
		ret = insert_in_huge_tree(huge_tree, node, free_list);
		if (ret)
			goto out;
		block_low = node->range_high + 1;
	}

 out:
	return ret;
}

int pmfs_free_blocks(struct super_block *sb, unsigned long blocknr,
	int num, unsigned short btype)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct rb_root *tree, *huge_tree;
	unsigned long block_low;
	unsigned long block_high;
	unsigned long num_blocks = 0, num_blocks_local = 0;
	struct pmfs_range_node *prev = NULL;
	struct pmfs_range_node *next = NULL;
	struct pmfs_range_node *curr_node;
	struct free_list *free_list;
	int cpuid;
	int new_node_used = 0;
	int ret;

	if (num <= 0) {
		pmfs_dbg("%s ERROR: free %d\n", __func__, num);
		BUG();
		return -EINVAL;
	}

	cpuid = get_block_cpuid(sbi, blocknr);

	free_list = pmfs_get_free_list(sb, cpuid);

	tree = &(free_list->unaligned_block_free_tree);
	huge_tree = &(free_list->huge_aligned_block_free_tree);

	num_blocks = pmfs_get_numblocks(btype) * num;

	while (num_blocks) {

		/* Pre-allocate blocknode */
		curr_node = pmfs_alloc_blocknode(sb);
		if (curr_node == NULL) {
			/* returning without freeing the block*/
			return -ENOMEM;
		}
		new_node_used = 0;

		if (cpuid < 0 || cpuid >= sbi->cpus) {
			BUG();
		}

		spin_lock(&free_list->s_lock);

		num_blocks_local = num_blocks > PAGES_PER_2MB ? PAGES_PER_2MB : num_blocks;
		block_low = blocknr;
		block_high = blocknr + num_blocks_local - 1;

		pmfs_dbg_verbose("Free: %lu - %lu\n", block_low, block_high);

		if (blocknr < free_list->block_start ||
		    blocknr + num > free_list->block_end + 1) {
			pmfs_err(sb, "free blocks %lu to %lu, free list %d, "
				 "start %lu, end %lu\n",
				 blocknr, blocknr + num - 1,
				 free_list->index,
				 free_list->block_start,
				 free_list->block_end);
			ret = -EIO;
			goto out;
		}

		if (IS_BLOCK_2MB_ALIGNED(block_low) && num_blocks_local == PAGES_PER_2MB) {
			curr_node->range_low = block_low;
			curr_node->range_high = block_high;
			ret = insert_in_huge_tree(huge_tree, curr_node, free_list);
			if (ret) {
				new_node_used = 0;
				goto out;
			}
			new_node_used = 1;
			goto block_found;
		}

		ret = pmfs_find_free_slot(tree, block_low,
					  block_high, &prev, &next);

		if (ret) {
			goto out;
		}

		if (prev && next && (block_low == prev->range_high + 1) &&
		    (block_high + 1 == next->range_low)) {
			/* fits the hole */
			prev->range_high = next->range_high;
			rb_erase(&next->node, tree);
			free_list->num_blocknode_unaligned--;
			pmfs_free_blocknode(sb, next);
			ret = check_and_insert_huge_aligned(sb, tree, huge_tree,
							    curr_node, prev,
							    free_list, &new_node_used);
			if (ret)
				goto out;
			goto block_found;
		}
		if (prev && (block_low == prev->range_high + 1)) {
			/* Aligns left */
			prev->range_high += num_blocks_local;
			ret = check_and_insert_huge_aligned(sb, tree, huge_tree,
							    curr_node, prev,
							    free_list, &new_node_used);
			if (ret)
				goto out;
			goto block_found;
		}
		if (next && (block_high + 1 == next->range_low)) {
			/* Aligns right */
			next->range_low -= num_blocks_local;
			ret = check_and_insert_huge_aligned(sb, tree, huge_tree,
							    curr_node, next,
							    free_list, &new_node_used);
			if (ret)
				goto out;
			goto block_found;
		}

		/* Aligns somewhere in the middle */
		curr_node->range_low = block_low;
		curr_node->range_high = block_high;
		new_node_used = 1;

		if (tree == NULL) {
			pmfs_dbg("%s: tree does not exist\n", __func__);
			PMFS_ASSERT(0);
		}

		ret = pmfs_insert_blocktree(tree, curr_node);
		if (ret) {
			new_node_used = 0;
			goto out;
		}
		if (!prev)
			free_list->first_node_unaligned = curr_node;

		free_list->num_blocknode_unaligned++;

	block_found:
		free_list->num_free_blocks += num_blocks_local;

		spin_unlock(&free_list->s_lock);

		if (new_node_used == 0)
			pmfs_free_blocknode(sb, curr_node);

		num_blocks -= num_blocks_local;
		blocknr += num_blocks_local;
	}
 out:

	if (ret != 0) {
		spin_unlock(&free_list->s_lock);
		if (new_node_used == 0)
			pmfs_free_blocknode(sb, curr_node);
	}

	return ret;
}

// static int not_enough_holes(struct free_list *free_list,
// 			    unsigned long num_blocks)
// {
// 	struct pmfs_range_node *first_unaligned = free_list->first_node_unaligned;
// 	struct pmfs_range_node *first_huge_aligned = free_list->first_node_huge_aligned;

// 	unsigned long num_hole_blocks = free_list->num_free_blocks - (free_list->num_blocknode_huge_aligned*PAGES_PER_2MB);
// 	if (num_hole_blocks < num_blocks ||
// 	    !first_unaligned) {
// 		pmfs_dbg_verbose("%s: num_free_blocks=%ld; num_blocks=%ld; "
// 				 "first_unaligned=0x%p; "
// 				 "first_aligned=0x%p\n",
// 				 __func__, free_list->num_free_blocks, num_blocks,
// 				 first_unaligned,
// 				 first_huge_aligned);
// 		return 1;
// 	}

// 	return 0;
// }

static int not_enough_blocks(struct free_list *free_list,
	unsigned long num_blocks)
{
	struct pmfs_range_node *first_unaligned = free_list->first_node_unaligned;
	struct pmfs_range_node *first_huge_aligned = free_list->first_node_huge_aligned;

	if (free_list->num_free_blocks < num_blocks ||
	    (!first_unaligned && !first_huge_aligned)) {
		pmfs_dbg_verbose("%s: num_free_blocks=%ld; num_blocks=%ld; "
				 "first_unaligned=0x%p; "
				 "first_aligned=0x%p\n",
				 __func__, free_list->num_free_blocks, num_blocks,
				 first_unaligned,
				 first_huge_aligned);
		return 1;
	}

	return 0;
}

/* Find out the free list with most free blocks */
static int pmfs_get_candidate_free_list(struct super_block *sb, int num_req_blocks)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list *free_list;
	int cpuid = 0;
	int cpuid_holes = -1;
	int num_free_blocks = 0;
	int num_free_holes = 0;
	int max_free_holes = 0;
	int i;
	int numa_node;
	int flag = 0;
	struct numa_node_cpus *numa_cpus;

	if (sbi->num_numa_nodes == 2) {
		numa_node = pmfs_get_numa_node(sb, pmfs_get_cpuid(sb));
		numa_cpus = sbi->numa_cpus;
	again:
		for (i = 0; i < numa_cpus[numa_node].num_cpus; i++) {
			free_list = pmfs_get_free_list(sb, numa_cpus[numa_node].cpus[i]);
			if (free_list->num_free_blocks > num_free_blocks) {
				cpuid = numa_cpus[numa_node].cpus[i];
				num_free_blocks = free_list->num_free_blocks;
			}
		}

		if (num_free_blocks == 0 && flag == 0) {
			numa_node = (numa_node + 1) % sbi->num_numa_nodes;
			flag = 1;
			goto again;
		} else {
			goto out;
		}
	} else {
		for (i = 0; i < sbi->cpus; i++) {
			free_list = pmfs_get_free_list(sb, i);
			if (free_list->num_free_blocks > num_free_blocks) {
				cpuid = i;
				num_free_blocks = free_list->num_free_blocks;
			}
			if (num_req_blocks != 512) {
				num_free_holes = free_list->num_free_blocks -
					(free_list->num_blocknode_huge_aligned*PAGES_PER_2MB);
				if (num_free_holes > max_free_holes && num_free_holes >= num_req_blocks) {
					max_free_holes = num_free_holes;
					cpuid_holes = i;
				}
			}
		}
	}

 out:

	return cpuid_holes >= 0 ? cpuid_holes : cpuid;
}

int pmfs_new_blocks(struct super_block *sb, unsigned long *blocknr,
		    unsigned int num, unsigned short btype, int zero, int cpuid)
{
	struct free_list *free_list;
	void *bp;
	unsigned long num_blocks = 0;
	unsigned long new_blocknr = 0;
	long ret_blocks = 0;
	int retried = 0;

	num_blocks = num * pmfs_get_numblocks(btype);
	if (num_blocks == 0) {
		pmfs_dbg_verbose("%s: num_blocks == 0", __func__);
		return -EINVAL;
	}

	if (cpuid == ANY_CPU)
		cpuid = pmfs_get_cpuid(sb);

 retry:

	free_list = pmfs_get_free_list(sb, cpuid);
	spin_lock(&free_list->s_lock);

	if (not_enough_blocks(free_list, num_blocks)) {
		pmfs_dbg_verbose("%s: cpu %d, free_blocks %lu, required %lu, "
			  "blocknode %lu\n",
			  __func__, cpuid, free_list->num_free_blocks,
			  num_blocks, free_list->num_blocknode_unaligned +
				 free_list->num_blocknode_huge_aligned);

		if (retried >= 2)
			/* Allocate anyway */
			goto alloc;

		spin_unlock(&free_list->s_lock);

		cpuid = pmfs_get_candidate_free_list(sb, num_blocks);
		retried++;
		goto retry;
	}
alloc:
	ret_blocks = pmfs_alloc_blocks_in_free_list(sb, free_list, btype,
					num_blocks, &new_blocknr);

	spin_unlock(&free_list->s_lock);

	if (ret_blocks <= 0 || new_blocknr == 0) {
		pmfs_dbg("%s: not able to allocate %d blocks. "
			  "ret_blocks=%ld; new_blocknr=%lu",
			  __func__, num, ret_blocks, new_blocknr);
		return -ENOSPC;
	}

	if (zero) {
		bp = pmfs_get_block(sb, pmfs_get_block_off(sb,
							   new_blocknr, btype));
		pmfs_memunlock_range(sb, bp, PAGE_SIZE * ret_blocks);
		memset_nt(bp, 0, PAGE_SIZE * ret_blocks);
		pmfs_memlock_range(sb, bp, PAGE_SIZE * ret_blocks);
	}
	*blocknr = new_blocknr;

	pmfs_dbg_verbose("Alloc %lu NVMM blocks 0x%lx\n", ret_blocks, *blocknr);
	return ret_blocks / pmfs_get_numblocks(btype);
}

unsigned int pmfs_get_free_numa_node(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list *free_list;
	unsigned long num_free_blocks_node_1 = 0;
	unsigned long num_free_blocks_node_2 = 0;

	int i;

	if (sbi->num_numa_nodes == 2) {
		if (sbi->cpus == 96) {
			for (i = 0; i < sbi->cpus; i++) {
				free_list = pmfs_get_free_list(sb, i);
				if (i < 24 || (i >= 48 && i < 72))
					num_free_blocks_node_1 += free_list->num_free_blocks;
				else
					num_free_blocks_node_2 += free_list->num_free_blocks;
			}
		} else if (sbi->cpus == 32) {
			for (i = 0; i < sbi->cpus; i++) {
				free_list = pmfs_get_free_list(sb, i);
				if (i < 8 || (i >= 16 && i < 24))
					num_free_blocks_node_1 += free_list->num_free_blocks;
				else
					num_free_blocks_node_2 += free_list->num_free_blocks;
			}
		}
	}

	if (num_free_blocks_node_1 >= num_free_blocks_node_2)
		return 0;
	return 1;
}

unsigned long pmfs_count_free_blocks(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct free_list *free_list;
	unsigned long num_free_blocks = 0;
	int i;

	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		num_free_blocks += free_list->num_free_blocks;
	}

	return num_free_blocks;
}
