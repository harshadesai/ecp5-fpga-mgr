#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>

#include <linux/spi/spi.h>

#include "lattice/SSPIEm.h"

struct ecp5
{
	struct spi_device *spi;
	int programming_result;

	int algo_size;
	unsigned char *algo_mem;
	struct mutex algo_lock;
	struct miscdevice algo_char_device;

	int data_size;
	unsigned char *data_mem;
	struct mutex data_lock;
	struct miscdevice data_char_device;
};

static DEFINE_MUTEX(programming_lock);
struct spi_device *current_programming_ecp5;

/*
 * File operations
 */
int ecp5_sspi_algo_open(struct inode *inode, struct file *fp)
{
	struct ecp5 *ecp5_info = container_of(fp->private_data, struct ecp5, algo_char_device);

	if (!mutex_trylock(&ecp5_info->algo_lock))
	{
		pr_err("ECP5: trying to open algo device while it already locked");
		return(-EBUSY);
	}

	fp->private_data = ecp5_info;

	return (0);
}


int ecp5_sspi_algo_release(struct inode *inode, struct file *fp)
{
	struct ecp5 *ecp5_info = fp->private_data;

	mutex_unlock(&ecp5_info->algo_lock);

	return (0);
}

int ecp5_sspi_data_open(struct inode *inode, struct file *fp)
{
	struct ecp5 *ecp5_info = container_of(fp->private_data, struct ecp5, data_char_device);

	if (!mutex_trylock(&ecp5_info->data_lock))
	{
		pr_err("ECP5: trying to open data device while it already locked");
		return(-EBUSY);
	}

	fp->private_data = ecp5_info;

	return (0);
}


int ecp5_sspi_data_release(struct inode *inode, struct file *fp)
{
	struct ecp5 *ecp5_info = fp->private_data;

	mutex_unlock(&ecp5_info->data_lock);

	return (0);
}

ssize_t ecp5_sspi_algo_read(struct file *fp, char __user *ubuf, size_t len,
		loff_t *offp)
{
	struct ecp5 *ecp5_info = fp->private_data;

	if (*offp > ecp5_info->algo_size)
		return (0);

	if (*offp + len > ecp5_info->algo_size)
		len = ecp5_info->algo_size - *offp;

	if (copy_to_user(ubuf, ecp5_info->algo_mem + *offp, len) != 0 )
	        return (-EFAULT);

	*offp += len;

	return (len);
}

ssize_t ecp5_sspi_data_read(struct file *fp, char __user *ubuf, size_t len,
		loff_t *offp)
{
	struct ecp5 *ecp5_info = fp->private_data;

	if (*offp > ecp5_info->data_size)
		return (0);

	if (*offp + len > ecp5_info->data_size)
		len = ecp5_info->data_size - *offp;

	if (copy_to_user(ubuf, ecp5_info->data_mem + *offp, len) != 0 )
	        return (-EFAULT);

	*offp += len;

	return (len);
}

ssize_t ecp5_sspi_algo_write(struct file *fp, const char __user *ubuf, size_t len,
		loff_t *offp)
{
	struct ecp5 *ecp5_info = fp->private_data;
	ssize_t size = max_t(ssize_t, len + *offp, ecp5_info->algo_size);

	ecp5_info->algo_size = len + *offp;
	ecp5_info->algo_mem = krealloc(ecp5_info->algo_mem, size, GFP_KERNEL);
	if (!ecp5_info->algo_mem)
	{
		pr_err("ECP5: can't allocate enough memory\n");
		return (-EFAULT);
	}

	if (!mutex_trylock(&programming_lock))
	{
		pr_err("ECP5: can't write to algo device while programming");
		return(-EBUSY);
	}

	if (copy_from_user(ecp5_info->algo_mem + *offp, ubuf, len) != 0)
		return (-EFAULT);

	mutex_unlock(&programming_lock);

	ecp5_info->algo_size = size;
	*offp += len;

	return (len);
}

ssize_t ecp5_sspi_data_write(struct file *fp, const char __user *ubuf, size_t len,
		loff_t *offp)
{
	struct ecp5 *ecp5_info = fp->private_data;
	ssize_t size = max_t(ssize_t, len + *offp, ecp5_info->data_size);

	ecp5_info->data_mem = krealloc(ecp5_info->data_mem, size, GFP_KERNEL);
	if (!ecp5_info->data_mem)
	{
		pr_err("ECP5: can't allocate enough memory\n");
		return (-EFAULT);
	}

	if (!mutex_trylock(&programming_lock))
	{
		pr_err("ECP5: can't write to data device while programming");
		return(-EBUSY);
	}

	if (copy_from_user(ecp5_info->data_mem + *offp, ubuf, len) != 0)
		return (-EFAULT);

	mutex_unlock(&programming_lock);


	ecp5_info->data_size = size;
	*offp += len;

	return (len);
}

