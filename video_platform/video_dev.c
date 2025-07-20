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

// 定义一个唯一的设备/驱动匹配名称，这是两者之间唯一的联系。
#define DRIVER_NAME "vcam_plat"

/**
 * @brief 设备释放回调函数。
 * @details 当设备的最后一个引用被释放时，此函数被调用。对于一个
 * 静态分配的虚拟设备，这个函数可以为空。
 */
static void vcam_plat_device_release(struct device *dev)
{
    pr_info("平台设备 %s 已释放\n", DRIVER_NAME);
}

// 静态定义一个平台设备实例。
static struct platform_device vcam_platform_device = {
    .name = DRIVER_NAME, // 设备的名称，必须与驱动的名称匹配
    .id   = -1,          // -1 表示只有一个该类型的设备
    .dev = {
        .release = vcam_plat_device_release,
    },
};

/**
 * @brief 模块初始化函数。
 */
static int __init vcam_device_init(void)
{
    pr_info("注册虚拟摄像头平台设备 '%s'...\n", DRIVER_NAME);
    return platform_device_register(&vcam_platform_device);
}

/**
 * @brief 模块退出函数。
 */
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
