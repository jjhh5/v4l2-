# 1 介绍
1. V4L2 虚拟摄像头平台驱动这是一个为学习目的而创建的、功能完整的Linux V4L2虚拟摄像头驱动程序。它基于平台总线 (Platform Bus) 模型，实现了设备与驱动的分离，并通过内核定时器模拟一个能产生视频数据流的虚拟硬件。当驱动加载成功后，它会在 /dev 目录下创建一个标准的 videoX 设备节点，用户空间的应用程序（如 GStreamer, FFmpeg, Qt）可以通过标准的V4L2接口来访问这个虚拟摄像头，获取由驱动动态生成的视频帧。✨ 功能特性设备与驱动分离: 采用标准的平台总线模型，将设备描述 (video_dev.c) 与驱动逻辑 (video_drv.c) 彻底解耦。V4L2 框架: 完整实现了 v4l2_device, video_device, v4l2_file_operations 和 v4l2_ioctl_ops 结构，支持标准的V4L2查询和控制命令。Videobuf2 缓冲区管理: 使用现代化的 videobuf2 (vb2) 框架来管理视频缓冲区，支持 MMAP 内存映射模式。虚拟数据流: 通过内核定时器 (timer_list) 模拟硬件中断，以大约30 FPS的帧率，循环生成纯色（红、绿、蓝）的YUYV格式视频帧。标准接口: 生成标准的 /dev/videoX 设备节点，兼容绝大多数V4L2应用程序。代码结构清晰: 包含详细的、符合开源标准的注释，非常适合用于学习Linux驱动开发。📂 项目结构.
├── video_dev.c     # 平台设备描述文件，负责注册一个虚拟设备
├── video_drv.c     # 平台驱动文件，包含所有V4L2核心逻辑
└── Makefile        # 用于编译这两个模块的Makefile
🛠️ 编译环境准备在编译本驱动前，请确保您已准备好以下环境：交叉编译工具链: 例如 aarch64-linux-gnu- 你自己对应的编译链也可以 完整的Linux内核源码树: 并且已经针对您的目标开发板（如RK3576）进行过正确的配置和完整编译（以生成 Module.symvers 文件）。🚀 编译与使用1. 修改 Makefile打开 Makefile 文件，确保 KERNEL_DIR 变量指向您正确的内核源码路径。KERNEL_DIR := /path/to/your/linux/kernel/source
2. 编译模块在项目根目录下，直接执行 make 命令：make
如果一切顺利，您会得到两个内核模块文件：video_dev.ko 和 video_drv.ko。3. 加载模块请务必按顺序加载这两个模块。您需要将这两个 .ko 文件复制到您的开发板上。首先，加载设备模块，它会在平台总线上注册一个“锁”：sudo insmod video_dev.ko
然后，加载驱动模块，它会去寻找并匹配那把“锁”：sudo insmod video_drv.ko
4. 验证驱动加载成功后，您可以通过以下方式验证：查看内核日志:dmesg | tail
您应该能看到类似以下的成功日志：注册虚拟摄像头平台设备 'vcam_plat'...
vcam_probe: 发现平台设备 'vcam_plat'，开始初始化驱动...
成功注册设备 /dev/video0
检查设备节点:ls /dev/video*
您应该能看到一个新的设备节点，比如 /dev/video0。5. 测试视频流您可以使用 v4l2-ctl 工具（通常需要安装 v4l-utils 包）来测试视频流：# 1. 查看设备信息
v4l2-ctl -d /dev/video0 --all

# 2. 从设备捕获10帧数据并保存
v4l2-ctl -d /dev/video0 --set-fmt-video=width=800,height=600,pixelformat=YUYV --stream-mmap --stream-count=10 --stream-to=test.yuv
执行后，您会得到一个 test.yuv 文件，可以用YUV播放器查看。6. 卸载模块请按与加载相反的顺序卸载模块：sudo rmmod video_drv
sudo rmmod video_dev
📄 许可证本项目采用 GPL v2 许可证。
