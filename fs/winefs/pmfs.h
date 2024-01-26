/*
 * BRIEF DESCRIPTION
 *
 * Definitions for the PMFS filesystem.
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
#ifndef __PMFS_H
#define __PMFS_H

#include <linux/crc16.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/uio.h>
#include <linux/version.h>
#include <linux/pfn_t.h>
#include <linux/iomap.h>
#include <linux/dax.h>
#include <linux/kthread.h>

#include "pmfs_def.h"
#include "journal.h"

#define PAGE_SHIFT_2M 21
#define PAGE_SHIFT_1G 30

#define PMFS_ASSERT(x)                                                 \
	if (!(x)) {                                                     \
		printk(KERN_WARNING "assertion failed %s:%d: %s\n",     \
	               __FILE__, __LINE__, #x);                         \
	}

/*
 * Debug code
 */
#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

/* #define pmfs_dbg(s, args...)         pr_debug(s, ## args) */
#define pmfs_dbg(s, args ...)           pr_debug(s, ## args)
#define pmfs_dbg1(s, args ...)
#define pmfs_err(sb, s, args ...)       pmfs_error_mng(sb, s, ## args)
#define pmfs_warn(s, args ...)          pr_warn(s, ## args)
#define pmfs_info(s, args ...)          pr_info(s, ## args)

extern unsigned int pmfs_dbgmask;
#define PMFS_DBGMASK_MMAPHUGE          (0x00000001)
#define PMFS_DBGMASK_MMAP4K            (0x00000002)
#define PMFS_DBGMASK_MMAPVERBOSE       (0x00000004)
#define PMFS_DBGMASK_MMAPVVERBOSE      (0x00000008)
#define PMFS_DBGMASK_VERBOSE           (0x00000010)
#define PMFS_DBGMASK_TRANSACTION       (0x00000020)

#define pmfs_dbg_mmaphuge(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_MMAPHUGE) ? pmfs_dbg(s, args) : 0)
#define pmfs_dbg_mmap4k(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_MMAP4K) ? pmfs_dbg(s, args) : 0)
#define pmfs_dbg_mmapv(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_MMAPVERBOSE) ? pmfs_dbg(s, args) : 0)
#define pmfs_dbg_mmapvv(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_MMAPVVERBOSE) ? pmfs_dbg(s, args) : 0)

