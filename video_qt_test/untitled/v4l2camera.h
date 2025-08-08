#ifndef V4L2CAMERA_H
#define V4L2CAMERA_H

#include <QString>
#include <QImage>
#include <linux/videodev2.h>

struct buffer {
    void   *start;
    size_t length;
};

class V4L2Camera
{
public:
    V4L2Camera();
    ~V4L2Camera();

    bool openDevice(const char *deviceName, int width, int height);
    void closeDevice();
    QImage getFrame();

    bool setBrightness(int value);

private:
    bool initDevice();
    void uninitDevice();

    int fd = -1;
    buffer *buffers = nullptr;
    unsigned int n_buffers = 0;
    int m_width = 0;
    int m_height = 0;
};

#endif
