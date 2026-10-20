# 飞腾 Linux 接入说明

## 初阶职责

飞腾 Linux 负责 UVC 摄像头采集、图像质量检查、模型推理、结果记录和简单界面。初阶通过 USB 连接 STM32 板载 CH340C；相机始终直接连接飞腾，不经过 STM32 传输图像。

## 公共代码接入

Linux C++ 应用可直接编译 `common/src`，公开头文件已提供 `extern "C"`。串口线程只负责字节收发和时间戳，完整帧由 `fi_stream_decoder_feed` 产生。业务线程保存当前会话号、工件号和单调递增序号，并把推理结果编码为 `FI_MSG_RESULT`。

建议模块边界：

```text
camera        V4L2 新帧和曝光清晰度检查
inference     固定 ROI 和 ncnn 模型调用
serial_port   termios 115200 8N1 与原始字节收发
protocol      复用 common 帧编解码
workflow      工件 ID 关联、超时和日志
```

## 首次板上验证命令

板上命令随发行版可能不同，执行后把实际输出登记到硬件清点表：

```bash
uname -a
cat /proc/device-tree/model
free -h
ls /dev/ttyUSB* /dev/video* 2>/dev/null
v4l2-ctl --list-devices
```

串口程序启动时禁止自动重发上一件动作。若 CH340C 重连导致 STM32 复位，Linux 必须等待新的 HELLO 和会话号，再接收下一件工件。

## 中阶迁移边界

中阶保留摄像头、推理和记录在 Linux，把工件状态机、期限判断和动作授权迁移到飞腾 FreeRTOS 独立核。Linux 仅通过 RPMsg 返回与当前工件 ID 对应的结果，不直接授权舵机动作。

