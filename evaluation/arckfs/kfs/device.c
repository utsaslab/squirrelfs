#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include "../include/kfs_config.h"
#include "../include/ioctl.h"
#include "dev_dax.h"
#include "super.h"
#include "mmap.h"
#include "inode.h"
#include "balloc.h"

/*
 * How many pm devices supremefs kernel module is responsible for? */
/* 1: /dev/dax0.0,
 * 2: /dev/dax0.0, /dev/dax1.0
 * 4: /dev/dax0.0, /dev/dax1.0 ... /dev/dax3.0
 * 8: /dev/dax0.0, /dev/dax1.0 ... /dev/dax7.0
 */

int pm_nr = 0;
module_param(pm_nr, int, S_IRUGO);
MODULE_PARM_DESC(pm_nr, "Number of PM deivce");

int sufs_kfs_dele_thrds = 0;
module_param(sufs_kfs_dele_thrds, int, S_IRUGO);
MODULE_PARM_DESC(sufs_kfs_dele_thrds, "Number of delegation threads");

static int sufs_kfs_do_init(void);

static long sufs_kfs_ioctl(struct file *file, unsigned int cmd,
        unsigned long arg)
{
    switch (cmd)
    {
        case SUFS_CMD_MOUNT:
            return sufs_mount();
        case SUFS_CMD_UMOUNT:
            return sufs_umount(arg);

        case SUFS_CMD_MAP:
            return sufs_mmap_file(arg);
        case SUFS_CMD_UNMAP:
            return sufs_unmap_file(arg);

        case SUFS_CMD_ALLOC_INODE:
            return sufs_alloc_inode_to_libfs(arg);
        case SUFS_CMD_FREE_INODE:
            return sufs_free_inode_from_libfs(arg);

        case SUFS_CMD_GET_PMNODES_INFO:
            return sufs_sys_info_libfs(arg);
        case SUFS_CMD_ALLOC_BLOCK:
            return sufs_alloc_blocks_to_libfs(arg);
        case SUFS_CMD_FREE_BLOCK:
            return sufs_free_blocks_from_libfs(arg);

        case SUFS_CMD_CHOWN:
            return sufs_chown(arg);
        case SUFS_CMD_CHMOD:
            return sufs_chmod(arg);

        case SUFS_CMD_DEBUG_READ:
            return sufs_debug_read();

        case SUFS_CMD_DEBUG_INIT:
            return sufs_kfs_do_init();

        default:
            printk("%s: unsupported command %x\n", __func__, cmd);
    }

    return -EINVAL;
}

static struct file_operations sufs_fops =
{
        .mmap = sufs_kfs_mmap,
        .unlocked_ioctl = sufs_kfs_ioctl
};

static struct class * sufs_class;
static dev_t sufs_dev_t;

static int sufs_hardware_check(void)
{
    /*
     * While there is many other instructions,
     * clwb is the most widely used and efficient one. So just complain if
     * there is no clwb support for now
     */

#if SUFS_CLWB_FLUSH
    if (!static_cpu_has(X86_FEATURE_CLWB))
    {
        printk("No clwb support!\n");
        return 1;
    }
#else
    if (!static_cpu_has(X86_FEATURE_CLFLUSH))
    {
            printk("No clush support!\n");
            return 1;
    }
#endif

    return 0;
}

static int sufs_init(void)
{
    struct device *dev;

    int ret = 0;

    if (sufs_hardware_check())
    {
        printk("hardware check failed!\n");
        return -EPERM;
    }

    if (pm_nr == 0)
        pm_nr = num_online_nodes();

    if (sufs_kfs_dele_thrds == 0)
        sufs_kfs_dele_thrds = SUFS_KFS_DEF_DELE_THREADS_PER_SOCKET;

    if (sufs_kfs_dele_thrds != 0)
        sufs_kfs_delegation = 1;

    if ((ret = register_chrdev(SUFS_MAJOR, SUFS_DEV_NAME, &sufs_fops)) != 0)
    {
        return ret;
    }

    sufs_dev_t = MKDEV(SUFS_MAJOR, 0);

    if (IS_ERR(sufs_class = class_create(THIS_MODULE, SUFS_DEV_NAME)))
    {
        /* TODO: error macro */
        printk("Error in class_create!\n");
        ret = PTR_ERR(sufs_class);
        goto out_chrdev;
    }


    if (IS_ERR(dev = device_create(sufs_class, NULL, sufs_dev_t, NULL,
            SUFS_DEV_NAME)))
    {
        printk("Error in device_create!\n");
        ret = PTR_ERR(dev);
        goto out_class;
    }

    printk("sufs init done!\n");

    return 0;

out_class:
    class_destroy(sufs_class);

out_chrdev:
    unregister_chrdev(SUFS_MAJOR, SUFS_DEV_NAME);

    return -ENOMEM;
}

/*
 * We cannot run the below code in sufs_init function since if we compile the
 * module as built-in, it cannot open "/dev/dax*.0" during kernel boot up
 */
static int sufs_kfs_do_init(void)
{
    int ret = 0;

    sufs_init_sb();

    if (sufs_init_dev(pm_nr) != 0)
        return -ENOMEM;

    if ((ret = sufs_fs_init()) == 0)
    {
        printk("sufs do init succeed!\n");
    }

    return ret;
}

static void sufs_exit(void)
{
    sufs_fs_fini();

    device_destroy(sufs_class, sufs_dev_t);

    class_destroy(sufs_class);

    unregister_chrdev(SUFS_MAJOR, SUFS_DEV_NAME);

    return;
}

MODULE_LICENSE("GPL");
module_init(sufs_init);
module_exit(sufs_exit);

