#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/types.h>
#include <linux/ide.h>

#define DEVICE_CNT              1
#define DEVICE_NAME             "key"

#define KEY0_VALUE              0xf0
#define INVAKEY                 0x00

struct key_device {
        int major;
        int minor;
        dev_t devid;
        struct cdev key_cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        int key_gpio;
        atomic_t key_value;
};
static struct key_device key_dev;

static int key_open(struct inode *inode, struct file *file);
static ssize_t key_read(struct file *file,
                        char __user *user,
                        size_t count,
                        loff_t *loff);
static int key_release(struct inode *inode, struct file *file);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = key_open,
        .read = key_read,
        .release = key_release,
};

static int key_open(struct inode *inode, struct file *file)
{
        file->private_data = &key_dev;
        return 0;
}

static ssize_t key_read(struct file *file,
                        char __user *user,
                        size_t count,
                        loff_t *loff)
{
        int ret = 0;
        int value = 0;
        struct key_device *dev = file->private_data;

        if (gpio_get_value(dev->key_gpio) == 0) {
                while(gpio_get_value(dev->key_gpio) == 0);
                atomic_set(&dev->key_value, KEY0_VALUE);
        } else {
                atomic_set(&dev->key_value, INVAKEY);
        }
        value = atomic_read(&dev->key_value);

        ret = copy_to_user(user, &value, sizeof(value));

        return ret;
}

static int key_release(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}


static int key_io_config(struct key_device *dev)
{
        int ret = 0;

        dev->nd = of_find_node_by_path("/key");
        if (dev->nd == NULL) {
                printk("find node error!\n");
                ret = -EINVAL;
                goto error;
        }
        dev->key_gpio = of_get_named_gpio(dev->nd, "key-gpios", 0);
        if (dev->key_gpio < 0) {
                printk("get named error!\n");
                ret = -EINVAL;
                goto error;
        }
        ret = gpio_request(dev->key_gpio, "key");
        if (ret != 0) {
                printk("gpio request error!\n");
                ret = -EBUSY;
                goto error;
        }
        ret = gpio_direction_input(dev->key_gpio);
        if (ret != 0) {
                printk("gpio dir set error!\n");
                gpio_free(dev->key_gpio);
                goto error;
        }

error:
        return ret;
}

static int __init user_key_init(void)
{
        int ret = 0;

        if (key_dev.major) {
                key_dev.devid = MKDEV(key_dev.major, key_dev.minor);
                ret = register_chrdev_region(key_dev.devid, DEVICE_CNT, DEVICE_NAME);
        } else {
                ret = alloc_chrdev_region(&key_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_chrdev_region;
        }
        key_dev.major = MAJOR(key_dev.devid);
        key_dev.minor = MINOR(key_dev.devid);
        printk("major:%d minor:%d\n", key_dev.major, key_dev.minor);

        cdev_init(&key_dev.key_cdev, &ops);
        ret = cdev_add(&key_dev.key_cdev, key_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }

        key_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(key_dev.class)) {
                printk("class create error!\n");
                ret = -EFAULT;
                goto fail_class_create;
        }
        key_dev.device = device_create(key_dev.class, NULL, 
                                       key_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(key_dev.device)) {
                printk("device create error!\n");
                ret = -EFAULT;
                goto fail_device_create;
        }

        ret = key_io_config(&key_dev);
        if (ret < 0) {
                printk("key io config error!\n");
                goto fail_io_config;
        }

        atomic_set(&key_dev.key_value, 0);

        goto success;

fail_io_config:
        device_destroy(key_dev.class, key_dev.devid);
fail_device_create:
        class_destroy(key_dev.class);
fail_class_create:
        cdev_del(&key_dev.key_cdev);
fail_cdev_add:
        unregister_chrdev_region(key_dev.devid, DEVICE_CNT);
fail_chrdev_region:
success:
        return ret;
}

static void __exit user_key_exit(void)
{
        gpio_free(key_dev.key_gpio);
        device_destroy(key_dev.class, key_dev.devid);
        class_destroy(key_dev.class);
        cdev_del(&key_dev.key_cdev);
        unregister_chrdev_region(key_dev.devid, DEVICE_CNT);
}

module_init(user_key_init);
module_exit(user_key_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
