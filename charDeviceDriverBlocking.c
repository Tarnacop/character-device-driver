/**
 * @file charDeviceDriver.c
 * @author Alexandru Blinda
 * @date 6 November 2017
 * @version 0.1
 * @brief C file that defines methods declared in .h file.
 * Part of exercise 3 for Operating Systems Module (creating a character device
 * driver).
 */
#include <linux/init.h> /* Macros used to mark up functions like __init and __exit */
#include <linux/module.h> /* Core headers for loading LKMs into the kernel */
#include <linux/device.h> /* Headr to support the kernel driver model */
#include <linux/kernel.h> /* Functions, types, macros for the kernel */
#include <linux/fs.h> /* Linux file support */
#include <asm/uaccess.h> /* Copy to / from user space */
#include <linux/slab.h> /* Header for kmalloc and kfree functions */
#include <linux/string.h> /* For memcpy, memset, sprintf */
#include <linux/mutex.h> /* Required for mutex functionality */
#include <linux/wait.h>  /* Required for using wait queue */
#include <linux/sched.h>

#include "charDeviceDriver.h"

/* LKM description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexandru Blinda");
MODULE_DESCRIPTION("A device driver for a character device which implements a simple way of message passing");
MODULE_VERSION("0.1");

static struct message_queue* queuep;
static DEFINE_MUTEX(queue_lock); /* Declare a mutex to be used for accessing queue */
DECLARE_WAIT_QUEUE_HEAD(read_wq); /* Used to block the reader until a message is available */
DECLARE_WAIT_QUEUE_HEAD(write_wq); /* Used to block the writer until there is room for his message */

/*
 * This function is called when the module is loaded
 * Static so it can be used only in this C file
 */
static int __init char_device_driver_init(void) {

    printk(KERN_INFO "%s: Initialising the %s Loadable Kernel Module\n", PRINTING_NAME, PRINTING_NAME);

    /* Try to dinamically obtain a major number from the kernel */
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if(major_number < 0) {

        printk(KERN_ALERT "%s: Registering character device failed with %d\n", PRINTING_NAME, major_number);
        return major_number;
    }
    printk(KERN_INFO "%s: Character device registered with major number %d\n", PRINTING_NAME, major_number);

    printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, major_number);

    mutex_init(&queue_lock);

    queuep = initialise_queue(); /* Initialise the globally declared queue */
    /* If queuep could not be allocated, handle the error */
    if(queuep == NULL) {

        printk(KERN_ALERT "%s: Failed to allocate memory for queue\n", PRINTING_NAME);
        mutex_destroy(&queue_lock);
        unregister_chrdev(major_number, DEVICE_NAME);
        return -EFAULT;
    }

    return SUCCESS;
}

/*
 * Function called when module is unloaded from kernel
 */
static void __exit char_device_driver_exit(void) {

    release_queue(queuep); /* Free the resources of the globally declared queue */
    queuep = NULL;
    mutex_destroy(&queue_lock);
    unregister_chrdev(major_number, DEVICE_NAME); /* Unregister the major number */
    printk(KERN_INFO "%s: Device driver resources cleaned up\n", PRINTING_NAME);
}

/* Handles process opening the device */
static int device_open(struct inode* inodep, struct file* filep) {

    /*
     * Increment the number of processes using this device
     * Useful so we do not remove the driver when it is in use
     */
    if(try_module_get(THIS_MODULE) == 0) {

        return -EAGAIN;
    }
    return SUCCESS;
}

