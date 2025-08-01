# NewICD3 - Universal IC Simulator

## 项目概述 (Project Overview)

NewICD3 是一个采用**分层解耦**设计的通用IC模拟器，实现了驱动透明性和插件化硬件仿真。通过内存保护机制和信号处理技术，驱动程序可以像访问真实硬件一样工作，无需任何修改。

NewICD3 is a universal IC simulator with **layered decoupled** architecture, implementing driver transparency and pluggable hardware simulation. Through memory protection mechanisms and signal processing technology, drivers can work like accessing real hardware without any modifications.

## 架构设计 (Architecture Design)

### 分层架构 (Layered Architecture)

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)           │
│              系统初始化 + 测试用例执行                   │
│                main.c - System init + Test execution    │
└─────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────┐
│                   驱动层 (Driver Layer)                 │
│      符合CMSIS 编码风格的Driver +  register 定义        │
│    CMSIS-compliant drivers + register definitions       │
│           (透明的寄存器访问 + 中断处理)                  │
│         (Transparent register access + IRQ handling)    │
└─────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────┐
│              接口层 (Interface Layer)                   │
│     内存保护 + 信号处理 + 寄存器映射 + 中断管理          │
│   Memory protection + Signal handling + Register        │
│              mapping + Interrupt management             │
└─────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────┐
│               设备模拟器层 (Device Model Layer)          │
│           使用python + 硬件行为模拟                     │
│          Python-based + Hardware behavior simulation    │
└─────────────────────────────────────────────────────────┘
```

### 核心组件 (Core Components)

#### 1. 接口层 (Interface Layer)
- **对接C语言驱动**: 驱动调用读写操作接口，转接到模型层的寄存器和memory读写操作
- **对接Python模型层**: 当模型有异常或中断发生，调用中断接口反馈到驱动
- **通信协议**: 使用socket通信，协议包含发起方设备ID、操作命令、地址、长度、数据、执行结果
- **内存保护**: 通过预设置设备寄存器和memory的访问属性(mmap + PROT_NONE)，当发生读写时进入handler处理
- **📢 NEW: memset支持**: segv_handler现在支持REP STOS* 指令族，完整支持memset批量内存操作

#### 2. 通信协议 (Communication Protocol)
```c
typedef struct {
    uint32_t device_id;     // 设备ID
    protocol_command_t command;  // 操作命令 (读/写/中断)
    uint32_t address;       // 地址
    uint32_t length;        // 长度
    protocol_result_t result;    // 执行结果
    uint8_t data[256];      // 数据负载
} protocol_message_t;
```

### 设计原则 (Design Principles)

1. **解耦 (Decoupling)**
   - 支持多种驱动风格 (CMSIS, MCAL)
   - 不依赖特定设备模型

2. **向后扩展 (Backward Compatibility)**
   - 支持多个驱动
   - 支持多个设备

## 构建和测试 (Build and Test)

### 构建系统 (Build System)
```bash
# 构建所有目标
make all

# 运行单元测试
make test

# 运行集成测试
make integration-test

# 清理构建产物
make clean

# 显示帮助
make help
```

### 目录结构 (Directory Structure)
```
NewICD3/
├── include/                # 头文件
│   ├── interface_layer.h   # 接口层API
│   └── device_driver.h     # 驱动层API
├── src/
│   ├── interface_layer/    # 接口层实现
│   ├── driver_layer/       # 驱动层实现
│   ├── app_layer/          # 应用层实现
│   └── device_models/      # Python设备模型
├── tests/                  # 测试代码
├── build/                  # 构建产物
├── bin/                    # 可执行文件
└── Makefile               # 构建配置
```

## 使用示例 (Usage Examples)

### 1. 基本使用
```c
#include "interface_layer.h"
#include "device_driver.h"

int main() {
    // 初始化接口层
    interface_layer_init();
    
    // 初始化设备驱动
    device_init();
    
    // 使用设备
    device_enable();
    device_write_data(0x12345678);
    
    uint32_t data;
    device_read_data(&data);
    
    // 清理
    device_deinit();
    interface_layer_deinit();
    
    return 0;
}
```

### 2. 寄存器访问
```c
// 直接寄存器访问
DEVICE->CTRL |= DEVICE_CTRL_ENABLE_Msk;
uint32_t status = DEVICE->STATUS;

// 通过接口层访问
write_register(0x40000000, 0x1, 4);
uint32_t value = read_register(0x40000000, 4);
```

### 3. 中断处理
```c
void my_interrupt_handler(uint32_t device_id, uint32_t interrupt_id) {
    printf("Interrupt received from device %d: %d\n", device_id, interrupt_id);
}

// 注册中断处理器
register_interrupt_handler(1, my_interrupt_handler);

