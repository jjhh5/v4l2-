/**
 * @file    vcam_driver.c
 * @author  dingyiqian
 * @brief   一个基于Platform总线的、支持亮度控制的V4L2虚拟摄像头驱动。
 * @details 本驱动在V4L2虚拟摄像头的基础上，增加了标准的亮度控制接口。
 * 用户空间程序可以通过V4L2_CID_BRIGHTNESS控制项来查询和设置亮度，
 * 驱动会实时地将亮度效果应用到输出的视频帧上。
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#define IMAGE_WIDTH  800
#define IMAGE_HEIGHT 600
#define IMAGE_SIZE   (IMAGE_WIDTH * IMAGE_HEIGHT * 2) // YUYV格式
#define DRIVER_NAME "vcam_plat"

struct vcam_device {
    struct v4l2_device v4l2_dev;
    struct video_device vdev;
    struct vb2_queue vb_queue;
    struct mutex lock;
    struct list_head queued_bufs;
    struct spinlock queued_lock;
    struct timer_list timer;
    int copy_cnt;
    int brightness;
};

struct vcam_frame_buf {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

//前向声明
static void vcam_timer_expire(struct timer_list *t);
static const struct v4l2_file_operations vcam_fops;
static const struct v4l2_ioctl_ops vcam_ioctl_ops;
static const struct vb2_ops vcam_vb2_ops;


/**
 * 动态生成YUYV格式的纯色图像，并应用亮度调节。
 */
static void fill_yuyv_buffer(void *ptr, int color_type, int brightness)
{
    unsigned char y, u, v;
    unsigned char *buf = ptr;
    int i;
    int y_final;

    switch (color_type) {
        case 0: y = 76; u = 84; v = 255; break;   // 红色
        case 1: y = 149; u = 43; v = 21; break;  // 绿色
        default: y = 29; u = 255; v = 107; break; // 蓝色
    }

    /*
     * 亮度只影响Y(亮度)分量。亮度值从[0, 255]映射到[-128, 127]的调整范围。
     * 默认值128对应调整量0。
     */
    y_final = clamp(y + brightness - 128, 0, 255);

    for (i = 0; i < IMAGE_SIZE; i += 4) {
        buf[i]     = y_final; // 第一个像素的亮度
        buf[i + 1] = u;
        buf[i + 2] = y_final; // 第二个像素的亮度
        buf[i + 3] = v;
    }
}

static struct vcam_frame_buf *vcam_get_next_buf(struct vcam_device *dev)
{
    unsigned long flags;
    struct vcam_frame_buf *buf = NULL;
    spin_lock_irqsave(&dev->queued_lock, flags);
    if (!list_empty(&dev->queued_bufs)) {
        buf = list_first_entry(&dev->queued_bufs, struct vcam_frame_buf, list);
        list_del(&buf->list);
    }
    spin_unlock_irqrestore(&dev->queued_lock, flags);
    return buf;
}

static void vcam_timer_expire(struct timer_list *t)
{
    struct vcam_device *dev = from_timer(dev, t, timer);
    struct vcam_frame_buf *buf;
    void *ptr;

    buf = vcam_get_next_buf(dev);
    if (buf) {
        ptr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
        // 将当前亮度值传递给填充函数
        fill_yuyv_buffer(ptr, dev->copy_cnt / 60, dev->brightness);

        vb2_set_plane_payload(&buf->vb.vb2_buf, 0, IMAGE_SIZE);
        buf->vb.vb2_buf.timestamp = ktime_get_ns();
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
    }

    dev->copy_cnt = (dev->copy_cnt + 1) % 180;
    mod_timer(&dev->timer, jiffies + HZ / 30);
}

static int vcam_queue_setup(struct vb2_queue *vq,
                            unsigned int *nbuffers, unsigned int *nplanes,
                            unsigned int sizes[], struct device *alloc_devs[])
{
    if (*nplanes)
        return sizes[0] < IMAGE_SIZE ? -EINVAL : 0;
    *nplanes = 1;
    sizes[0] = IMAGE_SIZE;
    return 0;
}