/* Handles process reading from device */
static ssize_t device_read(struct file* filep, char* buffer, size_t length, loff_t* offset) {

    /*
     * We try and dequeue the queue.
     * If tmp_data is NULL (no element in queuep), we put the process to sleep until tmp_data +
     */

    printk(KERN_INFO "%s: Request to read %zu bytes received.\n", PRINTING_NAME, length);
    wait_event(read_wq, is_queue_empty(queuep) == 0);

    struct message_queue_data* tmp_data = dequeue(queuep);
    wake_up(&write_wq);
    char* tmp_message = tmp_data->message;
    int bytes_read = 0;

    /* Ensures we send to the user the specific message */
    unsigned short tmp_length = 0;
    if(tmp_data->message_size >= length) {
        
        tmp_length = length;
    } else {
        
        tmp_length = tmp_data->message_size;
    }

    /* As long as we did not hit null byte and length is not 0 */
    while(tmp_length && *tmp_message) {

        /* Move the message from kernel space to user space */
        if(put_user(*(tmp_message++), buffer++) != SUCCESS) {

            kfree(tmp_data->message);
            kfree(tmp_data);
            return -EFAULT;
        }

        tmp_length--;
        bytes_read++;
    }

    /* Clean data */
    kfree(tmp_data->message);
    kfree(tmp_data);
    return bytes_read;
}

/* Handles process writing to device */
static ssize_t device_write(struct file* filep, const char* buffer, size_t length, loff_t* offset) {

    /* If the length of the message to be written is bigger than 4KB, return EINVAL */

    printk(KERN_INFO "%s: Request to write %zu bytes received.\n", PRINTING_NAME, length);
    if(length > MAX_MESSAGE_SIZE) {

        return -EINVAL;
    }

    /* If after enqueuing this message, the size of all the messages is bigger than the size defined, EAGAIN */
    wait_event(write_wq, is_space_in_queue(queuep, length) == 1);

    /* Local copy of the message */
    char tmp_message[length];
    memset(tmp_message, '\0', length);

    /* Read the characters 1 by 1 until we reach the size */

    int i;
    for(i=0; i < length; i++) {

	    if(get_user(tmp_message[i], buffer+i) != SUCCESS) {

            return -EFAULT;
        }
    }

    /* If everything is fine, just continue enqueuing the message */
    if(enqueue(queuep, tmp_message, length) != SUCCESS) {

        return -EFAULT;
    }

    wake_up(&read_wq);

    return i;
}

static long device_ioctl(struct file* filep, unsigned int ioctl_num, unsigned long ioctl_param) {

    /* Check if the ioctl_num is CHANGE_MAX_MESSAGES_SIZE */
    if(ioctl_num == CHANGE_MAX_MESSAGES_SIZE) {

        /* Lock because we access shared resources */
        mutex_lock(&queue_lock);
        if(ioctl_param > queuep->messages_size) {

            MAX_MESSAGES_SIZE = ioctl_param;
            printk(KERN_INFO "%s: New messages size - %lu bytes\n", PRINTING_NAME, MAX_MESSAGES_SIZE);
            mutex_unlock(&queue_lock);
            return SUCCESS;
        }
        mutex_unlock(&queue_lock);
    }

    /* Otherwise return Inval */
    return -EINVAL;
}

/* Handles process releasing the device */
static int device_release(struct inode* inodep, struct file* filep) {

    /*
     * Decrement the number of processes using this device
     * Useful so we do not remove the driver when it is in use
     */
    module_put(THIS_MODULE);
    return SUCCESS;
}

static struct message_queue* initialise_queue(void) {

    mutex_lock(&queue_lock);
    struct message_queue* queuep = (struct message_queue*) kmalloc(sizeof(struct message_queue), GFP_KERNEL);

    if(queuep != NULL) {

        queuep->head = queuep->rear = NULL;
        queuep->messages_size = 0;
    }
    mutex_unlock(&queue_lock);
    return queuep;
}

static void release_queue(struct message_queue* queuep) {

    mutex_lock(&queue_lock);
    /* If the pointer is null, we cannot release anything */
    if(queuep == NULL) {

        mutex_unlock(&queue_lock);
        return;
    }

    /* Lock because we are going to access the queue and modify it */
    /* If the head is not null, go through all the nodes and free them 1 by 1 */
    struct message_queue_node* tmp_node = queuep->head;
    struct message_queue_node* iterator_node = tmp_node;
    while(iterator_node != NULL) {

        iterator_node = iterator_node->next;
        if(tmp_node->data != NULL) {

            if(tmp_node->data->message != NULL) {

                kfree(tmp_node->data->message);
            }
            kfree(tmp_node->data);
        }
        kfree(tmp_node);
        tmp_node = iterator_node;
    }
    kfree(queuep);
    mutex_unlock(&queue_lock);
}

