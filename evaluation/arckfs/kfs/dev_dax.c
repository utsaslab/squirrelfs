#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>

#include "../include/kfs_config.h"
#include "dev_dax.h"
#include "super.h"

struct sufs_dev_arr sufs_dev_arr;

/* Debugging code */
static void sufs_print_dev_info(void)
{
    int i;

    for (i = 0; i < sufs_dev_arr.num; i++)
    {
        printk("dev %d, address: %lx, size: %ld\n",
                i, sufs_dev_arr.start_virt_addr[i],
                sufs_dev_arr.size_in_bytes[i]);
    }
}

static int sufs_init_one_dev(char * path, int index)
{
    struct file * dev_file = filp_open(path, O_RDWR, 0);
    struct dax_device *dax_dev;

    int ret = 0;

    if (IS_ERR(dev_file))
    {
        ret = PTR_ERR(dev_file);
        printk("Open: %s failed with error : %d\n", path, ret);

        return ret;
    }

    dax_dev = inode_dax(dev_file->f_inode);

    /* In our version of the dax_direct_access, it will always succeed... */
    sufs_dev_arr.size_in_bytes[index] =
            dax_direct_access(dax_dev, 0, LONG_MAX / PAGE_SIZE, DAX_ACCESS,
                    (void **) &(sufs_dev_arr.start_virt_addr[index]), NULL);

    sufs_dev_arr.size_in_bytes[index] *= PAGE_SIZE;

    sufs_sb_update_one_dev(index, sufs_dev_arr.start_virt_addr[index],
            sufs_dev_arr.size_in_bytes[index]);

    sufs_sb.tot_bytes = sufs_sb.end_virt_addr - sufs_sb.start_virt_addr + 1;

    filp_close(dev_file, NULL);

    return 0;
}

/* Iterate from /dev/dax0.0 to /dev/dax/"num".0 device and init each of them */
int sufs_init_dev(int num)
{
    int i = 0;

    /* Make this static to avoid overflowing the tiny kernel stack */
    static char name[PATH_MAX];

    sufs_dev_arr.num = num;

    for (i = 0; i < num; i++)
    {
        int ret;
        /* Yes, I know sprintf is unsafe */
        /* TODO: Some hardwired code here but should be fine */
        sprintf(name, "/dev/dax%d.0", i);

        if ((ret = sufs_init_one_dev(name, i)) != 0)
            return ret;
    }

    sufs_sb_update_devs(&sufs_dev_arr);

    sufs_print_dev_info();

    return 0;
}

