/**
 * @file    vcam_device.c
 * @author  dingyiqian
 * @brief   一个为V4L2虚拟摄像头创建平台设备(platform_device)的模块。
 * @details 该模块的唯一作用就是向内核注册一个平台设备。这个设备本身
 * 没有任何功能，它只是作为一个“占位符”或“硬件描述”，
 * 等待一个与之同名的平台驱动(platform_driver)来匹配和驱动。
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "vcam_plat"

static void vcam_plat_device_release(struct device *dev)
{
    pr_info("平台设备 %s 已释放\n", DRIVER_NAME);
}

static struct platform_device vcam_platform_device = {
    .name = DRIVER_NAME, // 设备的名称
    .id   = -1,          // -1 表示只有一个该类型的设备
    .dev = {
        .release = vcam_plat_device_release,
    },
};


static int __init vcam_device_init(void)
{
    pr_info("注册虚拟摄像头平台设备 '%s'...\n", DRIVER_NAME);
    return platform_device_register(&vcam_platform_device);
}
static void __exit vcam_device_exit(void)
{
    pr_info("注销虚拟摄像头平台设备 '%s'...\n", DRIVER_NAME);
    platform_device_unregister(&vcam_platform_device);
}

module_init(vcam_device_init);
module_exit(vcam_device_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dingyiqian");
MODULE_DESCRIPTION("为V4L2虚拟摄像头提供一个平台设备");