static int enqueue(struct message_queue* queuep, char* message, unsigned short message_size) {

    /* Nothing happens */
    mutex_lock(&queue_lock);
    if(queuep == NULL) {

        mutex_unlock(&queue_lock);
        return SUCCESS;
    }
    mutex_unlock(&queue_lock);

    /* Allocate memory for a node */
    struct message_queue_node* tmp_node = (struct message_queue_node*) kmalloc(sizeof(struct message_queue_node), GFP_KERNEL);

    /* If allocation failed, return -1 */
    if(tmp_node == NULL) {

        return -1;
    }

    /* If it was successful, allocate memory for data */
    tmp_node->next = NULL;
    tmp_node->data = (struct message_queue_data*) kmalloc(sizeof(struct message_queue_data), GFP_KERNEL);

    /* If allocation failed, clean and return -1 */
    if(tmp_node->data == NULL) {

        kfree(tmp_node);
        return -1;
    }

    tmp_node->data->message = (char*) kmalloc(message_size * sizeof(char), GFP_KERNEL);
    /* If allocation failed, clean and return -1 */
    if(tmp_node->data->message == NULL) {

        kfree(tmp_node->data);
        kfree(tmp_node);
        return -1;
    }
    memcpy(tmp_node->data->message, message, message_size);
    tmp_node->data->message_size = message_size;

    mutex_lock(&queue_lock);
    /* It means this is our first element to be added */
    if(queuep->rear == NULL) {

        queuep->head = queuep->rear = tmp_node;
    } else { /* It is not our first element */

        queuep->rear->next = tmp_node;
        queuep->rear = queuep->rear->next;
    }

    queuep->messages_size = queuep->messages_size + queuep->rear->data->message_size;
    mutex_unlock(&queue_lock);
    return SUCCESS;
}

static struct message_queue_data* dequeue(struct message_queue* queuep) {

    /* Cannot dequeue an empty queue */
    mutex_lock(&queue_lock);
    if(queuep == NULL) {

        mutex_unlock(&queue_lock);
        return NULL;
    }

    /* If there is no message in the queue, we cannot dequeue */
    if(queuep->head == NULL) {

        mutex_unlock(&queue_lock);
        return NULL;
    }

    /* If we are in the case of one element in the queue, just move the rear to NULL */
    if(queuep->head == queuep->rear) {

        queuep->rear = queuep->rear->next;
    }
    /* If there is message in the queue, fetch it */
    struct message_queue_node* tmp_node = queuep->head;
    /* Move the head to the next element */
    queuep->head = queuep->head->next;
    queuep->messages_size = queuep->messages_size - tmp_node->data->message_size;

    mutex_unlock(&queue_lock);

    struct message_queue_data* tmp_data = tmp_node->data;
    /* Free the fetched node */
    kfree(tmp_node);
    return tmp_data;
}

static int is_queue_empty(struct message_queue* queuep) {

    mutex_lock(&queue_lock);
    if(queuep == NULL) {

        mutex_unlock(&queue_lock);
        return -1;
    }

    if(queuep->messages_size == 0) {

        mutex_unlock(&queue_lock);
        return 1;
    }

    mutex_unlock(&queue_lock);
    return 0;
}

static int is_space_in_queue(struct message_queue* queuep, unsigned short length) {

    mutex_lock(&queue_lock);
    if(queuep == NULL) {

        mutex_unlock(&queue_lock);
        return -1;
    }

    if(queuep->messages_size + length > MAX_MESSAGES_SIZE) {

        mutex_unlock(&queue_lock);
        return 0;
    }

    mutex_unlock(&queue_lock);
    return 1;
}

module_init(char_device_driver_init);
module_exit(char_device_driver_exit);
