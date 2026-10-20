# 飞腾智检公共协议与控制核心实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可由飞腾 Linux、飞腾 FreeRTOS 和 STM32 共同复用的串口协议、流式解码器与工件状态机，并提供可在开发电脑运行的自动测试和模拟程序。

**Architecture:** 公共核心采用无动态内存的 C99 代码，硬件访问全部留在平台适配层。帧协议负责会话号、工件号、序号、消息类型、负载和 CRC；流式解码器从任意字节边界恢复；状态机负责单件检测的合法转换、重复指令拒绝和期限检查。电脑端模拟器先证明协议与状态机行为，再把相同源文件加入 STM32CubeF1、飞腾 FreeRTOS SDK 和 Linux C++ 应用。

**Tech Stack:** C99、MinGW GCC、PowerShell 测试脚本；后续平台为 STM32CubeF1 HAL/LL、飞腾 FreeRTOS SDK、OpenAMP/RPMsg 和 Linux C++。

**Spec:** `../../../../飞腾智检-可执行方案V3.md`

## Global Constraints

- UART 起始参数为 115200 bps、8N1。
- 帧最大长度为 128 字节，常规长度为 32 至 64 字节。
- 帧字段固定为帧头、版本、类型、长度、会话号、工件号、命令序号、载荷和 CRC16。
- CRC 使用 CRC-16/CCITT-FALSE：多项式 0x1021，初值 0xFFFF，无反射，结果不异或。
- 多字节整数统一使用小端序。
- 重复、过期、错误 CRC、未知消息和错误工件号必须被拒绝，不能触发动作。
- 动态内存、操作系统 API、HAL API 和日志 API 不得进入 `common/`。
- 工件授权期限以控制端本地到位时间计算，不比较 Linux、FreeRTOS 和 STM32 的绝对时钟。
- 尚未实板验证的结果只能标为电脑端验证通过。

---

### Task 1: 项目骨架和可重复测试入口

**Files:**
- Create: `phytium-inspection/.gitignore`
- Create: `phytium-inspection/README.md`
- Create: `phytium-inspection/scripts/test.ps1`
- Create: `phytium-inspection/common/tests/test_main.c`

**Interfaces:**
- Consumes: Windows 上的 `gcc`。
- Produces: `scripts/test.ps1`，负责构建并运行所有电脑端 C 测试。

- [x] **Step 1: 写一个无法链接的测试入口**

```c
#include "ftinspect/protocol.h"

int main(void) {
    return fi_crc16_ccitt((const unsigned char *)"123456789", 9) == 0x29B1u ? 0 : 1;
}
```

- [x] **Step 2: 运行测试并确认因缺少协议实现而失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，编译器报告 `ftinspect/protocol.h` 不存在。

- [x] **Step 3: 添加只负责收集测试的脚本和项目说明**

```powershell
gcc -std=c99 -Wall -Wextra -Werror -I common/include common/src/protocol.c common/src/stream_decoder.c common/src/controller.c common/tests/test_main.c -o build/native-tests.exe
& build/native-tests.exe
```

- [x] **Step 4: 保留预期失败，交给 Task 2 提供首个实现**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，仍缺少协议头文件；失败原因与 Step 2 一致。

### Task 2: CRC 和帧编解码

**Files:**
- Create: `phytium-inspection/common/include/ftinspect/protocol.h`
- Create: `phytium-inspection/common/src/protocol.c`
- Modify: `phytium-inspection/common/tests/test_main.c`

**Interfaces:**
- Produces: `fi_crc16_ccitt`、`fi_frame_encode`、`fi_frame_decode`、`fi_frame_t`、`fi_status_t`。
- Frame: magic `A5 5A`; version `01`; header fields `type:u8 payload_len:u16 session_id:u32 job_id:u32 seq:u32`; payload 0 至 108 字节；末尾 CRC16 小端序。

- [x] **Step 1: 写 CRC 已知向量和固定帧测试**

```c
CHECK(fi_crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29B1u);
CHECK(fi_frame_encode(&frame, bytes, sizeof(bytes), &written) == FI_OK);
CHECK(written == 23u);
CHECK(memcmp(bytes, expected_frame, sizeof(expected_frame)) == 0);
```

- [x] **Step 2: 运行测试并确认因符号缺失而失败**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，报告 `fi_crc16_ccitt` 或帧类型未定义。

- [x] **Step 3: 实现最小 CRC 与帧编解码**

```c
uint16_t fi_crc16_ccitt(const uint8_t *data, size_t length);
fi_status_t fi_frame_encode(const fi_frame_t *frame, uint8_t *out, size_t capacity, size_t *written);
fi_status_t fi_frame_decode(const uint8_t *bytes, size_t length, fi_frame_t *out);
```

- [x] **Step 4: 增加坏帧行为测试**

覆盖错误帧头、错误版本、声明长度不符、CRC 错误、负载超长和未知消息类型；每个测试断言具体状态码。

- [x] **Step 5: 运行全部测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: PASS，退出码为 0 且输出各测试名称。

### Task 3: 流式解码与断线后重新同步

**Files:**
- Create: `phytium-inspection/common/include/ftinspect/stream_decoder.h`
- Create: `phytium-inspection/common/src/stream_decoder.c`
- Modify: `phytium-inspection/common/tests/test_main.c`

