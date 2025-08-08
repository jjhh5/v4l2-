#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "camerathread.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

public slots:
    void updateFrame(const QImage &frame); /*接收新图像的槽*/

private slots:
    void on_picture_clicked();
    void on_brightness1_clicked();
    void on_brightness2_clicked();

private:
    Ui::Widget *ui;
    CameraThread *m_cameraThread;
    int m_brightness;
};
#endif