static void vcam_buf_queue(struct vb2_buffer *vb)
{
    struct vcam_device *dev = vb2_get_drv_priv(vb->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct vcam_frame_buf *buf = container_of(vbuf, struct vcam_frame_buf, vb);
    unsigned long flags;
    spin_lock_irqsave(&dev->queued_lock, flags);
    list_add_tail(&buf->list, &dev->queued_bufs);
    spin_unlock_irqrestore(&dev->queued_lock, flags);
}

static int vcam_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct vcam_device *dev = vb2_get_drv_priv(vq);
    mod_timer(&dev->timer, jiffies + HZ / 30);
    return 0;
}

static void vcam_stop_streaming(struct vb2_queue *vq)
{
    struct vcam_device *dev = vb2_get_drv_priv(vq);
    unsigned long flags;
    del_timer_sync(&dev->timer);
    spin_lock_irqsave(&dev->queued_lock, flags);
    while (!list_empty(&dev->queued_bufs)) {
        struct vcam_frame_buf *buf;
        buf = list_first_entry(&dev->queued_bufs, struct vcam_frame_buf, list);
        list_del(&buf->list);
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
    }
    spin_unlock_irqrestore(&dev->queued_lock, flags);
}

static const struct vb2_ops vcam_vb2_ops = {
    .queue_setup     = vcam_queue_setup,
    .buf_queue       = vcam_buf_queue,
    .start_streaming = vcam_start_streaming,
    .stop_streaming  = vcam_stop_streaming,
    .wait_prepare    = vb2_ops_wait_prepare,
    .wait_finish     = vb2_ops_wait_finish,
};

static int vcam_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
    strscpy(cap->driver, "V4L2 Virtual Cam", sizeof(cap->driver));
    strscpy(cap->card, "V4L2 Virtual Cam", sizeof(cap->card));
    snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s", DRIVER_NAME);
    cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
    return 0;
}

static int vcam_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
    if (f->index > 0) return -EINVAL;
    strscpy(f->description, "YUYV 4:2:2", sizeof(f->description));
    f->pixelformat = V4L2_PIX_FMT_YUYV;
    return 0;
}

static int vcam_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    f->fmt.pix.width        = IMAGE_WIDTH;
    f->fmt.pix.height       = IMAGE_HEIGHT;
    f->fmt.pix.pixelformat  = V4L2_PIX_FMT_YUYV;
    f->fmt.pix.field        = V4L2_FIELD_NONE;
    f->fmt.pix.bytesperline = IMAGE_WIDTH * 2;
    f->fmt.pix.sizeimage    = IMAGE_SIZE;
    return 0;
}

static int vcam_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    return vcam_g_fmt_vid_cap(file, priv, f);
}


static int vcam_queryctrl(struct file *file, void *priv, struct v4l2_queryctrl *qc)
{
    // 只支持亮度控制
    if (qc->id == V4L2_CID_BRIGHTNESS) {
        qc->type = V4L2_CTRL_TYPE_INTEGER;
        strscpy(qc->name, "Brightness", sizeof(qc->name));
        qc->minimum = 0;
        qc->maximum = 255;
        qc->step = 1;
        qc->default_value = 128;
        qc->flags = 0;
        return 0;
    }
    return -EINVAL;
}

static int vcam_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
    // 获取与此文件实例关联的设备私有数据
    struct vcam_device *dev = video_drvdata(file);

    if (ctrl->id == V4L2_CID_BRIGHTNESS) {
        ctrl->value = dev->brightness;
        return 0;
    }
    return -EINVAL;
}

static int vcam_s_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
    struct vcam_device *dev = video_drvdata(file);

    if (ctrl->id == V4L2_CID_BRIGHTNESS) {
        dev->brightness = clamp(ctrl->value, 0, 255);
        return 0;
    }
    return -EINVAL;
}


