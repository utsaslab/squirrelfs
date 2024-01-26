#include "pmfs.h"
#include "inode.h"

const char *Timingstring[TIMING_NUM] =
{
	"create",
	"new_inode",
	"add_nondir",
	"create_new_trans",
	"create_commit_trans",
	"unlink",
	"remove_entry",
	"unlink_new_trans",
	"unlink_commit_trans",
	"truncate_add",
	"evict_inode",
	"free_tree",
	"free_inode",
	"readdir",
	"xip_read",
	"read_find_blocks",
	"read__pmfs_get_block",
	"read_pmfs_find_data_blocks",
	"__pmfs_find_data_blocks",
	"read_get_inode",
	"xip_write",
	"xip_write_fast",
	"allocate_blocks",
	"internal_write",
	"write_new_trans",
	"write_commit_trans",
	"write_find_blocks",
	"memcpy_read",
	"memcpy_write",
	"alloc_blocks",
	"new_trans",
	"add_logentry",
	"commit_trans",
	"mmap_fault",
	"fsync",
	"recovery",
};

unsigned long long Timingstats[TIMING_NUM];
u64 Countstats[TIMING_NUM];

atomic64_t fsync_pages = ATOMIC_INIT(0);

void pmfs_print_IO_stats(void)
{
	printk("=========== PMFS I/O stats ===========\n");
	printk("Fsync %ld pages\n", atomic64_read(&fsync_pages));
}

void pmfs_print_available_hugepages(struct super_block *sb)
{
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	int i;
	unsigned long num_hugepages = 0;
	unsigned long num_free_blocks = 0;
	struct free_list *free_list;


	printk("======== PMFS Available Free Hugepages =======\n");
	for (i = 0; i < sbi->cpus; i++) {
		free_list = pmfs_get_free_list(sb, i);
		num_hugepages += free_list->num_blocknode_huge_aligned;
		printk("free list idx %d, free hugepages %lu, free unaligned pages %lu\n",
		       free_list->index, free_list->num_blocknode_huge_aligned,
		       free_list->num_blocknode_unaligned);
		num_free_blocks += free_list->num_free_blocks;
	}
	printk("Total free hugepages %lu, Total free blocks = %lu, Possible free hugepages = %lu\n",
	       num_hugepages, num_free_blocks, num_free_blocks / 512);
}

void pmfs_print_timing_stats(void)
{
	int i;

	printk("======== PMFS kernel timing stats ========\n");
	for (i = 0; i < TIMING_NUM; i++) {
		if (measure_timing || Timingstats[i]) {
			printk("%s: count %llu, timing %llu, average %llu\n",
				Timingstring[i],
				Countstats[i],
				Timingstats[i],
				Countstats[i] ?
				Timingstats[i] / Countstats[i] : 0);
		} else {
			printk("%s: count %llu\n",
				Timingstring[i],
				Countstats[i]);
		}
	}

	pmfs_print_IO_stats();
}

void pmfs_clear_stats(void)
{
	int i;

	printk("======== Clear PMFS kernel timing stats ========\n");
	for (i = 0; i < TIMING_NUM; i++) {
		Countstats[i] = 0;
		Timingstats[i] = 0;
	}
}