#define pmfs_dbg_verbose(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_VERBOSE) ? pmfs_dbg(s, ##args) : 0)
#define pmfs_dbg_trans(s, args ...)		 \
	((pmfs_dbgmask & PMFS_DBGMASK_TRANSACTION) ? pmfs_dbg(s, ##args) : 0)

#define pmfs_set_bit                   __test_and_set_bit_le
#define pmfs_clear_bit                 __test_and_clear_bit_le
#define pmfs_find_next_zero_bit                find_next_zero_bit_le

#define clear_opt(o, opt)       (o &= ~PMFS_MOUNT_ ## opt)
#define set_opt(o, opt)         (o |= PMFS_MOUNT_ ## opt)
#define test_opt(sb, opt)       (PMFS_SB(sb)->s_mount_opt & PMFS_MOUNT_ ## opt)

#define PMFS_LARGE_INODE_TABLE_SIZE    (0x200000)
/* PMFS size threshold for using 2M blocks for inode table */
#define PMFS_LARGE_INODE_TABLE_THREASHOLD    (0x20000000)
/*
 * pmfs inode flags
 *
 * PMFS_EOFBLOCKS_FL	There are blocks allocated beyond eof
 */
#define PMFS_EOFBLOCKS_FL      0x20000000
/* Flags that should be inherited by new inodes from their parent. */
#define PMFS_FL_INHERITED (FS_SECRM_FL | FS_UNRM_FL | FS_COMPR_FL | \
			    FS_SYNC_FL | FS_NODUMP_FL | FS_NOATIME_FL |	\
			    FS_COMPRBLK_FL | FS_NOCOMP_FL | FS_JOURNAL_DATA_FL | \
			    FS_NOTAIL_FL | FS_DIRSYNC_FL)
/* Flags that are appropriate for regular files (all but dir-specific ones). */
#define PMFS_REG_FLMASK (~(FS_DIRSYNC_FL | FS_TOPDIR_FL))
/* Flags that are appropriate for non-directories/regular files. */
#define PMFS_OTHER_FLMASK (FS_NODUMP_FL | FS_NOATIME_FL)
#define PMFS_FL_USER_VISIBLE (FS_FL_USER_VISIBLE | PMFS_EOFBLOCKS_FL)

#define INODES_PER_BLOCK(bt) (1 << (blk_type_to_shift[bt] - PMFS_INODE_BITS))

extern unsigned int blk_type_to_shift[PMFS_BLOCK_TYPE_MAX];
extern unsigned int blk_type_to_size[PMFS_BLOCK_TYPE_MAX];

/* ======================= Timing ========================= */
enum timing_category {
	create_t,
	new_inode_t,
	add_nondir_t,
	create_new_trans_t,
	create_commit_trans_t,
	unlink_t,
	remove_entry_t,
	unlink_new_trans_t,
	unlink_commit_trans_t,
	truncate_add_t,
	evict_inode_t,
	free_tree_t,
	free_inode_t,
	readdir_t,
	xip_read_t,
	read_find_blocks_t,
	read__pmfs_get_block_t,
	read_pmfs_find_data_blocks_t,
	__pmfs_find_data_blocks_t,
	read_get_inode_t,
	xip_write_t,
	xip_write_fast_t,
	allocate_blocks_t,
	internal_write_t,
	write_new_trans_t,
	write_commit_trans_t,
	write_find_block_t,
	memcpy_r_t,
	memcpy_w_t,
	alloc_blocks_t,
	new_trans_t,
	add_log_t,
	commit_trans_t,
	mmap_fault_t,
	fsync_t,
	recovery_t,
	TIMING_NUM,
};

extern const char *Timingstring[TIMING_NUM];
extern unsigned long long Timingstats[TIMING_NUM];
extern u64 Countstats[TIMING_NUM];

extern int measure_timing;
extern int support_clwb;

extern atomic64_t fsync_pages;

typedef struct timespec64 timing_t;

#define PMFS_START_TIMING(name, start) \
	{if (measure_timing) ktime_get_raw_ts64(&start);}

#define PMFS_END_TIMING(name, start) \
	{if (measure_timing) { \
		timing_t end; \
		ktime_get_raw_ts64(&end); \
		Timingstats[name] += \
			(end.tv_sec - start.tv_sec) * 1000000000 + \
			(end.tv_nsec - start.tv_nsec); \
	} \
	Countstats[name]++; \
	}


/* Function Prototypes */
extern void pmfs_error_mng(struct super_block *sb, const char *fmt, ...);

/* file.c */
extern int pmfs_mmap(struct file *file, struct vm_area_struct *vma);

/* balloc.c */
struct pmfs_range_node *pmfs_alloc_range_node_atomic(struct super_block *sb);
extern struct pmfs_range_node *pmfs_alloc_blocknode(struct super_block *sb);
extern void pmfs_free_blocknode(struct super_block *sb, struct pmfs_range_node *node);
extern void pmfs_init_blockmap(struct super_block *sb,
			       unsigned long init_used_size, int recovery);
extern int pmfs_free_blocks(struct super_block *sb, unsigned long blocknr, int num,
	unsigned short btype);
extern int pmfs_new_blocks(struct super_block *sb, unsigned long *blocknr,
			   unsigned int num, unsigned short btype, int zero,
			   int cpu);
extern unsigned long pmfs_count_free_blocks(struct super_block *sb);
extern unsigned int pmfs_get_free_numa_node(struct super_block *sb);

/* dir.c */
extern int pmfs_add_entry(pmfs_transaction_t *trans,
		struct dentry *dentry, struct inode *inode);
extern int pmfs_remove_entry(pmfs_transaction_t *trans,
		struct dentry *dentry, struct inode *inode);

/* namei.c */
extern struct dentry *pmfs_get_parent(struct dentry *child);

/* inode.c */

/* ioctl.c */
extern long pmfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
extern long pmfs_compat_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg);
#endif

