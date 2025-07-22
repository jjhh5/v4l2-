/**
 * @file    video_capture.c
 * @author  dingyiqian
 * @brief   一个基于V4L2的USB摄像头视频捕获程序。
 * @details 该程序实现了从V4L2设备捕获YUYV格式的视频流，
 * 并将其保存为单独的.yuyv文件。同时，它创建了一个
 * 独立的线程来实时调整摄像头的亮度。程序可以通过
 * Ctrl+C 信号进行退出并释放所有资源。
 * @platform RK3576 (同样适用于其他支持V4L2的Linux平台)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for exit()
#include <linux/types.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/mman.h>
#include <pthread.h>
#include <signal.h> // 为了处理信号

// 用来保存每个缓冲区的地址和长度
struct buffer {
    void   *start;
    size_t length;
};

static volatile int quit_flag = 0;

// Ctrl+C 信号处理函数
void handle_sigint(int sig)
{
    printf("\n捕获到 Ctrl+C 信号，准备退出...\n");
    quit_flag = 1;
}

/**
 * @brief 亮度控制线程的核心函数。
 * @param arg 传递给线程的参数，这里是设备的文件描述符(fd)。
 * @return NULL
 */
static void *thread_brightness_control(void *arg)
{
    // 将传入的void*参数安全地转换回文件描述符
    int fd = (int)(long)arg;
    int c;

    struct v4l2_queryctrl qctrl; // 用于查询控制项属性的结构体
    struct v4l2_control ctl;     // 用于获取/设置控制项值的结构体
    int delta;                   // 每次调整的步长

    // 查询设备是否支持亮度控制，并获取其范围 ---

    // 必须先清空结构体
    memset(&qctrl, 0, sizeof(qctrl));
    // 告诉内核，我们想查询的是“亮度”这个控制项
    qctrl.id = V4L2_CID_BRIGHTNESS;

    // 发送 VIDIOC_QUERYCTRL 命令给驱动
    if (0 != ioctl(fd, VIDIOC_QUERYCTRL, &qctrl)) {
        perror("ioctl VIDIOC_QUERYCTRL 失败，该设备可能不支持亮度控制");
        return NULL;
    }

    // 如果成功，驱动会填充qctrl结构体，我们可以打印出范围信息
    printf("亮度控制线程启动：范围 [min=%d, max=%d], 步长=%d, 默认值=%d\n",
           qctrl.minimum, qctrl.maximum, qctrl.step, qctrl.default_value);
    printf("请输入 'u' 增加亮度, 'd' 减少亮度, 然后按回车。\n");

    // 计算一个合适的调整步长，比如范围的1/10
    delta = (qctrl.maximum - qctrl.minimum) / 10;
    if (delta == 0) delta = 1; // 确保步长至少为1

    //获取当前的亮度值 ---

    // 告诉内核，我们要操作的是“亮度”
    ctl.id = V4L2_CID_BRIGHTNESS;

    // 发送 VIDIOC_G_CTRL (Get Control) 命令给驱动
    if (0 != ioctl(fd, VIDIOC_G_CTRL, &ctl)) {
        perror("ioctl VIDIOC_G_CTRL 失败");
        return NULL;
    }
    printf("获取到当前亮度为: %d\n", ctl.value);


    // 等待用户输入并设置新值 ---

    while (!quit_flag) {
        // getchar() 是一个阻塞函数，会等待用户输入并按回车
        c = getchar();
        if (quit_flag) break; // 如果主线程收到退出信号，则退出循环

        // 根据用户输入调整亮度值
        if (c == 'u' || c == 'U') {
            ctl.value += delta;
        } else if (c == 'd' || c == 'D') {
            ctl.value -= delta;
        } else if (c == '\n' || c == '\r') {
            continue; // 忽略单独的回车
        } else {
            printf("无效输入，请输入 'u' 或 'd'.\n");
            continue;
        }

        // 边界检查，确保新值不会超出硬件支持的范围
        if (ctl.value > qctrl.maximum) ctl.value = qctrl.maximum;
        if (ctl.value < qctrl.minimum) ctl.value = qctrl.minimum;

        // 发送 VIDIOC_S_CTRL (Set Control) 命令给驱动，将新值写入硬件
        if (0 != ioctl(fd, VIDIOC_S_CTRL, &ctl)) {
            perror("ioctl VIDIOC_S_CTRL 设置亮度失败");
        } else {
            printf("当前亮度已成功设置为: %d\n", ctl.value);
        }
    }

    printf("亮度控制线程退出。\n");
    return NULL;
}

