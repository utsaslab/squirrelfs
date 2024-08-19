#include <linux/string.h>
#include <linux/fs.h>
#include "pmfs.h"
#include "xattr.h"

static bool
pmfs_xattr_user_list(struct dentry *dentry)
{
	return test_opt(dentry->d_sb, XATTR_USER);
}

static int
pmfs_xattr_user_get(const struct xattr_handler *handler,
		    struct dentry *unused, struct inode *inode,
		    const char *name, void *buffer, size_t size)
{
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;
	return pmfs_xattr_get(inode,
			      name, buffer, size);
}

static int
pmfs_xattr_user_set(const struct xattr_handler *handler,
			struct mnt_idmap *idmap,
		    struct dentry *unused, struct inode *inode,
		    const char *name, const void *value,
		    size_t size, int flags)
{
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;
	return pmfs_xattr_set(inode,
			      name, value, size, flags);
}

const struct xattr_handler pmfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= pmfs_xattr_user_list,
	.get	= pmfs_xattr_user_get,
	.set	= pmfs_xattr_user_set,
};