/* super.c */
#ifdef CONFIG_PMFS_TEST
extern struct pmfs_super_block *get_pmfs_super(void);
#endif
extern void __pmfs_free_blocknode(struct pmfs_range_node *bnode);
extern struct super_block *pmfs_read_super(struct super_block *sb, void *data,
	int silent);
extern int pmfs_statfs(struct dentry *d, struct kstatfs *buf);
extern int pmfs_remount(struct super_block *sb, int *flags, char *data);

/* symlink.c */
extern int pmfs_block_symlink(struct inode *inode, const char *symname,
	int len);

/* Inline functions start here */

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __le32 pmfs_mask_flags(umode_t mode, __le32 flags)
{
	flags &= cpu_to_le32(PMFS_FL_INHERITED);
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & cpu_to_le32(PMFS_REG_FLMASK);
	else
		return flags & cpu_to_le32(PMFS_OTHER_FLMASK);
}

static inline int pmfs_calc_checksum(u8 *data, int n)
{
	u16 crc = 0;

	crc = crc16(~0, (__u8 *)data + sizeof(__le16), n - sizeof(__le16));
	if (*((__le16 *)data) == cpu_to_le16(crc))
		return 0;
	else
		return 1;
}

struct pmfs_blocknode_lowhigh {
       __le64 block_low;
       __le64 block_high;
};

enum bm_type {
	BM_4K = 0,
	BM_2M,
	BM_1G,
};

struct single_scan_bm {
	unsigned long bitmap_size;
	unsigned long *bitmap;
};

struct scan_bitmap {
	struct single_scan_bm scan_bm_4K;
	struct single_scan_bm scan_bm_2M;
	struct single_scan_bm scan_bm_1G;
};

struct inode_map {
	struct mutex inode_table_mutex;
	struct rb_root inode_inuse_tree;
	unsigned long num_range_node_inode;
	struct pmfs_range_node *first_inode_range;
	int allocated;
	int freed;
};

/*
 * PMFS super-block data in memory
 */
struct pmfs_sb_info {
	/*
	 * base physical and virtual address of PMFS (which is also
	 * the pointer to the super block)
	 */
	struct block_device *s_bdev;
	struct dax_device *s_dax_dev;
	phys_addr_t	phys_addr;
	phys_addr_t     phys_addr_2;
	void		*virt_addr;
	void            *virt_addr_2;
	struct list_head block_inuse_head;
	unsigned long	*block_start;
	unsigned long	*block_end;
	unsigned long	num_free_blocks;
	struct mutex 	s_lock;	/* protects the SB's buffer-head */
	unsigned long   num_blocks;
	int cpus;

	/*
	 * Backing store option:
	 * 1 = no load, 2 = no store,
	 * else do both
	 */
	unsigned int	pmfs_backing_option;

	/* Mount options */
	unsigned long	bpi;
	unsigned long	num_inodes;
	unsigned long	blocksize;
	unsigned long	initsize;
	unsigned long   initsize_2;
	unsigned long   pmem_size;
	unsigned long   pmem_size_2;
	unsigned long	s_mount_opt;
	kuid_t		uid;    /* Mount uid for root directory */
	kgid_t		gid;    /* Mount gid for root directory */
	umode_t		mode;   /* Mount mode for root directory */
	atomic_t	next_generation;
	/* inode tracking */
	struct mutex inode_table_mutex;
	unsigned int	s_inodes_count;  /* total inodes count (used or free) */
	unsigned int	s_free_inodes_count;    /* free inodes count */
	unsigned int	s_inodes_used_count;
	unsigned int	s_free_inode_hint;

	unsigned long num_blocknode_allocated;
	unsigned long num_inodenode_allocated;

	unsigned long head_reserved_blocks;

	/* Journaling related structures */
	atomic64_t    next_transaction_id;
	uint32_t    jsize;
	void       **journal_base_addr;
	struct mutex *journal_mutex;
	struct task_struct *log_cleaner_thread;
	wait_queue_head_t  log_cleaner_wait;
	bool redo_log;

