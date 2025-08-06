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
#include "aesd_ioctl.h"

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
        aesd_device.entry.size += count;
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

    // Update f_pos
    filp->f_pos += count;

    out:
        mutex_unlock(&dev->lock);
        return retval;
}

loff_t aesd_llseek (struct file *filp, loff_t offset, int whence)
{
    // PhucDA test
    // TODO: Add your own llseek function, with locking and logging, but use fixed_size_llseek for logic
    //   llseek implementation for fixed_sized devices
    // @file: file structure to seek on
    // @offset: file offset to seek to
    // @whence: type of seek (SEEK_SET, SEEK_CUR, SEEK_END)
    // @size: Size of the file
    // @return: Offset

    uint8_t index;
    loff_t circular_buffer_size = 0;  // Save total circular buffer size
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock)) return -ERESTARTSYS;
    // Get the circular buffer size
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr != NULL) {
            circular_buffer_size += entry->size;
        }
    }
    PDEBUG("Total circular buffer size = %d \r\n", circular_buffer_size);
    off_t ret_offset = fixed_size_llseek(filp, offset, whence, circular_buffer_size);
    if (ret_offset < 0) {
        ret_offset = -EINVAL;
    } else {
        filp->f_pos = ret_offset;
    }
    out:
        mutex_unlock(&dev->lock);
        return ret_offset;
}

/**
 * Adjust the file offset (f_pos) parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced command to locate)
 * and @param write_cmd_offset (the zero referenced offset into the command)
 * @return 0 if successful, negative if error occurred:
 * -ERESTARTSYS if mutex could not be obtained
 * -EINVAL if write command or write_cmd_offset was out of range
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    int retval = 0;
    size_t file_offset;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("Request command ioctl %d \r\n", write_cmd);
    PDEBUG("Request command offset ioctl %d \r\n", write_cmd_offset);

    if (mutex_lock_interruptible(&dev->lock)) return -ERESTARTSYS;
    // Out of range write command
    if (write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        retval = -EINVAL;
        goto out;
    }

    // Check if command is written before
    if (aesd_device.circular_buffer.entry[write_cmd].buffptr == NULL) {
        retval = -EINVAL;
        goto out;
    }

    // Check if command is written before
    if (aesd_device.circular_buffer.entry[write_cmd].size <= write_cmd_offset) {
        retval = -EINVAL;
        goto out;
    }

    /* Update the file pointer */
    for (uint8_t index = 0; index < (write_cmd); index++)
    {
        file_offset += aesd_device.circular_buffer.entry[index].size;
    }
    filp->f_pos = file_offset + write_cmd_offset;

    out:
        mutex_unlock(&dev->lock);
        return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, tmp;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESD_IOC_MAGIC) return -ENOTTY;

    /*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	// if (_IOC_DIR(cmd) & _IOC_READ)
	// 	err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	// else if (_IOC_DIR(cmd) & _IOC_WRITE)
	// 	err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	// if (err) return -EFAULT;

	switch(cmd) {
	    case AESDCHAR_IOCSEEKTO:
            PDEBUG("Command SEEKTO Received ! \r\n");
            struct aesd_seekto seekto;
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0) {
                retval = EFAULT;
            } else {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            }
		    break;
        default:
		    return -ENOTTY;
    }
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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