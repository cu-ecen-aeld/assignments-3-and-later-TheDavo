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

#include "aesd-circular-buffer.h"
#include "aesdchar.h"
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("TheDavo"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp) {
  PDEBUG("opening aesd device driver");

  struct aesd_dev *dev;
  dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
  filp->private_data = dev;
  return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
  PDEBUG("release");
  // since everything in filp->private_data was allocated in the module
  // init function, aesd_release does not have to deallocate anything here
  //
  // if content was allocated in the `open` function, then memory would have
  // to be freed here instead of the module exit function
  return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos) {
  ssize_t retval = 0;
  PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

  struct aesd_dev *dev = filp->private_data;

  if (mutex_lock_interruptible(&dev->dev_mutex)) {
    PDEBUG("aesd_read: failed to lock mutex");
    return -ERESTARTSYS;
  }

  // have the mutex, can now safely send content back to the user

  // continue to send content to the user until the amount sent is the
  // amount requested, or an error occurs
  // get the relevant entry based on *f_pos
  size_t offset_in_entry;
  struct aesd_buffer_entry *entry =
      aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos,
                                                      &offset_in_entry);

  if (NULL == entry) {
    PDEBUG("aesd_read: entry at f_pos returned NULL, buffer may be empty or at "
           "EOF");
    PDEBUG("aesd_read: retval at NULL == entry %lu", retval);
    mutex_unlock(&dev->dev_mutex);
    return retval;
  }

  // have a valid entry, can now start sending content to user

  // send out partial content, from the offset to the end of the current
  // entry
  size_t count_bytes_to_send;
  if (count > entry->size - offset_in_entry) {
    count_bytes_to_send = entry->size - offset_in_entry;
  } else {
    count_bytes_to_send = count;
  }

  // since partial content is being sent, the copy_to_user call must account
  // for that by adding the amount of bytes already sent to the initial
  // user buffer location

  // copy_to_user sends a 0 on success, so anything else is an error
  if (copy_to_user(buf, entry->buffptr + offset_in_entry,
                   count_bytes_to_send)) {
    PDEBUG("aesd_read: error sending content to user");
    retval = -EFAULT;
    mutex_unlock(&dev->dev_mutex);
    return retval;
  }

  // successfully copied to user, update file position and amount of bytes
  // read
  *f_pos += count_bytes_to_send;
  retval = count_bytes_to_send;
  PDEBUG("aesd_read: count %lu", count);
  PDEBUG("aesd_read: count_bytes_to_send %lu", count_bytes_to_send);
  PDEBUG("aesd_read: retval %lu", retval);

  ssize_t sum = 0;
  size_t i;
  for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
    sum += aesd_device.buffer.entry[i].size;
  }

  // no more bytes to read, end of file
  if (*f_pos > sum) {
    retval = 0;
  }

  PDEBUG("aesd_read: retval after EOF check %lu", retval);

  mutex_unlock(&dev->dev_mutex);
  return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos) {
  // in this function a new entry will be created to be added to the
  // circular buffer
  ssize_t retval = -ENOMEM;
  PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
  struct aesd_dev *dev = filp->private_data;

  if (mutex_lock_interruptible(&dev->dev_mutex)) {
    PDEBUG("aesd_read: failed to lock mutex");
    return -ERESTARTSYS;
  }

  void *kbuff = kmalloc(count, GFP_KERNEL);
  if (NULL == kbuff) {
    PDEBUG("aesd_write: error allocating buffer for __user *buf");
    retval = -EFAULT;
    mutex_unlock(&dev->dev_mutex);
    return retval;
  }

  // pointers/memory allocated, can copy from the user now
  if (copy_from_user(kbuff, buf, count)) {
    PDEBUG("aesd_write: error copying from user to kernel allocated buffer");
    retval = -EFAULT;
    mutex_unlock(&dev->dev_mutex);
    return retval;
  }

  // a new buffer entry shall not be placed in to the buffer until a newline
  // character is found, work with dev->working_entry for that case

  size_t offset = 0;

  // if the working entry already has content in memory krealloac has to be
  // used to ask for more memory from the kernel
  if (NULL != dev->working_entry.buffptr) {
    dev->working_entry.buffptr =
        krealloc(dev->working_entry.buffptr, dev->working_entry.size + count,
                 GFP_KERNEL);

    if (NULL == dev->working_entry.buffptr) {
      PDEBUG("aesd_write: krealloc error");
      retval = -EFAULT;
      mutex_unlock(&dev->dev_mutex);
      return retval;
    }

    offset = dev->working_entry.size;
    dev->working_entry.size = dev->working_entry.size + count;
  } else {
    // allocate a new buffer pointer
    dev->working_entry.buffptr = kmalloc(count, GFP_KERNEL);
    dev->working_entry.size = count;

    if (NULL == dev->working_entry.buffptr) {
      PDEBUG("aesd_write: kmalloc error");
      retval = -EFAULT;
      mutex_unlock(&dev->dev_mutex);
      return retval;
    }
  }

  if (NULL == dev->working_entry.buffptr) {
    PDEBUG("aesd_write: working_entry.buffptr NULL before memcpy");
    kfree(kbuff);
    mutex_unlock(&dev->dev_mutex);
    return -EFAULT;
  }

  memcpy((void *)(dev->working_entry.buffptr + offset), kbuff, count);

  // from aesdsocket implementation to find newline character
  char *newline_pos =
      (char *)memchr(dev->working_entry.buffptr, '\n', dev->working_entry.size);
  // add the new entry to the buffer, and free the previous one
  if (NULL != newline_pos) {
    PDEBUG("aesd_write: new entry added to dev->buffer");
    const char *released =
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->working_entry);
    if (NULL != released) {
      PDEBUG(
          "aesd_write: buffer is full, releasing oldest entry at out_offs %d",
          aesd_device.buffer.out_offs);
      kfree(released);
    }

    // reset working_entry for the next write
    dev->working_entry.buffptr = NULL;
    dev->working_entry.size = 0;
  }

  kfree(kbuff);

  mutex_unlock(&dev->dev_mutex);
  retval = count;
  return retval;
}
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev) {
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev", err);
  }
  return err;
}