	/* truncate list related structures */
	struct list_head s_truncate;
	struct mutex s_truncate_lock;

	struct inode_map *inode_maps;

	/* Decide new inode map id */
	unsigned long map_id;

	/* Number of NUMA nodes */
	int num_numa_nodes;

	/* process -> NUMA node mapping */
	int num_parallel_procs;
	struct process_numa *process_numa;

	/* Struct to hold NUMA node for each CPU */
	u8 *cpu_numa_node;

	/* struct to hold cpus for each NUMA node */
	struct numa_node_cpus *numa_cpus;

	/* Per-CPU free blocks list */
	struct free_list *free_lists;
	unsigned long per_list_blocks;
};

struct process_numa {
	int tgid;
	int numa_node;
};

struct numa_node_cpus {
	int *cpus;
	int num_cpus;
	struct cpumask cpumask;
};

struct pmfs_range_node_lowhigh {
	__le64 range_low;
	__le64 range_high;
};

#define	RANGENODE_PER_PAGE	256

struct pmfs_range_node {
	struct rb_node node;
	struct vm_area_struct *vma;
	unsigned long mmap_entry;
	union {
		/* Block, inode */
		struct {
			unsigned long range_low;
			unsigned long range_high;
		};
		/* Dir node */
		struct {
			unsigned long hash;
			void *direntry;
		};
	};
};

struct vma_item {
	struct rb_node node;
	struct vm_area_struct *vma;
	unsigned long mmap_entry;
};

static inline struct pmfs_sb_info *PMFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* If this is part of a read-modify-write of the super block,
 * pmfs_memunlock_super() before calling! */
static inline struct pmfs_super_block *pmfs_get_super(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	return (struct pmfs_super_block *)sbi->virt_addr;
}

static inline pmfs_journal_t *pmfs_get_journal(struct super_block *sb, int cpu)
{
	struct pmfs_super_block *ps = pmfs_get_super(sb);

	return (pmfs_journal_t *)((char *)ps +
				   (le64_to_cpu(ps->s_journal_offset)) +
				   (cpu * CACHELINE_SIZE));
}

static inline struct pmfs_inode *pmfs_get_inode_table(struct super_block *sb)
{
	struct pmfs_super_block *ps = pmfs_get_super(sb);

	return (struct pmfs_inode *)((char *)ps +
			le64_to_cpu(ps->s_inode_table_offset));
}

static inline struct pmfs_super_block *pmfs_get_redund_super(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	return (struct pmfs_super_block *)(sbi->virt_addr + PMFS_SB_SIZE);
}

/* If this is part of a read-modify-write of the block,
 * pmfs_memunlock_block() before calling! */
static inline void *pmfs_get_block(struct super_block *sb, u64 block)
{
	struct pmfs_super_block *ps = pmfs_get_super(sb);

	return block ? ((void *)ps + block) : NULL;
}

static inline int pmfs_get_reference(struct super_block *sb, u64 block,
	void *dram, void **nvmm, size_t size)
{
	int rc;

	*nvmm = pmfs_get_block(sb, block);
	rc = copy_mc_to_kernel(dram, *nvmm, size);
	return rc;
}

static inline int pmfs_get_numa_node(struct super_block *sb, int cpuid)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	return sbi->cpu_numa_node[cpuid];
}

/* uses CPU instructions to atomically write up to 8 bytes */
static inline void pmfs_memcpy_atomic (void *dst, const void *src, u8 size)
{
	switch (size) {
		case 1: {
			volatile u8 *daddr = dst;
			const u8 *saddr = src;
			*daddr = *saddr;
			break;
		}
		case 2: {
			volatile __le16 *daddr = dst;
			const u16 *saddr = src;
			*daddr = cpu_to_le16(*saddr);
			break;
		}
		case 4: {
			volatile __le32 *daddr = dst;
			const u32 *saddr = src;
			*daddr = cpu_to_le32(*saddr);
			break;
		}
		case 8: {
			volatile __le64 *daddr = dst;
			const u64 *saddr = src;
			*daddr = cpu_to_le64(*saddr);
			break;
		}
		default:
			pmfs_dbg("error: memcpy_atomic called with %d bytes\n", size);
			//BUG();
	}
}

