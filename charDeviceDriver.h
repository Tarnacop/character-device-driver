/**
 * @file charDeviceDriver.h
 * @author Alexandru Blinda
 * @date 6 November 2017
 * @version 0.1
 * @brief Header file that declares methods and structs that will be used.
 * Part of exercise 3 for Operating Systems Module (creating a character device
 * driver).
 */
#ifndef CHARDEVICEDRIVER_H
#define CHARDEVICEDRIVER_H

#define PRINTING_NAME "CharDeviceDriver"
#define SUCCESS 0
#define DEVICE_NAME "opsysmem" /* The device will appear as /dev/opsysmem */
#define MAX_MESSAGE_SIZE 4096 /* 4KiB in bytes; subject to change */
#define CHANGE_MAX_MESSAGES_SIZE 0 /* Used for ioctl check */
static unsigned long MAX_MESSAGES_SIZE = 2097152; /* 2MiB in bytes; subject to change */
static int major_number; /* major number assigned to our device driver */

/* Prototype functions for file operations */
static int __init char_device_driver_init(void);
static void __exit char_device_driver_exit(void);
static int device_open(struct inode*, struct file*);
static int device_release(struct inode*, struct file*);
static ssize_t device_read(struct file*, char*, size_t, loff_t*);
static ssize_t device_write(struct file*, const char*, size_t, loff_t*);
static long device_ioctl(struct file*, unsigned int, unsigned long);

/*
 * Devices are represented as file structures in kernel.
 * The file_operations struct declares the operations over a device.
 */
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = device_read,
	.write = device_write,
	.unlocked_ioctl = device_ioctl,
	.open = device_open,
	.release = device_release
};

/* Declaring queue struct and operations on it */

/* Struct to hold the message and the message size */
struct message_queue_data {

    char* message; /* The stored message */
    unsigned short message_size; /* Max message size is 4096 so short can hold it */
};

/* Struct to represent the node of a queue (data and next element) */
struct message_queue_node {

    struct message_queue_data* data;
    struct message_queue_node* next;
};

/* Struct to represent the queue - it holds the head of the queue and the size */
struct message_queue {

    struct message_queue_node* head;
    struct message_queue_node* rear;
    unsigned long messages_size; /* Size of all messages stored in queue*/
};

static struct message_queue* initialise_queue(void);
static void release_queue(struct message_queue*);
static int enqueue(struct message_queue*, char*, unsigned short);
static struct message_queue_data* dequeue(struct message_queue*);
static int is_queue_empty(struct message_queue*);
static int is_space_in_queue(struct message_queue*, unsigned short);

#endif