int aesd_init_module(void) {
  dev_t dev = 0;
  int result;
  result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
  aesd_major = MAJOR(dev);
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  memset(&aesd_device, 0, sizeof(struct aesd_dev));

  aesd_circular_buffer_init(&aesd_device.buffer);
  mutex_init(&aesd_device.dev_mutex);

  result = aesd_setup_cdev(&aesd_device);

  if (result) {
    unregister_chrdev_region(dev, 1);
  }
  return result;
}

void aesd_cleanup_module(void) {
  dev_t devno = MKDEV(aesd_major, aesd_minor);

  cdev_del(&aesd_device.cdev);

  // loop through the circular buffer and free each entry using the macro
  // struct aesd_buffer_entry *entry;
  uint8_t i;
  // AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, i) {
  //   if (NULL != entry->buffptr) {
  //     kfree(entry->buffptr);
  //     entry->buffptr = NULL;
  //     entry->size = 0;
  //   }
  // }

  for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
    if (NULL != aesd_device.buffer.entry[i].buffptr) {
      kfree(aesd_device.buffer.entry[i].buffptr);
      aesd_device.buffer.entry[i].buffptr = NULL;
      aesd_device.buffer.entry[i].buffptr = 0;
    }
  }

  // free the entry used for multiple read/writes
  if (NULL != aesd_device.working_entry.buffptr) {
    kfree(aesd_device.working_entry.buffptr);
    aesd_device.working_entry.buffptr = NULL;
    aesd_device.working_entry.size = 0;
  }

  mutex_destroy(&aesd_device.dev_mutex);

  unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