int main(int argc, char **argv)
{
    int fd;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers rb;
    int buf_cnt;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct pollfd fds[1];
    char filename[32];
    int file_cnt = 0;
    int i;
    struct buffer bufs[32]; // 最多支持32个缓冲区

    signal(SIGINT, handle_sigint);

    if (argc != 2) {
        fprintf(stderr, "用法: %s </dev/videox>\n", argv[0]);
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("无法打开设备");
        return -1;
    }

    // --- V4L2 初始化流程 ---
    // 设置格式
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  //使用YUYV的摄像头 这里可以改掉
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        perror("设置格式失败");
        close(fd);
        return -1;
    }

    //请求缓冲区
    memset(&rb, 0, sizeof(rb));
    rb.count = 4; // 请求4个缓冲区
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &rb) != 0) {
        perror("请求缓冲区失败");
        close(fd);
        return -1;
    }
    //以它实际分配的为准
    buf_cnt = rb.count; 
    printf("驱动实际分配了 %d 个缓冲区\n", buf_cnt);

    //查询并映射所有缓冲区
     
    for (i = 0; i < buf_cnt; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
            perror("查询缓冲区失败");
            exit(EXIT_FAILURE);
        }

        bufs[i].length = buf.length;
        bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (bufs[i].start == MAP_FAILED) {
            perror("映射缓冲区失败");
            exit(EXIT_FAILURE);
        }
    }
    printf("成功映射 %d 个缓冲区\n", buf_cnt);

    // 将所有缓冲区放入驱动的空闲队列 (QBUF)
    for (i = 0; i < buf_cnt; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            perror("将缓冲区入队失败");
            exit(EXIT_FAILURE);
        }
    }
    printf("成功将 %d 个缓冲区入队\n", buf_cnt);
    
    // 启动视频流
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        perror("启动视频流失败");
        close(fd);
        return -1;
    }
    printf("视频流已启动。\n");

    // 创建线程
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, thread_brightness_control, (void *)(long)fd) != 0) {
        perror("创建线程失败");
        // ... 清理资源 ...
        close(fd);
        return -1;
    }

    printf("主循环开始，按 Ctrl+C 退出。\n");

    // 主循环，捕获视频帧
    while (!quit_flag) {
        memset(fds, 0, sizeof(fds));
        fds[0].fd = fd;
        fds[0].events = POLLIN;

        int ret = poll(fds, 1, 1000); 

        if (ret < 0) {
            perror("Poll 错误");
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (ioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
                perror("将缓冲区出队失败");
                break;
            }

            printf("捕获到第 %d 帧数据，大小: %u\n", file_cnt, buf.bytesused);
            sprintf(filename, "video_frame_%04d.yuyv", file_cnt++);
            int frame_fd = open(filename, O_WRONLY | O_CREAT, 0666);
            if (frame_fd >= 0) {
                write(frame_fd, bufs[buf.index].start, buf.bytesused);
                close(frame_fd);
            } else {
                printf("无法创建文件: %s\n", filename);
            }

            if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
                perror("将缓冲区再次入队失败");
                break;
            }
        }
    }

    printf("主循环结束，准备清理资源。\n");

    printf("等待亮度控制线程退出... (可能需要按一下回车来解除阻塞)\n");
    pthread_join(thread_id, NULL);
    //停止视频流
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    printf("视频流已停止。\n");
    
    // 解除内存映射
    for (i = 0; i < buf_cnt; i++) {
        if (munmap(bufs[i].start, bufs[i].length) != 0) {
            perror("解除映射失败");
        }
    }
    printf("所有缓冲区已解除映射。\n");

    close(fd);
    printf("设备已关闭，程序退出。\n");

    return 0;
}