/* assumes the length to be 4-byte aligned */
static inline void memset_nt(void *dest, uint32_t dword, size_t length)
{
	uint64_t dummy1, dummy2;
	uint64_t qword = ((uint64_t)dword << 32) | dword;

	asm volatile ("movl %%edx,%%ecx\n"
		"andl $63,%%edx\n"
		"shrl $6,%%ecx\n"
		"jz 9f\n"
		"1:      movnti %%rax,(%%rdi)\n"
		"2:      movnti %%rax,1*8(%%rdi)\n"
		"3:      movnti %%rax,2*8(%%rdi)\n"
		"4:      movnti %%rax,3*8(%%rdi)\n"
		"5:      movnti %%rax,4*8(%%rdi)\n"
		"8:      movnti %%rax,5*8(%%rdi)\n"
		"7:      movnti %%rax,6*8(%%rdi)\n"
		"8:      movnti %%rax,7*8(%%rdi)\n"
		"leaq 64(%%rdi),%%rdi\n"
		"decl %%ecx\n"
		"jnz 1b\n"
		"9:     movl %%edx,%%ecx\n"
		"andl $7,%%edx\n"
		"shrl $3,%%ecx\n"
		"jz 11f\n"
		"10:     movnti %%rax,(%%rdi)\n"
		"leaq 8(%%rdi),%%rdi\n"
		"decl %%ecx\n"
		"jnz 10b\n"
		"11:     movl %%edx,%%ecx\n"
		"shrl $2,%%ecx\n"
		"jz 12f\n"
		"movnti %%eax,(%%rdi)\n"
		"12:\n"
		: "=D"(dummy1), "=d" (dummy2) : "D" (dest), "a" (qword), "d" (length) : "memory", "rcx");
}

static inline unsigned long BKDRHash(const char *str, int length)
{
	unsigned int seed = 131;
	unsigned long hash = 0;
	int i;

	for (i = 0; i < length; i++)
		hash = hash * seed + (*str++);

	return hash;
}

static inline u64
pmfs_get_block_off(struct super_block *sb, unsigned long blocknr,
		    unsigned short btype)
{
	return (u64)blocknr << PAGE_SHIFT;
}

static inline int pmfs_get_cpuid(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);

	return smp_processor_id() % sbi->cpus;
}

static inline int get_block_cpuid(struct pmfs_sb_info *sbi,
	unsigned long blocknr)
{
	unsigned long temp_blocknr = 0;
	int cpuid = blocknr / sbi->per_list_blocks;

	if (sbi->num_numa_nodes == 2) {
		if (sbi->cpus == 96 || sbi->cpus == 32) {
			if (blocknr >= sbi->block_start[1]) {
				temp_blocknr = blocknr - (sbi->block_start[1] - sbi->block_end[0]);
				cpuid = temp_blocknr / sbi->per_list_blocks;
			}
		}
	}

	if (sbi->num_numa_nodes == 2) {
		if (sbi->cpus == 96) {
			if (cpuid >= 24 && cpuid < 48) {
				cpuid += 24;
			} else if (cpuid >= 48 && cpuid < 72) {
				cpuid -= 24;
			}
		}
		else if (sbi->cpus == 32) {
			if (cpuid >= 8 && cpuid < 16) {
				cpuid += 8;
			} else if (cpuid >= 16 && cpuid < 24) {
				cpuid -= 8;
			}
		}
	}
	return cpuid;
}

static inline unsigned long
pmfs_get_numblocks(unsigned short btype)
{
	unsigned long num_blocks;

	if (btype == PMFS_BLOCK_TYPE_4K) {
		num_blocks = 1;
	} else if (btype == PMFS_BLOCK_TYPE_2M) {
		num_blocks = 512;
	} else {
		//btype == PMFS_BLOCK_TYPE_1G 
		num_blocks = 0x40000;
	}
	return num_blocks;
}