loff_t ecp5_sspi_algo_lseek(struct file *fp, loff_t off, int whence)
{
	struct ecp5 *ecp5_info = fp->private_data;

        loff_t newpos;
        uint32_t size;

        size = ecp5_info->algo_size;

        switch(whence) {

        case SEEK_SET:
                newpos = off;
                break;

        case SEEK_CUR:
                newpos = fp->f_pos + off;
                break;

        case SEEK_END:
                newpos = size + off;
                break;

        default:
                return (-EINVAL);
        }

        if (newpos < 0)
                return (-EINVAL);

        if (newpos > size)
                newpos = size;

        fp->f_pos = newpos;

        return (newpos);
}

loff_t ecp5_sspi_data_lseek(struct file *fp, loff_t off, int whence)
{
	struct ecp5 *ecp5_info = fp->private_data;

        loff_t newpos;
        uint32_t size;

        size = ecp5_info->data_size;

        switch(whence) {

        case SEEK_SET:
                newpos = off;
                break;

        case SEEK_CUR:
                newpos = fp->f_pos + off;
                break;

        case SEEK_END:
                newpos = size + off;
                break;

        default:
                return (-EINVAL);
        }

        if (newpos < 0)
                return (-EINVAL);

        if (newpos > size)
                newpos = size;

        fp->f_pos = newpos;

        return (newpos);
}

struct file_operations algo_fops = {
	.owner = THIS_MODULE,
	.read = ecp5_sspi_algo_read,
	.write = ecp5_sspi_algo_write,
	.open = ecp5_sspi_algo_open,
	.release = ecp5_sspi_algo_release,
	.llseek = ecp5_sspi_algo_lseek,
};

struct file_operations data_fops = {
	.owner = THIS_MODULE,
	.read = ecp5_sspi_data_read,
	.write = ecp5_sspi_data_write,
	.open = ecp5_sspi_data_open,
	.release = ecp5_sspi_data_release,
	.llseek = ecp5_sspi_data_lseek,
};


ssize_t algo_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ecp5 *dev_info = dev_get_drvdata(dev);
	return (sprintf(buf, "%d\n", dev_info->algo_size));
}

static ssize_t algo_size_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return (count);
}

ssize_t data_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ecp5 *dev_info = dev_get_drvdata(dev);
	return (sprintf(buf, "%d\n", dev_info->data_size));
}

static ssize_t data_size_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return (count);
}

ssize_t program_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ecp5 *dev_info = dev_get_drvdata(dev);
	return (sprintf(buf, "%d\n", dev_info->programming_result));
}

static ssize_t program_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ecp5 *dev_info = dev_get_drvdata(dev);

	if (!mutex_trylock(&programming_lock))
	{
		pr_warn("ECP5: can't lock programming mutex \n");
		pr_info("ECP5: (maybe someone already programming your chip?)");
		return (count);
	}

	current_programming_ecp5 = dev_info->spi;

	if (dev_info->spi != to_spi_device(dev)) {
		pr_err("ECP5: Mystical error occurred\n");
	}

	/* here we call lattice programming code */
	/* 1 - preparing data*/
	dev_info->programming_result = SSPIEm_preset(dev_info->algo_mem,
			dev_info->algo_size, dev_info->data_mem,
			dev_info->data_size);
	pr_debug("ECP5: SSPIEm_preset result %d\n",
					dev_info->programming_result);
	/* 2 - programming here */
	dev_info->programming_result = SSPIEm(0xFFFFFFFF);

	mutex_unlock(&programming_lock);

	if (dev_info->programming_result != 2)
		pr_err("ECP5: FPGA programming failed with code %d\n",
				dev_info->programming_result);
	else
		pr_info("ECP5: FPGA programming success\n");

	return (count);
}

struct device_attribute ecp5_algo_size_attr =
__ATTR(algo_size, 0666, algo_size_show, algo_size_store);

struct device_attribute ecp5_data_size_attr =
__ATTR(data_size, 0666, data_size_show, data_size_store);

struct device_attribute ecp5_program_attr =
__ATTR(program, 0666, program_show, program_store);

struct attribute *ecp5_attrs[] = {
	&ecp5_algo_size_attr.attr,
	&ecp5_data_size_attr.attr,
	&ecp5_program_attr.attr,
	NULL,
};

struct attribute_group ecp5_attr_group = {
	.attrs = ecp5_attrs,
};

