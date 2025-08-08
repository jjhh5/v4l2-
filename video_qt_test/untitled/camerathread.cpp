#include "camerathread.h"
#include <QDebug>
#include <QDateTime>

CameraThread::CameraThread(QObject *parent) : QThread(parent)
{
    m_camera = new V4L2Camera();
    m_running = false;
    m_capture_request = false;
    m_brightness_value = 128; /*默认值*/
    m_brightness_changed = false;
}

CameraThread::~CameraThread()
{
    stop();
    delete m_camera;
}

void CameraThread::stop()
{
    m_running = false;
    wait(); /*等待run()函数结束*/
}

void CameraThread::setBrightness(int value)
{
    m_brightness_value = value;
    m_brightness_changed = true;
}

void CameraThread::capturePicture()
{
    m_capture_request = true;
}

void CameraThread::run()
{
    m_running = true;
    /*在线程启动时才打开设备*/
    if (!m_camera->openDevice("/dev/video1", 640, 480)) {
        qDebug() << "线程错误: 无法在线程中打开摄像头";
        m_running = false;
        return;
    }

    while (m_running)
    {
        if (m_brightness_changed) {
            m_camera->setBrightness(m_brightness_value);
            m_brightness_changed = false;
        }

        QImage frame = m_camera->getFrame();
        if (!frame.isNull()) {
            emit newFrame(frame);

            if (m_capture_request) {
                QString fileName = QString("capture_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
                frame.save(fileName);
                qDebug() << "图片已保存为:" << fileName;
                    m_capture_request = false;
            }
        }
        /*短暂休眠，避免CPU占用过高*/
        msleep(30);
    }

    m_camera->closeDevice();
}
