#include <linux/device-mapper.h>
#include <linux/atomic.h>
#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blk_types.h>
#include <linux/sysfs.h>
#include <linux/overflow.h>
#include <linux/blk-mq.h>  
#include <linux/blk_types.h>
#include <linux/sched.h>

static atomic64_t read_request;
static atomic64_t write_request;
static atomic64_t total_read_size;
static atomic64_t total_write_size;

struct my_dm_target
{
    struct dm_dev *dev;
    sector_t start;
};

static int basic_target_map(struct dm_target *ti, struct bio *bio)
{
    struct my_dm_target *mdt = ti->private;
    int op = bio_op(bio);

    size_t sz = bio->bi_iter.bi_size;

    bio->bi_iter.bi_sector += mdt->start;
    bio_set_dev(bio, mdt->dev->bdev);

    if (op == REQ_OP_WRITE) {
        atomic64_inc(&write_request);
        atomic64_add(sz, &total_write_size);
    } else { /* REQ_OP_READ */
        atomic64_inc(&read_request);
        atomic64_add(sz, &total_read_size);
    }

    return DM_MAPIO_REMAPPED;
}


static int basic_target_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct my_dm_target *mdt;

    printk(KERN_INFO "basic_target_ctr called with argc=%u\n", argc);

    for (int i = 0; i < argc; i++)
    {
        printk(KERN_INFO "argv[%d] = '%s'\n", i, argv[i]);
    }

    if (argc != 1)
    {
        ti->error = "Invalid argument count: expected 1 (device_path)";
        printk(KERN_ERR "Invalid argument count: expected 1, got %u\n", argc);
        return -EINVAL;
    }

    mdt = kmalloc(sizeof(struct my_dm_target), GFP_KERNEL);
    if (!mdt)
    {
        ti->error = "Failed to allocate target context";
        printk(KERN_ERR "Failed to allocate target context\n");
        return -ENOMEM;
    }

    mdt->start = 0; 

    if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &mdt->dev))
    {
        ti->error = "Device lookup failed";
        printk(KERN_ERR "Device lookup failed for '%s'\n", argv[0]);
        kfree(mdt);
        return -EINVAL;
    }

    ti->private = mdt;

    printk(KERN_INFO "Target created successfully\n");
    return 0;
}

static void basic_target_dtr(struct dm_target *ti)
{
    struct my_dm_target *mdt = (struct my_dm_target *)ti->private;
    printk(KERN_DEBUG "\n<<in function basic_target_dtr \n");
    dm_put_device(ti, mdt->dev);
    kfree(mdt);
    
    atomic64_set(&read_request,     0);
    atomic64_set(&write_request,    0);
    atomic64_set(&total_read_size,  0);
    atomic64_set(&total_write_size, 0);
    
    printk(KERN_DEBUG "\n>>out function basic_target_dtr \n");
}

static struct target_type basic_target = {

    .name = "dmp",
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr = basic_target_ctr,
    .dtr = basic_target_dtr,
    .map = basic_target_map};

/*-------------------------------------------Module Functions ---------------------------------*/

static struct kobject *sysfs_kobj;

static u64 safe_avg(u64 total, u64 count)
{
    u64 avg = 0;

    if (count == 0)
        return 0;

    avg = div64_u64(total, count);
    return avg;
}

static ssize_t show_volumes(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    u64 total_w = atomic64_read(&total_write_size);
    u64 count_w = atomic64_read(&write_request);
    u64 total_r = atomic64_read(&total_read_size);
    u64 count_r = atomic64_read(&read_request);

    u64 total_req, total_size;
    u64 avg_read_size, avg_write_size, avg_total_size;

    if (check_add_overflow(count_r, count_w, &total_req))
    {
        pr_warn("Request count overflow detected\n");
        total_req = U64_MAX;
    }

    if (check_add_overflow(total_r, total_w, &total_size))
    {
        pr_warn("Total size overflow detected\n");
        total_size = U64_MAX;
    }

    avg_read_size =  safe_avg(total_r, count_r);
    avg_write_size = safe_avg(total_w, count_w);
    avg_total_size = safe_avg(total_size, total_req);

    return sprintf(buf,
                   "read:\n"
                   "       reqs: %llu\n"
                   "       avg size: %llu\n"
                   "write:\n"
                   "       reqs: %llu\n"
                   "       avg size: %llu\n"
                   "total:\n"
                   "       reqs: %llu\n"
                   "       avg size: %llu\n",
                   count_r, avg_read_size,
                   count_w, avg_write_size,
                   total_req, avg_total_size);
}
struct kobj_attribute volumes_attr = __ATTR(volumes, 0444, show_volumes, NULL);

static ssize_t reset_store(struct kobject *kobj, struct kobj_attribute *attr,
                           const char *buf, size_t count)
{
    atomic64_set(&read_request,     0);
    atomic64_set(&write_request,    0);
    atomic64_set(&total_read_size,  0);
    atomic64_set(&total_write_size, 0);

    return count;
}
static struct kobj_attribute reset_attr = __ATTR(reset, 0200, NULL, reset_store);

static struct attribute *attrs[] = {
    &volumes_attr.attr,
    &reset_attr.attr,
    NULL};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int __init init_basic_target(void)
{
    int result;

    printk(KERN_INFO "Initializing dmp module\n");

    result = dm_register_target(&basic_target);
    if (result < 0)
    {
        printk(KERN_ERR "Error registering target 'dmp'\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "Target 'dmp' registered successfully\n");

    sysfs_kobj = kobject_create_and_add("stat", &THIS_MODULE->mkobj.kobj);
    if (!sysfs_kobj)
    {
        dm_unregister_target(&basic_target);
        printk(KERN_ERR "Failed to create sysfs entry\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "Sysfs directory created\n");

    if (sysfs_create_group(sysfs_kobj, &attr_group))
    {
        printk(KERN_ERR "Failed to create sysfs attribute group\n");
        kobject_put(sysfs_kobj);
        return -ENOMEM;
    }
    printk(KERN_INFO "Sysfs attribute group created\n");

    atomic64_set(&read_request,     0);
    atomic64_set(&write_request,    0);
    atomic64_set(&total_read_size,  0);
    atomic64_set(&total_write_size, 0);

    printk(KERN_INFO "Module initialized successfully\n");
    return 0;
}

static void __exit cleanup_basic_target(void)
{
    dm_unregister_target(&basic_target);
    sysfs_remove_group(sysfs_kobj, &attr_group);
    kobject_put(sysfs_kobj);

    atomic64_set(&read_request,     0);
    atomic64_set(&write_request,    0);
    atomic64_set(&total_read_size,  0);
    atomic64_set(&total_write_size, 0);
}

module_init(init_basic_target);
module_exit(cleanup_basic_target);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Korban Pavel")