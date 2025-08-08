#include "widget.h"
#include "ui_widget.h"
#include <QPixmap>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    /*初始化UI界面上所有的控件 (按钮, Label等)*/
    ui->setupUi(this);
    this->setWindowTitle("V4L2 Camera Demo (亮度显示)");

    /* 初始化一个变量来跟踪当前的亮度值*/
    m_brightness = 128;

    /* QString("亮度: %1").arg(m_brightness) 会生成 "亮度: 128" 这样的字符串。*/
    ui->label->setText(QString("亮度: %1").arg(m_brightness));

    /* 创建并配置后台工作线程*/
    m_cameraThread = new CameraThread(this);

    /* 连接信号槽：当后台线程发出 newFrame 信号时，调用主窗口的 updateFrame 函数*/
    connect(m_cameraThread, &CameraThread::newFrame, this, &Widget::updateFrame);

    /* 连接UI按钮的 clicked() 信号到对应的槽函数*/
    connect(ui->picture, &QPushButton::clicked, this, &Widget::on_picture_clicked);
    connect(ui->brightness1, &QPushButton::clicked, this, &Widget::on_brightness1_clicked);
    connect(ui->brightness2, &QPushButton::clicked, this, &Widget::on_brightness2_clicked);

    /* 启动后台线程捕捉摄像头画面*/
    m_cameraThread->start();
}

Widget::~Widget()
{
    m_cameraThread->stop();
    delete ui;
}

/*这个槽函数在每次接收到新图像时被调用*/
void Widget::updateFrame(const QImage &frame)
{
    /* 在主GUI线程中安全地更新UI界面*/
    ui->video_widget->setPixmap(QPixmap::fromImage(frame).scaled(
        ui->video_widget->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

/*拍照按钮的槽函数*/
void Widget::on_picture_clicked()
{
    m_cameraThread->capturePicture();
}

/*亮度 + 按钮的槽函数*/
void Widget::on_brightness1_clicked()
{
    /*增加亮度值不超过上限255*/
    m_brightness += 10;
    if (m_brightness > 255) m_brightness = 255;

    /*请求后台线程去设置硬件的亮度*/
    m_cameraThread->setBrightness(m_brightness);

    ui->label->setText(QString("亮度: %1").arg(m_brightness));
}

/*亮度 - 按钮的槽函数*/
void Widget::on_brightness2_clicked()
{
    /* 降低亮度值不低于下限0*/
    m_brightness -= 10;
    if (m_brightness < 0) m_brightness = 0;

    /*请求后台线程去设置硬件的亮度*/
    m_cameraThread->setBrightness(m_brightness);

    ui->label->setText(QString("亮度: %1").arg(m_brightness));
}
