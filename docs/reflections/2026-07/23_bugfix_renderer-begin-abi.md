# 开发反思报告

**日期**: 2026-07-23
**提交类型**: bugfix
**会话时长**: 30 分钟
**修改文件数**: 5 个文件

## 1. 概述

修复 PostNG/PostAE 在进入主菜单前因 Deferred renderer hook ABI 不匹配而发生的访问冲突。REL ID `2276833` 实际指向 `BSGraphics::Renderer::Begin(uint32_t windowID)`，旧 thunk 错将其作为单参数 `ResetState` 调用，丢失了窗口索引参数。

## 2. 修改内容

### 修改的文件

- `src/Core/Deferred.h` - 将 hook 声明改为带 `windowID` 的 `Renderer_Begin`。
- `src/Core/Deferred.cpp` - 安装正确命名的 hook，并向原函数转发两个参数。
- `extern/CommonLibF4PreNG/CommonLibF4/include/RE/FO4Runtime.h` - 修正 PostNG runtime namespace 中 ID `2276833` 的语义名称。
- `extern/CommonLibF4PostNG/CommonLibF4/include/RE/FO4Runtime.h` - 修正 PostNG runtime namespace 中 ID `2276833` 的语义名称。
- `extern/CommonLibF4PostAE/include/RE/FO4Runtime.h` - 修正 PostNG runtime namespace 中 ID `2276833` 的语义名称。

### 主要变更

- 保留 PreNG 真正的 `Renderer_ResetState` ID `153957`。
- 将 PostNG/PostAE 的 ID `2276833` 统一命名为 `DEFERRED_RENDERER_BEGIN`。
- 将 thunk 签名从 `void(void*)` 改为 `void(void*, uint32_t)`，原样转发 `a_windowID`。

## 3. 遇到的错误

### 错误 1: Renderer hook ABI mismatch

**严重程度**: 严重

**错误信息**: PostAE `0xC0000005` at `Fallout4.exe+0x1817F9B`。

**上下文**: `Renderer::Begin` 使用第二参数按 80-byte stride 访问 renderer-window table；旧 thunk 调用原函数时没有提供该参数。

**解决方案**: 依据静态反编译和 CommonLib 类型定义恢复真实函数语义及完整签名。

## 4. 根本原因分析

### 为什么会出现这个错误?

- relocation 的地址本身可解析，但符号被错误标注为无窗口参数的 `ResetState`。
- detour 类型由 thunk 推导，错误声明使 trampoline 调用合法编译，却破坏运行时寄存器参数。
- 原函数随后把无效的 `EDX` 当作窗口索引，最终越界读取。

### 是什么导致编写时出现这个错误?

- 仅验证 REL ID 是否存在，没有同时验证目标函数 prologue、参数使用和 CommonLib 声明。
- 三个 runtime header 复制了同一错误语义名称，使错误看起来像跨版本一致事实。
- build 无法检测 Windows x64 下参数数量错误的 detour ABI。

## 5. 调试过程

### 调查步骤

1. 读取最新 `CommunityShaders.log`，确认初始化已到 MainMenu/Present，且 MRT override 尚未触发。
2. 从 Windows Application Error 提取异常码和模块内偏移。
3. 用 PostAE 静态导出与反汇编把 `0x1817F9B` 映射到 `Renderer::Begin` 的窗口表访问。
4. 对比 hook 声明、trampoline 调用和三个 runtime header，确认第二参数丢失。
5. 修正签名后构建 PreNG、PostNG、PostAE，并部署匹配 DLL。

### 迭代过程

- 初始现象包含 `nvngx_update.exe`，但 WER faulting module 与 CS 日志均不支持把 updater 作为根因。
- 将调查边界收窄到固定 Fallout4.exe 偏移后，ABI mismatch 能同时解释稳定地址与 PostNG/PostAE 共性。

### 耗时统计

- 调查: 18 分钟
- 实现: 4 分钟
- 测试: 8 分钟

## 6. 经验总结

### 核心洞察

- relocation “能解析”不等于函数语义和 ABI 正确。
- detour 必须验证所有寄存器参数；Windows x64 下少一个整型参数仍可能静态编译成功。
- companion process 的出现只是相关性，faulting module、异常偏移和调用边界才是根因证据。

### 预防策略

- 新增 renderer relocation 时记录函数原型、关键参数寄存器和至少一处参数消费指令。
- 对共享 runtime ID 做跨版本命名审计，不以复制后的相同名称作为独立证据。
- runtime crash 修复必须保留 build proof 与 deployed runtime proof 的明确分界。

### 识别的最佳实践

- 先读最新运行日志，再查 WER/崩溃偏移，最后映射到静态代码。
- hook thunk 与原函数使用同一强类型签名，所有参数显式命名并转发。
- 修改共享 hook 后构建全部 runtime target，避免旧版条件编译回归。

## 7. 知识提炼

### 可复用模式

- `fixed module offset -> disassembly parameter use -> detour signature` 是定位 native hook 崩溃的高效证据链。
- 对数组/表访问崩溃，优先回溯 index 的 ABI 来源，而不是只检查 table base pointer。

### 应避免的反模式

- 根据猜测的函数名定义 thunk。
- 用 `try/catch` 代替 ABI 正确性；访问冲突不会被普通 C++ 异常边界可靠修复。
- 把构建成功表述为游戏运行已修复。

### 类似任务检查清单

- [x] 核对 REL ID 对应函数体。
- [x] 核对 RCX/RDX/R8/R9 参数消费。
- [x] 核对 thunk、trampoline 与原函数签名一致。
- [x] 构建所有受共享 header 影响的 target。
- [x] 比对 built/deployed DLL SHA-256。
- [ ] 用新 DLL 完成 PostNG/PostAE 实机启动复测。

## 8. 测试与验证

### 测试用例

- PostNG RelWithDebInfo `/WX` 构建：通过。
- PostAE RelWithDebInfo `/WX` 构建：通过。
- PreNG RelWithDebInfo `/WX` 回归构建：通过。
- PostNG/PostAE built/deployed DLL SHA-256：分别匹配。
- PostNG/PostAE 进入主菜单与稳定运行：待实机验证。

### 验证步骤

1. 启动对应 MO2 profile 并进入 MainMenu。
2. 确认日志包含 `Found REL::ID(2276833) for Renderer_Begin hook`。
3. 保持运行并进入游戏世界，确认 Present 计数继续推进。
4. 检查 WER 不再出现 `Fallout4.exe+0x1817F9B`。

## 9. 参考资料

- PostAE Windows Application Error：`0xC0000005`, `Fallout4.exe+0x1817F9B`。
- PostAE IDA export：`sub_141817E30`。
- `BSGraphics::Renderer::Begin(uint32_t)` CommonLib 声明与目标函数反汇编。

## 10. 指标

- 总错误数: 1
- 严重错误数: 1
- 调试迭代次数: 2
- 成功率: 80%
- 代码变动: +14 -14 行

---
**生成工具**: Codex
**技能**: commit-with-reflection v2.0
