/**
 * @file at24.c
 * @brief AT24 EEPROM I2C Driver Implementation.
 * @author Ali Nasrolahi <A.Nasrolahi01@gmail.com>
 * @note Compiled for kernel 6.6 and tested on
 * Raspberry Pi 5 Yocto core-image-minimal distro.
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/cdev.h>
#include <linux/delay.h> /* msleep() */
#include <linux/fs.h> /* File operations */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#define AT24_MAX_DEVICES 32
#define AT24_OF_COMPAT_ID 0xcafe
#define AT24_OF_COMPAT_DEVICE "zephyr,eeprom_driver"
#define AT24_I2C_WRITE_DELAY (8) /* Delay for write operations to complete */

struct at24_device {
	struct cdev cdev;
	struct i2c_client *client;
	struct device *device;
	u32 size_in_bytes;
};

/* Global Driver Data */
static int major_number;
static atomic_t device_count = ATOMIC_INIT(0);
static struct class *at24_class;

static const struct of_device_id at24_of_match[] = {
	{ .compatible = AT24_OF_COMPAT_DEVICE,
	  .data = (void *)AT24_OF_COMPAT_ID },
	{}
};

MODULE_DEVICE_TABLE(of, at24_of_match);

/* File Operations --- Begin */
static int at24_open(struct inode *inode, struct file *file)
{
	struct at24_device *ldev;

	ldev = container_of(inode->i_cdev, struct at24_device, cdev);

	if (!ldev || !ldev->client)
		return pr_err("Invalid open, no client specified!"), -EINVAL;

	file->private_data = ldev;

	dev_info(&ldev->client->dev, "Client 0x%x opened!\n",
		 ldev->client->addr);

	return 0;
}

static int at24_release(struct inode *inode, struct file *file)
{
	struct at24_device *ldev =
		container_of(inode->i_cdev, struct at24_device, cdev);

	dev_info(&ldev->client->dev, "Client 0x%x released!\n",
		 ldev->client->addr);

	return 0;
}

static loff_t at24_llseek(struct file *file, loff_t off, int whence)
{
	struct at24_device *ldev = file->private_data;
	loff_t newpos;

	switch (whence) {
	case SEEK_SET:
		newpos = off;
		break;
	case SEEK_CUR:
		newpos = file->f_pos + off;
		break;
	case SEEK_END:
		newpos = ldev->size_in_bytes + off;
		break;
	default:
		return -EINVAL;
	}

	if (newpos < 0 || newpos > ldev->size_in_bytes)
		return -EINVAL;

	return file->f_pos = newpos;
}

static ssize_t at24_read(struct file *file, char __user *buffer, size_t length,
			 loff_t *offset)
{
	struct at24_device *ldev = file->private_data;

	if (*offset >= ldev->size_in_bytes - 1 /* eof */)
		return 0;

	length = min((loff_t)ldev->size_in_bytes - *offset -
			     1 /* max allowable len */,
		     (loff_t)length);

	for (int i = 0, byte_from_device; i < length; ++i) {
		byte_from_device =
			i2c_smbus_read_byte_data(ldev->client, (u8)*offset + i);

		if (byte_from_device < 0)
			return dev_dbg(&ldev->client->dev,
				       "r failure: off: 0x%x\n",
				       (u8)*offset + i),
			       byte_from_device;

		if (put_user((u8)byte_from_device, &buffer[i]) < 0)
			return -EFAULT;
	}

	*offset += length;
	return length;
}

static ssize_t at24_write(struct file *file, const char __user *buffer,
			  size_t length, loff_t *offset)
{
	struct at24_device *ldev = file->private_data;

	if (*offset >= ldev->size_in_bytes - 1 /* eof */)
		return 0;

	length = min((loff_t)ldev->size_in_bytes - *offset -
			     1 /* max allowable len */,
		     (loff_t)length);

	u8 byte_send_to_device;
	for (int i = 0; i < length; ++i, msleep(AT24_I2C_WRITE_DELAY)) {
		if (get_user(byte_send_to_device, &buffer[i]) < 0)
			return -EFAULT;

		int ret = i2c_smbus_write_byte_data(
			ldev->client, (u8)*offset + i, byte_send_to_device);
		if (ret < 0)
			return dev_dbg(&ldev->client->dev,
				       "w failure, off: 0x%x\n",
				       (u8)*offset + i),
			       ret;
	}

	*offset += length;
	return length;
}

