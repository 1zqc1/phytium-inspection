# STM32F103 接入说明

## 目标板和资源

当前资料对应正点原子精英 V2 系列和 STM32F103ZET6，实际板卡小版本必须在上电前记录。初阶通过板载 CH340C 和 USART1 通信；中阶候选链路使用 USART2 连接飞腾从核。

| 功能 | MCU 资源 | 接入要求 |
|---|---|---|
| 初阶业务串口 | USART1 PA9 TX、PA10 RX | P3 正确连接，使用 USB UART Type-C；业务帧中不混入调试文本 |
| 中阶业务串口 | USART2 PA2 TX、PA3 RX | 取下 P5 通往 RS485 的跳线，TX/RX 交叉，非隔离链路共地，先测电平 |
| 分流舵机 | PA8 TIM1 CH1 | OLED/CAMERA 接口保持空置 |
| 放行舵机 | PC6 TIM8 CH1 | OLED/CAMERA 接口保持空置 |
| 三路光电输入 | PC4、PC5、PC7 | 分别用于检测位和两个出口；实测极性、电平和去抖 |
| 下载调试 | PA13、PA14 SWD | 始终保留，不复用为业务 GPIO |

## 公共代码接入

将以下源文件加入 STM32 工程：

```text
common/src/protocol.c
common/src/stream_decoder.c
common/src/controller.c
```

把 `common/include` 加入编译器头文件路径。公共代码只依赖 C99 标准类型，不使用堆内存、HAL、RTOS 或文件系统。

USART 接收中断或 DMA 空闲中断取得的每个字节送入 `fi_stream_decoder_feed_timed`，传入单调毫秒计数及经实测确定的字节间超时。超时会丢弃不完整帧并保留当前字节作为新帧候选。只有返回 `FI_STREAM_FRAME_READY` 时才处理业务消息；返回 `FI_STREAM_FRAME_DROPPED` 时记录错误计数并保持执行器禁止，不能调用舵机动作函数。无时间信息的 `fi_stream_decoder_feed` 不提供接收超时保护。

业务层按以下顺序调用控制器：

```text
fi_controller_arrive
fi_controller_begin_stabilization
fi_controller_request_capture
fi_controller_apply_result
fi_controller_route_ready
fi_controller_release_started
fi_controller_confirm_exit
```

主循环或 1 ms 定时任务调用 `fi_controller_tick`。进入 `FI_STATE_FAULT` 后关闭放行授权，由人工清障并用新的会话号调用 `fi_controller_reset`。

`fi_controller_route_ready` 和 `fi_controller_release_started` 需要传入当前单调毫秒计数；截止时刻本身即已超时。完整帧通过类型、长度、CRC 和会话校验后，业务层才可将有效心跳送入 `fi_controller_heartbeat`。启动后必须先收到心跳才能接纳新工件；忙碌状态下心跳间隔达到 300 ms 会锁存故障，恢复需人工清障并建立新会话。调用方必须串行化控制器访问，并在硬件输出处落实禁能；这些软件检查尚未经过实板验证。

## 首次实板检查

1. 保持两只舵机断电，只验证下载、串口收发和复位行为。
2. 打开、关闭、重连 CH340C 串口，观察 DTR/RTS 是否导致 BOOT0 或 RESET 动作。
3. 先接一个传感器，测量高低电平和静态极性，再接 MCU 输入。
4. 先接一只空载舵机，使用独立电源并与 MCU 共地，测量启动峰值电流。
5. 完成单通道后再接第二舵机和两个出口传感器。

