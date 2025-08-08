#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QImage>
#include "v4l2camera.h"

class CameraThread : public QThread
{
    Q_OBJECT
public:
    explicit CameraThread(QObject *parent = nullptr);
    ~CameraThread();

    void stop();
    void setBrightness(int value);
    void capturePicture();

signals:
    void newFrame(const QImage &frame);

protected:
    void run() override;

private:
    V4L2Camera *m_camera;
    volatile bool m_running;
    volatile bool m_capture_request;
    int m_brightness_value;
    volatile bool m_brightness_changed;
};

#endif
