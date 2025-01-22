// simple.c -- a simple module

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
 
#include <linux/kernel.h>       // printk() 
#include <linux/slab.h>         // kmalloc() 
#include <linux/fs.h>           // everything... 
#include <linux/errno.h>        // error codes 
#include <linux/types.h>        // size_t 
#include <linux/proc_fs.h>
#include <linux/fcntl.h>        // O_ACCMODE 
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>

//#include <asm/system.h>         // cli(), *_flags 
#include <asm/uaccess.h>        // copy_*_user 
 
// Our parameters which can be set at load time.
int simple_major =   0;
int simple_minor =   0;
int memsize = 255;

module_param(simple_major, int, S_IRUGO);
module_param(simple_minor, int, S_IRUGO);
module_param(memsize, int, S_IRUGO);

MODULE_AUTHOR("Elisa Martinenghi");
MODULE_LICENSE("Dual BSD/GPL");


struct simple_dev {
    char *data;  // Pointer to data area
    int memsize;
    struct semaphore sem;     // mutual exclusion semaphore    
    struct cdev cdev;         // structure for char devices 
};

struct simple_dev simple_device; 

int simple_open(struct inode *inode, struct file *filp){
    struct simple_dev *dev;     // a pointer to a simple_dev structure 
 
    dev = container_of(inode->i_cdev, struct simple_dev, cdev);
    filp->private_data = dev; // stored here to be re-used in other system call
 
    return 0;           
}


int simple_release(struct inode *inode, struct file *filp){
    return 0;
}


ssize_t simple_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){
    struct simple_dev *dev = filp->private_data;
    ssize_t retval = 0; // return value 

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (count >= dev->memsize) 
        count = dev->memsize;
    
    if (copy_from_user(dev->data, buf, count)) {
        retval = -EFAULT;
        goto out;
    }

    dev->data[count] = '\0';
    printk("%s", dev->data);
    retval = count; 
    
    out:
        up(&dev->sem);
        return retval;
}

struct file_operations simple_fops = {
    .owner =    THIS_MODULE,
    .write =    simple_write,
    .open =     simple_open,
    .release =  simple_release,
};

void simple_cleanup_module(void){
    dev_t devno = MKDEV(simple_major, simple_minor);
 
    // Free the cdev entries  
    cdev_del(&simple_device.cdev);

    // Free the memory 
    kfree(simple_device.data);

    unregister_chrdev_region(devno, 1);
}

int simple_init_module(void){
    int result, err;
    dev_t dev = 0;
 
    if (simple_major){   //the major number is given as a parameter
        dev = MKDEV(simple_major, simple_minor);
        result = register_chrdev_region(dev, 1, "simple");
    } else {        // otherwise
        result = alloc_chrdev_region(&dev, simple_minor, 1, "simple");
        simple_major = MAJOR(dev);
    }
    
    if (result < 0){
        printk(KERN_WARNING "simple: can't get major %d\n", simple_major);
        return result;
    }

    // Prepare the memory area 
    simple_device.memsize = memsize;
    simple_device.data = kmalloc(memsize * sizeof(char), GFP_KERNEL);
    memset(simple_device.data, 0, memsize * sizeof(char));

    // Initialize the semaphore 
    sema_init(&simple_device.sem,1);

    // Initialize cdev 
    cdev_init(&simple_device.cdev, &simple_fops);
    simple_device.cdev.owner = THIS_MODULE;
    simple_device.cdev.ops = &simple_fops;
    err = cdev_add (&simple_device.cdev, dev, 1);

    if (err)  printk(KERN_NOTICE "Error %d adding simple", err);
    else  printk(KERN_NOTICE "Simple Added major: %d minor: %d", simple_major, simple_minor);
    
    return 0; 
}


module_init(simple_init_module);
module_exit(simple_cleanup_module);


