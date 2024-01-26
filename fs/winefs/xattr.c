#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mbcache.h>
#include <linux/quotaops.h>
#include <linux/iversion.h>
#include "pmfs.h"
#include "xattr.h"
#include "inode.h"

const struct xattr_handler *pmfs_xattr_handlers[] = {
	&pmfs_xattr_user_handler,
	NULL
};

static inline void
pmfs_update_special_xattr(struct super_block *sb, void *pmem_addr, struct pmfs_special_xattr_info *xattr_info)
{
	if (pmem_addr && xattr_info) {
		pmfs_memunlock_range(sb, pmem_addr, sizeof(struct pmfs_special_xattr_info));
		memcpy(pmem_addr, (void *)xattr_info, sizeof(struct pmfs_special_xattr_info));
		pmfs_flush_buffer((void *)xattr_info, sizeof(struct pmfs_special_xattr_info), false);
		pmfs_memlock_range(sb, pmem_addr, sizeof(struct pmfs_special_xattr_info));
	}
}

int
pmfs_xattr_set(struct inode *inode, const char *name,
	       const void *value, size_t value_len, int flags)
{
	pmfs_transaction_t *trans;
	struct pmfs_inode *pi;
	int cpu;
	struct super_block *sb = inode->i_sb;
	const char *special_xattr = "file_type";
	const char *special_xattr_value_mmap = "mmap";
	const char *special_xattr_value_sys = "sys";
	void* bp;
	int ret = 0;
	unsigned long blocknr = 0;
	int num_blocks = 0;
	struct pmfs_special_xattr_info xattr_info;

	pmfs_dbg("%s: start\n", __func__);
	cpu = pmfs_get_cpuid(sb);

	if (memcmp(name, special_xattr, strlen(special_xattr))) {
		pmfs_dbg("checking name. name = %s, special_xattr = %s, strlen(special_xattr) = %lu\n",
			 name, special_xattr, strlen(special_xattr));
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (memcmp(value, (void *)special_xattr_value_mmap, value_len) &&
	    memcmp(value, (void *)special_xattr_value_sys, value_len)) {
		pmfs_dbg("value = %s, special_xattr_value_mmap = %s, value_len = %lu\n",
			 value, special_xattr_value_mmap, value_len);
		ret = -EOPNOTSUPP;
		goto out;
	}

	pi = pmfs_get_inode(sb, inode->i_ino);
	trans = pmfs_new_transaction(sb, MAX_INODE_LENTRIES, cpu);

	if (!pi->i_xattr) {
		pmfs_add_logentry(sb, trans, pi, sizeof(*pi), LE_DATA);
		num_blocks = pmfs_new_blocks(sb, &blocknr, 1, PMFS_BLOCK_TYPE_4K, 1, cpu);
		if (num_blocks == 0) {
			ret = -ENOSPC;
			pmfs_abort_transaction(sb, trans);
			goto out;
		}
		pmfs_memunlock_range(sb, pi, CACHELINE_SIZE);
		pi->i_xattr = pmfs_get_block_off(sb, blocknr, PMFS_BLOCK_TYPE_4K);
		pmfs_memlock_range(sb, pi, CACHELINE_SIZE);
	}

	xattr_info.name = PMFS_SPECIAL_XATTR_NAME;
	if (!memcmp(value, (void *)special_xattr_value_mmap, value_len)) {
		xattr_info.value = PMFS_SPECIAL_XATTR_MMAP_VALUE;
		pi->huge_aligned_file = 1;
	} else {
		xattr_info.value = PMFS_SPECIAL_XATTR_SYS_VALUE;
		pi->huge_aligned_file = 0;
	}

	bp = pmfs_get_block(sb, pi->i_xattr);
	pmfs_update_special_xattr(sb, bp, &xattr_info);
	pmfs_commit_transaction(sb, trans);

 out:
	return ret;
}

ssize_t
pmfs_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	int ret = 0;
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
	const char *special_xattr = "file_type";
	const char *user_special_xattr = "user.file_type";
	int fraction_of_hugepage_files = 0;

	inode_lock(inode);


	/* [TODO]: List through the inodes of all the files if dir
	 * and mark dir as hugepage aligned if all the files
	 * in the dir are hugepage aligned
	 */
	if (S_ISDIR(inode->i_mode)) {
		fraction_of_hugepage_files = pmfs_get_ratio_hugepage_files_in_dir(sb, inode);
		if (fraction_of_hugepage_files == 1) {
			pi->huge_aligned_file = 1;
		}
	}

	if (pi->huge_aligned_file && !pi->i_xattr) {
		ret = pmfs_xattr_set(inode, special_xattr, "mmap", 4, 0);
		if (ret != 0) {
			goto out;
		}
	}

	if (!pi->i_xattr) {
		ret = 0;
		goto out;
	}

	if (buffer && buffer_size > strlen(user_special_xattr)) {
		memcpy(buffer, user_special_xattr, strlen(user_special_xattr));
		buffer += strlen(user_special_xattr);
		ret += strlen(user_special_xattr);
		*buffer++ = 0;
		ret += 1;
	} else if (buffer) {
		ret = -ERANGE;
	} else {
		ret = strlen(user_special_xattr) + 1;
	}

 out:
	inode_unlock(inode);
	return ret;
}

int pmfs_xattr_get(struct inode *inode, const char *name,
		   void *buffer, size_t size)
{
	struct pmfs_inode *pi;
	struct super_block *sb = inode->i_sb;
	const char *special_xattr = "file_type";
	void *bp;
	int ret = 0;
	int value = 0;

	inode_lock(inode);

	if (memcmp(name, special_xattr, strlen(special_xattr))) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	pi = pmfs_get_inode(sb, inode->i_ino);

	if (pi->huge_aligned_file && !pi->i_xattr) {
		ret = pmfs_xattr_set(inode, special_xattr, "mmap", 4, 0);
		if (ret != 0) {
			goto out;
		}
	}

	if (!pi->i_xattr) {
		ret = -ENODATA;
		goto out;
	}

	bp = pmfs_get_block(sb, pi->i_xattr);
	memcpy(&value, (void *) (bp + sizeof(int)), sizeof(int));
	pmfs_dbg_verbose("%s: value = %d. buffer size = %lu\n", __func__, value, size);
	if (buffer) {
		if (value == PMFS_SPECIAL_XATTR_MMAP_VALUE && size >= 4) {
			memcpy(buffer, "mmap", 4);
			ret = 4;
		} else if (value == PMFS_SPECIAL_XATTR_SYS_VALUE && size >= 3) {
			memcpy(buffer, "sys", 3);
			ret = 3;
		} else if (value != PMFS_SPECIAL_XATTR_MMAP_VALUE &&
			   value != PMFS_SPECIAL_XATTR_SYS_VALUE){
			ret = -ENODATA;
		} else {
			ret = -ERANGE;
		}
	} else {
		if (value == PMFS_SPECIAL_XATTR_MMAP_VALUE)
			ret = 4;
		else
			ret = 3;
	}
 out:
	inode_unlock(inode);
	return ret;
}
