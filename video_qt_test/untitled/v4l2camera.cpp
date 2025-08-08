#include "v4l2camera.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include <QDebug>
#include <algorithm>

/*添加一个成员变量来记录当前的像素格式*/
static v4l2_format current_fmt;

V4L2Camera::V4L2Camera() {}

V4L2Camera::~V4L2Camera() {
    closeDevice();
}

bool V4L2Camera::openDevice(const char *deviceName, int width, int height) {
    fd = open(deviceName, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        qDebug() << "错误: 无法打开设备" << deviceName;
        return false;
    }
    m_width = width;
    m_height = height;
    if (!initDevice()) {
        closeDevice();
        return false;
    }
    return true;
}

bool V4L2Camera::initDevice() {
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        qDebug() << "错误: VIDIOC_QUERYCAP 失败";
        return false;
    }

    /*尝试多种格式*/
    memset(&current_fmt, 0, sizeof(current_fmt));
    current_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    current_fmt.fmt.pix.width       = m_width;
    current_fmt.fmt.pix.height      = m_height;
    current_fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    /*首先尝试 YUYV*/
    current_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    if (ioctl(fd, VIDIOC_S_FMT, &current_fmt) == 0) {
        qDebug() << "成功设置格式为 YUYV";
    } else {
        /* 如果 YUYV 失败，尝试 MJPEG*/
        qDebug() << "YUYV 格式设置失败, 正在尝试 MJPEG...";
        current_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (ioctl(fd, VIDIOC_S_FMT, &current_fmt) != 0) {
            qDebug() << "错误: MJPEG 格式也设置失败";
            return false;
        }
        qDebug() << "成功设置格式为 MJPEG";
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        qDebug() << "错误: VIDIOC_REQBUFS 失败";
        return false;
    }

    buffers = (buffer*)calloc(req.count, sizeof(*buffers));
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = n_buffers;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qDebug() << "错误: VIDIOC_QUERYBUF 失败";
            return false;
        }
        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[n_buffers].start == MAP_FAILED) {
            qDebug() << "错误: mmap 失败";
            return false;
        }
    }

    for (unsigned int i = 0; i < n_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            qDebug() << "错误: VIDIOC_QBUF 失败";
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        qDebug() << "错误: VIDIOC_STREAMON 失败";
        return false;
    }
    return true;
}

QImage V4L2Camera::getFrame() {
    if (fd < 0) return QImage();
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        return QImage();
    }

    QImage image;
    /*根据当前格式选择不同的处理方式*/
    if (current_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
        /*YUYV to RGB转换*/
        image = QImage(m_width, m_height, QImage::Format_RGB888);
        unsigned char* yuyv = (unsigned char*)buffers[buf.index].start;
        unsigned char* rgb = image.bits();
        for (int i = 0; i < m_width * m_height / 2; ++i) {
            int y1 = yuyv[i*4 + 0];
            int u  = yuyv[i*4 + 1] - 128;
            int y2 = yuyv[i*4 + 2];
            int v  = yuyv[i*4 + 3] - 128;
            auto clamp = [](int val) { return (unsigned char)std::max(0, std::min(val, 255)); };
            rgb[i*6 + 0] = clamp(y1 + 1.402 * v);
            rgb[i*6 + 1] = clamp(y1 - 0.344 * u - 0.714 * v);
            rgb[i*6 + 2] = clamp(y1 + 1.772 * u);
            rgb[i*6 + 3] = clamp(y2 + 1.402 * v);
            rgb[i*6 + 4] = clamp(y2 - 0.344 * u - 0.714 * v);
            rgb[i*6 + 5] = clamp(y2 + 1.772 * u);
        }
    } else if (current_fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
        /*MJPEG格式，直接用Qt的解码功能*/
        image = QImage::fromData((const uchar *)buffers[buf.index].start, buf.bytesused, "JPEG");
    }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        qDebug() << "警告: VIDIOC_QBUF 失败";
    }
    return image;
}

void V4L2Camera::uninitDevice() {
    if (fd < 0) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (unsigned int i = 0; i < n_buffers; ++i) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    buffers = nullptr;
    n_buffers = 0;
}

void V4L2Camera::closeDevice() {
    if (fd >= 0) {
        uninitDevice();
        close(fd);
        fd = -1;
    }
}

bool V4L2Camera::setBrightness(int value) {
    if (fd < 0) return false;
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_BRIGHTNESS;
    ctl.value = value;
    if (ioctl(fd, VIDIOC_S_CTRL, &ctl) != 0) {
        qDebug() << "错误: 设置亮度失败";
        return false;
    }
    return true;
}
