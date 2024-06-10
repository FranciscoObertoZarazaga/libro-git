#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>  // Include for msleep

#define DRIVER_NAME "bh1750_driver"
#define DRIVER_CLASS "BH1750ClassLuz"
#define I2C_BUS_AVAILABLE 1
#define SLAVE_DEVICE_NAME "BH1750"
#define BH1750_SLAVE_ADDRESS 0x23

static dev_t my_device_nr;
static struct class *my_class;
static struct cdev my_device;
static int device_open = 0;
static struct i2c_adapter *bh1750_i2c_adapter = NULL;
static struct i2c_client *bh1750_i2c_client = NULL;
static int current_measurement = 0;

// I2C Board Info
static struct i2c_board_info bh1750_i2c_board_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, BH1750_SLAVE_ADDRESS)
};

// Función para leer el nivel de luz desde el sensor BH1750
static int bh1750_read_light_level(void) {
    int ret;
    char buf[2];
    int lux;

    // Enviar comando de medición
    buf[0] = 0x10; // Continuously H-Resolution Mode
    ret = i2c_master_send(bh1750_i2c_client, buf, 1);
    if (ret < 0) {
        printk(KERN_ERR "Failed to send measurement command\n");
        return ret;
    }

    msleep(180); // Esperar a que se tome la medición

    // Leer datos de medición
    ret = i2c_master_recv(bh1750_i2c_client, buf, 2);
    if (ret < 0) {
        printk(KERN_ERR "Failed to read measurement data\n");
        return ret;
    }

    lux = (buf[0] << 8) | buf[1];
    return lux;
}

// Función de lectura
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    static int read_done = 0;  // Variable estática para controlar si ya se ha realizado una lectura
    char buf[16];
    int measurement;
    int ret;

    if (read_done) {
        read_done = 0;  // Reset para la próxima apertura del archivo
        return 0;  // Indicar que no hay más datos que leer
    }

    // Realizar la medición
    measurement = bh1750_read_light_level();
    if (measurement < 0) {
        printk(KERN_ERR "Measurement failed\n");
        return -EFAULT;
    }

    snprintf(buf, sizeof(buf), "%d\n", measurement);
    ret = copy_to_user(buffer, buf, strlen(buf));
    if (ret) {
        printk(KERN_ERR "Failed to send measurement to user\n");
        return -EFAULT;
    }

    read_done = 1;  // Indicar que se ha realizado una lectura

    return strlen(buf);
}

static int dev_open(struct inode *inode, struct file *file) {
    if (device_open) {
        return -EBUSY;
    }
    device_open++;
    try_module_get(THIS_MODULE);
    return 0;
}

static int dev_release(struct inode *inode, struct file *file) {
    device_open--;
    module_put(THIS_MODULE);
    return 0;
}

static struct file_operations fops = {
    .read = dev_read,
    .open = dev_open,
    .release = dev_release,
};

static int __init ModuleInit(void) {
    int ret;  // Variable to store return values

    // Allocate device number
    if (alloc_chrdev_region(&my_device_nr, 0, 1, DRIVER_NAME) < 0) {
        printk(KERN_ERR "Device number allocation failed!\n");
        return -1;
    }

    // Create device class
    my_class = class_create(DRIVER_CLASS);
    if (IS_ERR(my_class)) {
        printk(KERN_ERR "Device class creation failed!\n");
        unregister_chrdev_region(my_device_nr, 1);
        return PTR_ERR(my_class);
    }

    // Create device file
    if (device_create(my_class, NULL, my_device_nr, NULL, DRIVER_NAME) == NULL) {
        printk(KERN_ERR "Device file creation failed!\n");
        class_destroy(my_class);
        unregister_chrdev_region(my_device_nr, 1);
        return -1;
    }

    // Initialize and add cdev
    cdev_init(&my_device, &fops);
    if (cdev_add(&my_device, my_device_nr, 1) == -1) {
        printk(KERN_ERR "cdev add failed!\n");
        device_destroy(my_class, my_device_nr);
        class_destroy(my_class);
        unregister_chrdev_region(my_device_nr, 1);
        return -1;
    }

    // Register I2C Adapter
    bh1750_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
    if (bh1750_i2c_adapter == NULL) {
        printk(KERN_ERR "Failed to get I2C adapter\n");
        cdev_del(&my_device);
        device_destroy(my_class, my_device_nr);
        class_destroy(my_class);
        unregister_chrdev_region(my_device_nr, 1);
        return -1;
    }

    // Attach I2C client
    bh1750_i2c_client = i2c_new_client_device(bh1750_i2c_adapter, &bh1750_i2c_board_info);
    if (bh1750_i2c_client == NULL) {
        printk(KERN_ERR "Failed to create I2C client\n");
        i2c_put_adapter(bh1750_i2c_adapter);
        cdev_del(&my_device);
        device_destroy(my_class, my_device_nr);
        class_destroy(my_class);
        unregister_chrdev_region(my_device_nr, 1);
        return -1;
    }

    printk(KERN_INFO "Driver initialized.\n");
    return 0;
}

static void __exit ModuleExit(void) {
    i2c_unregister_device(bh1750_i2c_client);
    i2c_put_adapter(bh1750_i2c_adapter);
    cdev_del(&my_device);
    device_destroy(my_class, my_device_nr);
    class_destroy(my_class);
    unregister_chrdev_region(my_device_nr, 1);
    printk(KERN_INFO "Driver removed.\n");
}

module_init(ModuleInit);
module_exit(ModuleExit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SdC_LIPCEN_OBERTO_FERNANDEZ");
MODULE_DESCRIPTION("Driver de kernel para BH1750");