**Interfaces:**
- Consumes: `fi_frame_decode`。
- Produces: `fi_stream_decoder_init`、`fi_stream_decoder_feed`、`fi_stream_result_t`。

- [x] **Step 1: 写分片输入测试**

逐字节输入一个合法帧，前 N-1 个字节返回 `FI_STREAM_NEED_MORE`，最后一个字节返回 `FI_STREAM_FRAME_READY` 并得到相同工件号和载荷。

- [x] **Step 2: 运行测试并确认缺少流式解码器**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，报告流式解码类型或函数不存在。

- [x] **Step 3: 实现固定 128 字节缓冲区的流式解码器**

解码器只缓存从 `A5 5A` 开始的候选帧；长度超限、CRC 错误或噪声后继续寻找下一组帧头，不分配堆内存。

- [x] **Step 4: 写噪声、粘包和坏帧后恢复测试**

输入 `噪声 + 坏帧 + 合法帧1 + 合法帧2`，断言只产生两个合法帧，顺序和工件号正确。

- [x] **Step 5: 运行全部测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: PASS。

### Task 4: 工件控制状态机

**Files:**
- Create: `phytium-inspection/common/include/ftinspect/controller.h`
- Create: `phytium-inspection/common/src/controller.c`
- Modify: `phytium-inspection/common/tests/test_main.c`

**Interfaces:**
- Produces: `fi_controller_reset`、`fi_controller_arrive`、`fi_controller_begin_stabilization`、`fi_controller_request_capture`、`fi_controller_apply_result`、`fi_controller_route_ready`、`fi_controller_release_started`、`fi_controller_confirm_exit`、`fi_controller_tick`。
- States: `IDLE, ARRIVED, WAIT_STABLE, WAIT_RESULT, SET_ROUTE, RELEASE, VERIFY, FAULT`。
- Routes: `PASS, REVIEW`；识别不确定必须映射为 `REVIEW`。

- [x] **Step 1: 写正常单件流程测试**

从新会话开始，依次触发到位、稳定、拍摄请求、识别结果、挡板到位、放行和正确出口，断言最终返回 `IDLE`，完成计数增加 1。

- [x] **Step 2: 运行测试并确认状态机尚不存在**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，报告控制器类型或函数不存在。

- [x] **Step 3: 实现合法转换和错误状态拒绝**

所有转换返回明确状态码；错误状态、错误工件号、重复序号和反向序号均不改变状态和执行计数。

- [x] **Step 4: 写期限和故障测试**

覆盖识别授权超过 1000 ms、执行超过配置期限、走错出口、重复结果、复位新会话以及旧会话命令。

- [x] **Step 5: 运行全部测试**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: PASS。

### Task 5: 电脑端闭环模拟器

**Files:**
- Create: `phytium-inspection/tools/simulator.h`
- Create: `phytium-inspection/tools/simulator.c`
- Create: `phytium-inspection/scripts/run-simulator.ps1`
- Create: `phytium-inspection/docs/protocol-v1.md`
- Modify: `phytium-inspection/README.md`

**Interfaces:**
- Consumes: 帧编解码、流式解码和控制状态机。
- Produces: 无硬件情况下可运行的正常、复检、CRC 错误、重复命令和超时场景日志。

- [x] **Step 1: 写模拟场景验收测试**

测试调用模拟场景函数并断言正常件完成、低置信度件进入复检、坏 CRC 不改变状态、重复命令不重复执行、超时进入 `FAULT`。

- [x] **Step 2: 运行测试并确认模拟器入口缺失**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: FAIL，报告模拟场景函数不存在。

- [x] **Step 3: 实现模拟器和协议文档**

模拟器打印会话号、工件号、序号、消息类型、状态变化和拒绝原因；协议文档逐字节说明字段、CRC 范围、消息类型和期限规则。

- [x] **Step 4: 运行测试和人工模拟**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: PASS。

Run: `powershell -ExecutionPolicy Bypass -File scripts/run-simulator.ps1`
Expected: 五个场景均打印预期最终状态，进程退出码为 0。

### Task 6: 平台接入边界和首日硬件记录

**Files:**
- Create: `phytium-inspection/ports/stm32/README.md`
- Create: `phytium-inspection/ports/phytium-linux/README.md`
- Create: `phytium-inspection/ports/phytium-freertos/README.md`
- Create: `phytium-inspection/docs/hardware-inventory.md`

**Interfaces:**
- Consumes: `common/` 的公开头文件。
- Produces: 三个平台的源文件接入位置、回调边界、串口资源和实板验证清单。

- [x] **Step 1: 记录 STM32 接入约束**

写明 USART1 PA9/PA10 用于初阶 CH340C，USART2 PA2/PA3 用于中阶直连，P5 跳线和 DTR/RTS 复位行为必须实测。

- [x] **Step 2: 记录飞腾 Linux 与 FreeRTOS 接入约束**

写明 Linux 初阶串口入口、OpenAMP/RPMsg 消息边界、FreeRTOS 独占 UART 仍需实板确认，以及官方版本必须成套冻结。

- [x] **Step 3: 建立硬件清点表**

列出板卡丝印、内存、系统版本、SD 备份、摄像头 V4L2、P3/P5、电平、供电、下载器和器材状态的实测填写栏。

- [x] **Step 4: 最终验证**

Run: `powershell -ExecutionPolicy Bypass -File scripts/test.ps1`
Expected: PASS；文档中不含未定占位符或未经验证的完成声明。