static inline unsigned long
pmfs_get_blocknr(struct super_block *sb, u64 block, unsigned short btype)
{
	return block >> PAGE_SHIFT;
}

static inline unsigned long pmfs_get_pfn(struct super_block *sb, u64 block)
{
	return (PMFS_SB(sb)->phys_addr + block) >> PAGE_SHIFT;
}

static inline int pmfs_is_mounting(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = (struct pmfs_sb_info *)sb->s_fs_info;
	return sbi->s_mount_opt & PMFS_MOUNT_MOUNTING;
}

static inline int is_dir_init_entry(struct super_block *sb,
	struct pmfs_direntry *entry)
{
	if (entry->name_len == 1 && strncmp(entry->name, ".", 1) == 0)
		return 1;
	if (entry->name_len == 2 && strncmp(entry->name, "..", 2) == 0)
		return 1;

	return 0;
}

#include "wprotect.h"
#include "balloc.h"

/*
 * Inodes and files operations
 */

/* dir.c */
extern const struct file_operations pmfs_dir_operations;
int pmfs_insert_dir_tree(struct super_block *sb,
			 struct pmfs_inode_info_header *sih, const char *name,
			 int namelen, struct pmfs_direntry *direntry);
int pmfs_remove_dir_tree(struct super_block *sb,
			 struct pmfs_inode_info_header *sih, const char *name, int namelen,
			 struct pmfs_direntry **create_dentry);
void pmfs_delete_dir_tree(struct super_block *sb,
			  struct pmfs_inode_info_header *sih);
struct pmfs_direntry *pmfs_find_dentry(struct super_block *sb,
				       struct pmfs_inode *pi, struct inode *inode,
				       const char *name, unsigned long name_len);

/* xip.c */
int pmfs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		     unsigned int flags, struct iomap *iomap, bool taking_lock);
int pmfs_iomap_end(struct inode *inode, loff_t offset, loff_t length,
		   ssize_t written, unsigned int flags, struct iomap *iomap);


/* file.c */
extern const struct inode_operations pmfs_file_inode_operations;
extern const struct file_operations pmfs_xip_file_operations;
int pmfs_fsync(struct file *file, loff_t start, loff_t end, int datasync);

/* inode.c */
extern const struct address_space_operations pmfs_aops_xip;

/* bbuild.c */
void pmfs_init_header(struct super_block *sb,
		      struct pmfs_inode_info_header *sih, u16 i_mode);
void pmfs_save_blocknode_mappings(struct super_block *sb);
void pmfs_save_inode_list(struct super_block *sb);
int pmfs_recovery(struct super_block *sb, unsigned long size, unsigned long size_2);

/* namei.c */
extern const struct inode_operations pmfs_dir_inode_operations;
extern const struct inode_operations pmfs_special_inode_operations;

/* symlink.c */
extern const struct inode_operations pmfs_symlink_inode_operations;

int pmfs_check_integrity(struct super_block *sb,
	struct pmfs_super_block *super);
void *pmfs_ioremap(struct super_block *sb, phys_addr_t phys_addr,
	ssize_t size);

int pmfs_check_dir_entry(const char *function, struct inode *dir,
			  struct pmfs_direntry *de, u8 *base,
			  unsigned long offset);

static inline int pmfs_match(int len, const char *const name,
			      struct pmfs_direntry *de)
{
	if (len == de->name_len && de->ino && !memcmp(de->name, name, len))
		return 1;
	return 0;
}

int pmfs_search_dirblock(u8 *blk_base, struct inode *dir, struct qstr *child,
			  unsigned long offset,
			  struct pmfs_direntry **res_dir,
			  struct pmfs_direntry **prev_dir);

#define ANY_CPU                (65536)

/* pmfs_stats.c */
#define	PMFS_PRINT_TIMING	0xBCD00010
#define	PMFS_CLEAR_STATS	0xBCD00011
#define PMFS_GET_AVAILABLE_HUGEPAGES 0xBCD00012

void pmfs_print_timing_stats(void);
void pmfs_print_available_hugepages(struct super_block *sb);
void pmfs_clear_stats(void);

#endif /* __PMFS_H */
