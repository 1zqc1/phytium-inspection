# 飞腾智检源码

本目录承载飞腾 Linux、飞腾 FreeRTOS 和 STM32 共用的业务代码及平台适配层。项目按可独立验证的增量推进，当前阶段先实现公共串口协议、流式解码器和工件控制状态机。

## 当前目录

- `common/`：不依赖操作系统、HAL 或动态内存的 C99 公共核心。
- `scripts/`：Windows 开发电脑上的构建与验证入口。
- `tools/`：电脑端闭环模拟器。
- `ports/`：STM32、飞腾 Linux 和飞腾 FreeRTOS 的接入说明与适配代码。
- `docs/`：协议、硬件清点表和实施计划。

## 运行测试

在本目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
```

测试通过只表示公共代码在电脑端通过验证。板卡、串口、电平、外设和 OpenAMP 仍需按硬件清单逐项实测。

## 运行闭环模拟器

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run-simulator.ps1
```

模拟器覆盖正常合格、低置信度复检、CRC 错误、重复序号和识别授权超时五个场景。

## 当前进度

- 已完成：协议 V1、CRC、固定帧编解码、串口流式解码、工件状态机和电脑端模拟器。
- 已验证：MinGW GCC 严格警告模式下的电脑端自动测试。
- 待实物：STM32 串口和舵机、飞腾相机、OpenAMP、飞腾从核独占 UART 及完整分流机构。