static const struct v4l2_ioctl_ops vcam_ioctl_ops = {
    .vidioc_querycap      = vcam_querycap,
    .vidioc_enum_fmt_vid_cap = vcam_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap = vcam_g_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap = vcam_s_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap = vcam_s_fmt_vid_cap,

    /* 将控制项相关的ioctl注册到“分机号列表”中 */
    .vidioc_queryctrl     = vcam_queryctrl,
    .vidioc_g_ctrl        = vcam_g_ctrl,
    .vidioc_s_ctrl        = vcam_s_ctrl,

    .vidioc_reqbufs       = vb2_ioctl_reqbufs,
    .vidioc_querybuf      = vb2_ioctl_querybuf,
    .vidioc_qbuf          = vb2_ioctl_qbuf,
    .vidioc_dqbuf         = vb2_ioctl_dqbuf,
    .vidioc_streamon      = vb2_ioctl_streamon,
    .vidioc_streamoff     = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations vcam_fops = {
    .owner          = THIS_MODULE,
    .open           = v4l2_fh_open,
    .release        = vb2_fop_release,
    .read           = vb2_fop_read,
    .poll           = vb2_fop_poll,
    .mmap           = vb2_fop_mmap,
    .unlocked_ioctl = video_ioctl2,
};

static int vcam_probe(struct platform_device *pdev)
{
    struct vcam_device *dev;
    struct video_device *vdev;
    int ret;

    pr_info("vcam_probe: 发现平台设备 '%s'，开始初始化驱动...\n", pdev->name);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;
    
    /* 在 kzalloc 之后，安全地初始化亮度默认值 */
    dev->brightness = 128;

    mutex_init(&dev->lock);
    spin_lock_init(&dev->queued_lock);
    INIT_LIST_HEAD(&dev->queued_bufs);
    timer_setup(&dev->timer, vcam_timer_expire, 0);

    ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
    if (ret) goto free_dev;

    dev->vb_queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dev->vb_queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
    dev->vb_queue.drv_priv = dev;
    dev->vb_queue.buf_struct_size = sizeof(struct vcam_frame_buf);
    dev->vb_queue.ops = &vcam_vb2_ops;
    dev->vb_queue.mem_ops = &vb2_vmalloc_memops;
    dev->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    dev->vb_queue.lock = &dev->lock;
    ret = vb2_queue_init(&dev->vb_queue);
    if (ret) goto unreg_v4l2_dev;

    vdev = &dev->vdev;
    strscpy(vdev->name, "VirtualCam_Platform", sizeof(vdev->name));
    vdev->fops = &vcam_fops;
    vdev->ioctl_ops = &vcam_ioctl_ops;
    vdev->v4l2_dev = &dev->v4l2_dev;
    vdev->queue = &dev->vb_queue;
    vdev->lock = &dev->lock;
    vdev->release = video_device_release_empty;
    video_set_drvdata(vdev, dev);
    
    vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;

    ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
    if (ret) {
        pr_err("注册 video_device 失败 (%d)\n", ret);
        goto unreg_v4l2_dev;
    }

    platform_set_drvdata(pdev, dev);
    pr_info("成功注册设备 %s\n", video_device_node_name(vdev));
    return 0;

unreg_v4l2_dev:
    v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
    kfree(dev);
    pr_err("vcam_probe 失败\n");
    return ret;
}

static int vcam_remove(struct platform_device *pdev)
{
    struct vcam_device *dev = platform_get_drvdata(pdev);
    pr_info("vcam_remove: 卸载设备 %s\n", video_device_node_name(&dev->vdev));
    video_unregister_device(&dev->vdev);
    v4l2_device_unregister(&dev->v4l2_dev);
    kfree(dev);
    return 0;
}

static struct platform_driver vcam_pdrv = {
    .probe  = vcam_probe,
    .remove = vcam_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

module_platform_driver(vcam_pdrv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dingyiqian");
MODULE_DESCRIPTION("一个基于Platform总线的、支持亮度控制的V4L2虚拟摄像头驱动");