static struct file_operations at24_fops = {
	.owner = THIS_MODULE,
	.open = at24_open,
	.release = at24_release,
	.read = at24_read,
	.write = at24_write,
	.llseek = at24_llseek,
};

/* File Operations --- End */

static int at24_probe(struct i2c_client *c)
{
	struct device *dev = &c->dev;

	/* 1. AT24 Device Creation */
	struct at24_device *at24_device =
		devm_kzalloc(dev, sizeof(struct at24_device), GFP_KERNEL);

	if (!at24_device)
		return -ENOMEM;

	at24_device->client = c;
	i2c_set_clientdata(c, at24_device);

	/* 2. Check for required parameters & bus functions */
	if (device_property_read_u32(&c->dev, "size",
				     &at24_device->size_in_bytes))
		return dev_err(dev,
			       "Driver needs 'size' property to be specified!\n"),
		       -EINVAL;

	if (!i2c_check_functionality(c->adapter, I2C_FUNC_I2C))
		return dev_err(dev, "Controller does not support I2C!\n"),
		       -ENODEV;

	/* 3. Character Device preparations */
	int minor = atomic_fetch_inc(&device_count);
	dev_t devno = MKDEV(major_number, minor);

	at24_device->device =
		device_create(at24_class, dev, devno, NULL, "eeprom%d", minor);
	if (IS_ERR(at24_device->device)) {
		atomic_dec(&device_count);
		dev_err(dev, "Failed to create device\n");
		return PTR_ERR(at24_device->device);
	}

	cdev_init(&at24_device->cdev, &at24_fops);
	int ret = cdev_add(&at24_device->cdev, devno, 1);
	if (ret < 0) {
		device_destroy(at24_class, devno);
		atomic_dec(&device_count);
		return dev_err(&c->dev, "Failed to add cdev\n"), ret;
	}

	dev_info(&c->dev, "Client with addr 0x%x probed!\n", c->addr);
	return 0;
}

static void at24_remove(struct i2c_client *c)
{
	struct at24_device *at24_device = i2c_get_clientdata(c);
	dev_t devno = at24_device->cdev.dev;
	cdev_del(&at24_device->cdev);
	device_destroy(at24_class, devno);
	atomic_dec(&device_count);
	dev_info(&c->dev, "Client with addr 0x%x removed!\n", c->addr);
}

static struct i2c_driver at24_driver = {
    .probe = at24_probe,
    .remove = at24_remove,
    .driver = {
        .name = "at24",
        .of_match_table = at24_of_match,
		.owner = THIS_MODULE,
    },
};

static int __init at24_init(void)
{
	int ret;
	dev_t dev;

	ret = alloc_chrdev_region(&dev, 0, AT24_MAX_DEVICES, "eeprom");
	if (ret < 0) {
		pr_err("Failed to allocate chr region\n");
		return ret;
	}

	major_number = MAJOR(dev);

	at24_class = class_create("at24_class");
	if (IS_ERR(at24_class)) {
		pr_err("Failed to create device class\n");
		ret = PTR_ERR(at24_class);
		goto fail_class;
	}

	ret = i2c_add_driver(&at24_driver);
	if (ret < 0) {
		pr_err("Failed to register platform driver\n");
		goto fail_platform;
	}

	pr_info("Driver loaded successfully with major number %d\n",
		major_number);

	return 0;

fail_platform:
	class_destroy(at24_class);
fail_class:
	unregister_chrdev_region(dev, AT24_MAX_DEVICES);
	return ret;
}

static void __exit at24_exit(void)
{
	i2c_del_driver(&at24_driver);
	class_destroy(at24_class);
	unregister_chrdev_region(MKDEV(major_number, 0), AT24_MAX_DEVICES);

	pr_info("Driver unloaded\n");
}

module_init(at24_init);
module_exit(at24_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ali Nasrolahi <A.Nasrolahi01@gmail.com>");
MODULE_DESCRIPTION("AT24 EEPROM I2C Device Driver");
MODULE_VERSION("0.1");

