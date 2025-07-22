编译应用程序目前需要添加  -pthread 这个文件
        eg aarch64-linux-gnu-gcc video_test.c -o video_test -pthread
使用方法也需要在后面添加接口
        eg ./video /dev/video*