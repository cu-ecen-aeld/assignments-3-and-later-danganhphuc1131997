/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>       /* file_operations */
#include <linux/errno.h>	/* error codes */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/uaccess.h>	/* copy_*_user */
#include <linux/mutex.h>
#include "aesd-circular-buffer.h"
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;
MODULE_AUTHOR("Phuc Dang");
MODULE_LICENSE("Dual BSD/GPL");
struct aesd_dev aesd_device;
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    // Set filp-> private_data with our aesd_dev device struct (use inode-> i_cdev with container_of)
    struct aesd_dev *dev;     /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */
    return 0;
}
int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    size_t entry_offset;
    size_t bytes_remaining = count;
    size_t bytes_read = 0;
    struct aesd_buffer_entry *aesd_entry;
    size_t char_offset = *f_pos;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    if (mutex_lock_interruptible(&dev->lock)) return -ERESTARTSYS;
    /**
     * TODO: handle read
     */
    while (bytes_remaining > 0) {
        aesd_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buffer,
                                                                     char_offset,
                                                                     &entry_offset);
        if (!aesd_entry) {
          // No more data to read, EOF
          break;
        }
        // Calculate current data from current entry, get the data size we can copy
        size_t bytes_available = aesd_entry->size - entry_offset;
        size_t bytes_to_copy = min(bytes_available, bytes_remaining);
        // Copy data to use space
        if (copy_to_user(buf + bytes_read, aesd_entry->buffptr + entry_offset, bytes_to_copy)) {
          // Copy failed
          PDEBUG("Copy to user error %d \r\n", retval);
          goto out;
        }
        // Update data
        bytes_read += bytes_to_copy;
        bytes_remaining -= bytes_to_copy;
        char_offset += bytes_to_copy;
        if (bytes_remaining == 0) break;
        PDEBUG("String =  %s \r\n", aesd_entry->buffptr);
    }
    *f_pos += bytes_read;
    retval = bytes_read;
    out:
	    mutex_unlock(&dev->lock);
	    return retval;
}
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *kernel_buf = NULL;
    // Do not use f_pos for write in this assignment
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
        
    // Save user_buffer to kernel buff 
    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (!kernel_buf) {
       retval = -ENOMEM;
    goto out;
    }
    if (copy_from_user(kernel_buf, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    if (aesd_device.entry.buffptr == NULL) {
        // No data in buffer now, copy user data directly
        aesd_device.entry.buffptr = kernel_buf;
        aesd_device.entry.size = count;
    } else {
        // Data already in entry data, just resize and add new data after existed data
        aesd_device.entry.buffptr = krealloc(aesd_device.entry.buffptr, aesd_device.entry.size + count, GFP_KERNEL);
        // Save the last entry data
        memcpy(aesd_device.entry.buffptr + aesd_device.entry.size, kernel_buf, count);
        aesd_device.entry.size = aesd_device.entry.size + count;
        kfree(kernel_buf);
    }

    if (memchr(aesd_device.entry.buffptr, '\n', aesd_device.entry.size) != NULL) {
        // Newline character found, add entry to circular buffer
        const char *replaced_entry = aesd_circular_buffer_add_entry(&aesd_device.circular_buffer, &aesd_device.entry);
        if (replaced_entry != NULL) {
            kfree(replaced_entry);
        }
        PDEBUG("Add string %s to circular buffer\r\n", aesd_device.entry.buffptr);
        // Reset entry
        aesd_device.entry.buffptr = NULL;
        aesd_device.entry.size = 0;
    } else {
        PDEBUG("No newline found \r\n");
    }
    retval = count;
    out:
        mutex_unlock(&dev->lock);
        return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};
static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);
    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}
int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    PDEBUG("Initialize aesdchar \r\n");
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));
    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    aesd_device.entry.buffptr = NULL;
    mutex_init(&aesd_device.lock);
    result = aesd_setup_cdev(&aesd_device);
    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}
void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    cdev_del(&aesd_device.cdev);
    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    unregister_chrdev_region(devno, 1);
}
module_init(aesd_init_module);
module_exit(aesd_cleanup_module);