static int __devinit ecp5_probe(struct spi_device *spi)
{
	int ret;
	struct ecp5 *ecp5_info = NULL;
	unsigned char *algo_cdev_name = NULL;
	unsigned char *data_cdev_name = NULL;

	pr_info("ECP5: device spi%d.%d probing\n", spi->master->bus_num, spi->chip_select);

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = 30000000;
	ret = spi_setup(spi);
	if (ret < 0)
		return (ret);

	ecp5_info = devm_kzalloc(&spi->dev, sizeof(*ecp5_info), GFP_KERNEL);
	if (!ecp5_info)
		return (-ENOMEM);

	spi_set_drvdata(spi, ecp5_info);
	ecp5_info->spi = spi;
	ecp5_info->programming_result = 0;

	ecp5_info->algo_char_device.minor = MISC_DYNAMIC_MINOR;
	algo_cdev_name = kzalloc(64, GFP_KERNEL);
	if (!algo_cdev_name) return (-ENOMEM);
	sprintf(algo_cdev_name, "ecp5-spi%d.%d-algo", spi->master->bus_num, spi->chip_select);
	ecp5_info->algo_char_device.name = algo_cdev_name;
	ecp5_info->algo_char_device.fops = &algo_fops;
	ret = misc_register(&ecp5_info->algo_char_device);
	if (ret) {
		pr_err("ECP5: can't register firmware algo image device\n");
		goto error_return;
	}
	mutex_init(&ecp5_info->algo_lock);

	ecp5_info->data_char_device.minor = MISC_DYNAMIC_MINOR;
	data_cdev_name = kzalloc(64, GFP_KERNEL);
	if (!data_cdev_name) return (-ENOMEM);
	sprintf(data_cdev_name, "ecp5-spi%d.%d-data", spi->master->bus_num, spi->chip_select);
	ecp5_info->data_char_device.name = data_cdev_name;
	ecp5_info->data_char_device.fops = &data_fops;
	ret = misc_register(&ecp5_info->data_char_device);
	if (ret) {
		pr_err("ECP5: can't register firmware data image device\n");
		goto error_return;
	}
	mutex_init(&ecp5_info->data_lock);

	ret = sysfs_create_group(&spi->dev.kobj, &ecp5_attr_group);
	if (ret)
	{
		pr_err("ECP5: failed to create attribute files\n");
		goto error_return;
	}

	pr_info("ECP5: device spi%d.%d probed\n", spi->master->bus_num, spi->chip_select);

	return (0);

error_return:
	kzfree(algo_cdev_name);
	kzfree(data_cdev_name);
	return (-ENOMEM);
}


static int __devexit ecp5_remove(struct spi_device *spi)
{
	int err_code;
	struct ecp5 *ecp5_info = spi_get_drvdata(spi);

	pr_info("ECP5: device spi%d.%d removing\n", spi->master->bus_num, spi->chip_select);

	err_code = misc_deregister(&ecp5_info->algo_char_device);
	if(err_code) {
		pr_err("ECP5: can't unregister firmware image device\n");
		return (err_code);
	}
	kzfree(ecp5_info->algo_char_device.name);
	mutex_destroy(&ecp5_info->algo_lock);

	err_code = misc_deregister(&ecp5_info->data_char_device);
	if(err_code) {
		pr_err("ECP5: can't unregister firmware image device\n");
		return (err_code);
	}
	kzfree(ecp5_info->data_char_device.name);
	mutex_destroy(&ecp5_info->data_lock);

	sysfs_remove_group(&spi->dev.kobj, &ecp5_attr_group);

	kzfree(ecp5_info->algo_mem);
	kzfree(ecp5_info->data_mem);

	pr_info("ECP5: device spi%d.%d removed\n", spi->master->bus_num, spi->chip_select);
	return (0);
}

static const struct spi_device_id ecp5_ids[] = {
	{"ecp5-device"},
	{ },
};
MODULE_DEVICE_TABLE(spi, ecp5_ids);

static struct spi_driver ecp5_driver = {
	.driver = {
		.name	= "ecp5-driver",
		.owner	= THIS_MODULE,
	},
	.id_table	= ecp5_ids,
	.probe	= ecp5_probe,
	//.remove	= ecp5_remove,
	.remove	= __devexit_p(ecp5_remove),
};

module_spi_driver(ecp5_driver);


/*
 * device initialization
 */
static int __init ecp5_sspi_init(void)
{
	int err_code;

	pr_info("ECP5: driver initialization\n");

	err_code = spi_register_driver(&ecp5_driver);
	if (err_code) {
		pr_err("can't register spi driver\n");
		return (err_code);
	}

	pr_info("ECP5: driver successfully inited\n");
	return (0);
}

static void __exit ecp5_sspi_exit(void)
{

	pr_info("ECP5: driver exiting\n");

	spi_unregister_driver(&ecp5_driver);

	pr_info("ECP5: driver successfully exited\n");
}

module_init(ecp5_sspi_init);
module_exit(ecp5_sspi_exit);

MODULE_AUTHOR("STC Metrotek");
MODULE_DESCRIPTION("Lattice ECP5 FPGA Slave SPI programming interface driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ecp5_sspi");