// 触发中断 (通常由设备模型调用)
trigger_interrupt(1, 0x10);
```

## 📢 memset 支持 (memset Support)

NewICD3 现在完整支持 memset 批量内存操作！segv_handler 已扩展支持 REP STOS* 指令族。

### 支持的指令 (Supported Instructions)
- **REP STOSB** (0xF3 0xAA) - 8位内存填充
- **REP STOSW** (0xF3 0x66 0xAB) - 16位内存填充  
- **REP STOSD** (0xF3 0xAB) - 32位内存填充
- **REP STOSQ** (0xF3 0x48 0xAB) - 64位内存填充

### 使用示例 (Usage Examples)
```c
// 基本 memset 操作 - 现在完全支持！
uint8_t *device_buffer = (uint8_t *)0x40000000;
memset(device_buffer, 0xAA, 1024);  // ✅ 现在可以工作！

// 零填充操作
struct uart_regs *uart = (struct uart_regs *)0x40000000;
memset(uart, 0, sizeof(*uart));     // ✅ 设备寄存器初始化

// DMA 缓冲区设置
uint8_t *dma_buffer = (uint8_t *)0x50000000;
memset(dma_buffer, 0xFF, DMA_BUFFER_SIZE);  // ✅ 批量填充
```

### 特性 (Features)
- ✅ **透明支持**: 无需修改现有代码
- ✅ **完整覆盖**: 支持所有 REP STOS* 指令变体
- ✅ **批量转换**: REP 操作转换为独立的设备模型写操作
- ✅ **寄存器管理**: 正确更新 RDI、RCX、RIP 寄存器状态
- ✅ **向后兼容**: 保持与现有功能的完全兼容性

### 演示程序 (Demonstration)
```bash
# 编译并运行 memset 演示
make clean && make
./demo_memset
```

详细信息请参阅 [MEMSET_SUPPORT.md](MEMSET_SUPPORT.md)

## 测试框架 (Test Framework)

### 单元测试 (Unit Tests)
- 接口层初始化/反初始化测试
- 设备注册/注销测试
- 寄存器读写测试
- 中断处理测试
- 协议消息测试

### 集成测试 (Integration Tests)
- 完整的驱动初始化流程
- 设备操作测试
- 中断处理集成测试
- 直接寄存器访问测试

## 扩展性 (Extensibility)

### 添加新设备驱动
1. 在 `src/driver_layer/` 创建新的驱动文件
2. 实现符合CMSIS风格的API
3. 在 `include/` 添加对应的头文件
4. 更新Makefile构建配置

### 添加新设备模型
1. 在 `src/device_models/` 创建Python模型
2. 实现socket通信协议
3. 模拟设备的硬件行为
4. 支持寄存器读写和中断生成

## 依赖要求 (Dependencies)

### 编译环境
- GCC 编译器
- Make 构建工具
- POSIX兼容系统 (Linux/Unix)

### 运行时依赖
- Python 3.x (用于设备模型)
- Unix domain socket 支持

## 许可证 (License)

MIT License - 详见LICENSE文件

## 贡献指南 (Contributing)

1. Fork 项目
2. 创建特性分支
3. 提交更改
4. 推送到分支
5. 创建Pull Request

## 联系方式 (Contact)

项目维护者: CjieSun
GitHub: https://github.com/CjieSun/NewICD3

---

## Recent Interface Layer Improvements

### Key Changes Implemented

1. **File Naming Updates**
   - `interface_layer.c` → `driver_interface.c`
   - `simple_device_model.py` → `model_interface.py`

2. **Enhanced SEGV Handler**
   - Added x86 instruction parsing for read/write detection
   - Supports instruction types: 0x8B (read), 0x89 (write), 0xC7 (write immediate)
   - Uses RIP register to examine instruction bytes at fault address

3. **Corrected Interrupt Flow**
   - **Before**: Driver-to-driver interrupt triggering (incorrect)
   - **After**: Python model-to-C driver interrupt triggering (correct direction)

4. **Socket Communication Enhancement**
   - Bidirectional communication between Python models and C driver
   - Interrupt delivery from Python to C driver interface
   - Model interface can trigger interrupts to driver interface

### x86 Instruction Detection in SEGV Handler

The enhanced `segv_handler` now detects instruction types by examining RIP:

| Instruction Byte | Assembly | Type | Description |
|-----------------|----------|------|-------------|
| 0x8B | `mov reg, [mem]` | READ | Move from memory to register |
| 0x89 | `mov [mem], reg` | WRITE | Move from register to memory |
| 0xC7 | `mov [mem], imm32` | WRITE | Move immediate to memory |

### Integration Demo

Run the integration demo to see Python model triggering interrupts to C driver:

```bash
# Build the system first
make clean && make

# Run the demo
python3 demo_integration.py
```

This demonstrates the corrected interrupt flow where Python device models trigger interrupts that are received by the C driver interface, which is the proper direction for hardware simulation.