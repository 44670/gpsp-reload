# PLAN.md — 基于 gpSP 源码重新实现更好的 JS JIT 后端（Emscripten 纯 JS，不使用 Wasm）

## 0. 这份计划的定位

这份计划只讨论你上传的 **gpSP** 源码本身，不再把 QEMU、TCG 之类的通用动态翻译器当作主设计参照。对这份仓库而言，真正需要尊重的“事实来源”有五个：

1. `cpu.cc`：ARM7TDMI 语义、模式切换、IRQ、访存快慢路径、块传送语义的真值来源。
2. `cpu_threaded.c`：gpSP 现有 dynarec 的块扫描、块缓存、RAM/ROM 分治、BIOS hook、translation gate、分支回填逻辑的真值来源。
3. `gba_memory.c` / `gba_memory.h`：内存布局、ROM 分页、DMA、open-bus、BIOS 读取限制、自修改代码（SMC）信号的真值来源。
4. `main.c`：`update_gba()` 硬件事件调度、CPU 让出时机、frame 结束信号、DMA 睡眠/唤醒的真值来源。
5. `libretro/libretro.c`：一帧级入口和 `execute_arm_translate()` / `execute_arm()` 的调用契约。

如果一个“JS 版 dynarec”没有把这五个层面接起来，那么它得到的最多只是“一个能把 ARM 小程序翻译成 JS 的原型”，而不是“gpSP 的 JS 后端”。

这也是我对前一版计划的纠偏：**之前把注意力放在通用 JIT 设计上是不对的，新的计划必须以 gpSP 现有结构为中心。**

---

## 1. 先给结论：应该保留什么，应该重写什么

### 1.1 必须保留的，不应随意改语义

以下内容属于 gpSP 运行正确性的基础，应尽量原样保留其语义，而不是“抽象后再猜”：

- `main.c` 里的 `update_gba()` 事件循环与返回值协议。
- `cpu.cc` 里的 CPU 状态布局、模式切换、IRQ 进入、CPSR/SPSR 处理。
- `gba_memory.c` 里的 BIOS/open-bus/ROM 越界/VRAM mirror/EEPROM/flash/RTC/GPIO 行为。
- `gba_memory.c` 里的 32KB `memory_map_read` 页表与 gamepak page swap 机制。
- `gba_over.h` / `load_game_config_over()` 的 `idle_loop_target_pc`、`translation_gate_target_pc[]`、存储介质/RTC/rumble/serial override。
- RAM 自修改代码的检测语义：IWRAM/EWRAM 的 shadow/mirror 标记，以及写入后触发 invalidation 的时机。

### 1.2 应该彻底重写的

以下内容不应原样照搬到 JS，因为它们是为 native emit / 汇编 stub 定制的：

- `cpu_threaded.c` + `x86_emit.h` / `arm_emit.h` / `mips_emit.h` 这种 **“前端扫描 + 语义判断 + 后端寄存器分配 + 机器码发射”** 全部靠宏直接耦合的结构。
- 以“可执行内存 buffer + patch 原始跳转地址”为中心的缓存模型。
- 依赖 host 汇编 stub 的 flags 提取、alert 处理、sleep loop、lookup/jump ABI。
- `rom_translation_cache` / `ram_translation_cache` 内嵌 header、offset、链表头的内存打包技巧本身。

**要保留的是行为，不是现有 C 宏和可执行 buffer 的物理形态。**

---

## 2. 基于源码的现状解剖

### 2.1 构建层：gpSP 当前是怎么拼起来的

`Makefile.common` 明确显示：

- `cpu.cc` 永远参与构建。
- `HAVE_DYNAREC=1` 时，额外编译 `cpu_threaded.c`。
- 同时根据宿主架构再选一个 backend stub：
  - `x86/x86_stub.S`
  - `arm/arm_stub.S`
  - `arm/arm64_stub.S`
  - `mips/mips_stub.S`

这说明当前 gpSP 结构不是“一个前端 + 多个松耦合 backend”，而是：

- 解释器：`cpu.cc`
- 动态翻译器前端：`cpu_threaded.c`
- 动态翻译器后端：emit 头文件 + 宿主汇编 stub

也就是说，**JS 不是简单再加一个 `#elif defined(JS_ARCH)` 就能放进去的宿主架构**。如果强行这么做，只会把原本面向汇编机器码发射的宏体系复制成另一套更难维护的字符串宏体系。

### 2.2 运行入口：一帧里 CPU 是怎么被驱动的

`libretro/libretro.c` 在每帧里做的事情非常直接：

- 如果启用 dynarec：`execute_arm_translate(execute_cycles);`
- 否则：`clear_gamepak_stickybits(); execute_arm(execute_cycles);`

这意味着当前 dynarec 的外部契约不是“跑到某个 guest 函数返回”，而是：

- 以 `execute_cycles` 为预算；
- 跑 CPU；
- 在需要时调用/配合 `update_gba()`；
- 一帧结束再返回主线程。

`main.c` 里 `init_main()` 在 dynarec 模式下还会做两件关键事情：

- `init_dynarec_caches();`
- `init_emitter(gamepak_must_swap());`

因此新实现不能只替换一段翻译器，而必须明确：

1. **缓存什么时候初始化**
2. **CPU 运行什么时候返回给外层**
3. **scheduler / video / DMA / timers / IRQ 怎样保持原契约**

### 2.3 `update_gba()` 是 CPU 后端的真正宿主契约

`main.c` 的 `update_gba(int remaining_cycles)` 非常重要。它处理：

- timer 递减与 timer IRQ
- serial IRQ
- HBlank / VBlank / VCount 事件
- scanline 更新
- HBlank/VBlank DMA
- IRQ 标志上报与 `check_and_raise_interrupts()`
- `execute_cycles` 下一段预算计算
- DMA 导致的 `CPU_DMA` 睡眠
- frame 完成信号（最高位）

这说明任何新的 JS dynarec 都不能把自己当成“独立 CPU 世界”，它必须遵守 gpSP 现有的 **run-until-event** 模型。

native dynarec 的汇编 stub 已经把这个契约写死了：

- 写后 epilogue 里可能做 SMC flush、IRQ raise、HALT/sleep loop。
- `cpu_sleep_loop` 中调用 `update_gba()`。
- `execute_arm_translate_internal` 进入时先处理 HALT，再 `lookup_pc` 找 block。

**新的 JS 设计要做的是在 JS 层重新表达这套 contract，而不是发明一套新的控制流。**

### 2.4 CPU 状态：`cpu.h` / `cpu.cc` 给出的真实 ABI

`cpu.h` 里有几个必须遵守的点：

- 架构寄存器 `r0-r15`，`REG_PC = 15`
- `REG_CPSR = 16`
- `CPU_MODE = 17`
- `CPU_HALT_STATE = 18`
- `REG_BUS_VALUE = 19`
- 后面还有 dynarec 用的伪寄存器和 spill 槽位

`cpu.cc` 中 `set_cpu_mode()` 实现了 banked registers 与 FIQ bank 交换逻辑；`check_and_raise_interrupts()` 负责进入 IRQ 模式并跳到 BIOS IRQ handler；`init_cpu()` 初始化 boot state 和各 mode SP。

这给新实现两个明确要求：

1. **新后端必须与现有 `reg[]` / `spsr[]` / `reg_mode[][]` 兼容，至少在桥接层上兼容。**
2. **如果内部想用更适合 JS 的状态表示，也必须有无损映射回 gpSP 当前状态布局。**

### 2.5 `cpu.cc` 才是语义真值，不是 `cpu_threaded.c`

`cpu_threaded.c` 主要解决的是“怎么切 block / 怎么发射 host code / 怎么 patch 跳转”，但指令语义真正完整、细节最多的是 `cpu.cc`。

特别是这些区域应被视为真值：

- `fast_read_memory` / `fast_write_memory`
- `load_aligned32` / `store_aligned32`
- LDM/STM helper：`exec_arm_block_mem()`、`exec_thumb_block_mem()`
- mode switch、IRQ、CPSR/SPSR 处理
- idle loop target 处理
- open-bus / BIOS 访问限制配合 `gba_memory.c`

这意味着新的 JS backend 最稳妥的路线不是“从现有 native emit 宏倒推语义”，而是：

- 以 `cpu.cc` 为指令与 helper 语义参照；
- 以 `cpu_threaded.c` 为块形成与缓存行为参照；
- 以 `gba_memory.c` / `main.c` 为外围系统参照。

### 2.6 内存模型：这部分比“翻译 ARM 指令”更重要

`gba_memory.c` / `gba_memory.h` 中有几个关键结构必须理解清楚。

#### 2.6.1 `memory_map_read` 是 32KB 页表

`init_memory()` 用 `map_region()` / `map_vram()` / `map_null()` 初始化 `memory_map_read`。索引单位是 `address >> 15`，也就是 **32KB 页**。这不是 incidental detail，而是：

- PC fetch 走它；
- ROM page swap 走它；
- DMA segmented gamepak 访问也走它；
- fast memory path 也依赖它。

#### 2.6.2 IWRAM 和 EWRAM 不是单纯的数据区，它们有 shadow 区

源码里实际布局是：

- **IWRAM**
  - `iwram[0x8000..0xFFFF]`：真实数据
  - `iwram[0x0000..0x7FFF]`：SMC mirror / code tag 区
- **EWRAM**
  - `ewram[0x00000..0x3FFFF]`：真实数据
  - `ewram[0x40000..0x7FFFF]`：SMC mirror / code tag 区

这一点在很多地方都有体现：

- `init_memory()` 读映射 IWRAM 用的是 `&iwram[0x8000]`
- `cpu_threaded.c` RAM tag 查找直接访问这两块 shadow 区
- `gba_memory.c` 的 DMA 写入在 IWRAM/EWRAM 时会检查 shadow 非零并设置 `CPU_ALERT_SMC`
- native store stubs 写 RAM 后也会检查 shadow

这不是一个可以轻易“换成另一个更现代结构”的细节，而是 gpSP 当前 RAM code invalidation 机制的根。

#### 2.6.3 ROM 是可换页的，不一定整包常驻

`load_gamepak_page()` 与 `evict_gamepak_page()` 说明：

- gamepak 用 32KB 页加载
- 有 LRU queue
- sticky bit 页面不轻易驱逐
- 页表映射由 `memory_map_read` 回填

因此任何新设计都不能默认“整个 ROM 一次性映射好了并一直稳定存在”，至少在兼容性优先阶段不能这么假设。

### 2.7 当前 dynarec 的块缓存其实是两套系统，不是一套

#### 2.7.1 ROM/BIOS/gamepak：hash 表 + 链表

`cpu_threaded.c` 的 `block_lookup_translate_builder()` 中：

- 对 region 0x00、0x08..0x0D：
  - key = `pc | thumb`
  - 用 `rom_branch_hash[]` 哈希
  - 碰撞链表 header 直接塞在 `rom_translation_cache` 里
  - miss 时 `translate_block_arm/thumb(pc, false)`

#### 2.7.2 RAM：shadow tag -> `ramtag_type` -> offset

对 region 0x02 / 0x03：

- 从 EWRAM/IWRAM shadow 区读 tag
- tag 对应 RAM cache 尾部的 `ramtag_type`
- `ramtag_type` 里有 `offset_arm` / `offset_thumb`
- miss 时 `translate_block_arm/thumb(pc, true)`

这说明“当前 gpSP 的 dynarec cache”并不是一个统一的映射层，而是：

- ROM：相对长期、基于 guest PC 的 hash cache
- RAM：基于镜像 tag 的易失性 cache

新的设计必须承认这一点。**JS 后端应当统一接口，但不能把 ROM 和 RAM 当成相同的失效问题。**

### 2.8 自修改代码（SMC）不是抽象问题，而是现成机制

`cpu_threaded.c` 在扫描 RAM block 时会把相应的 shadow 位置写成 `CODE_TAG_BLOCK32` 或 `CODE_TAG_BLOCK16`。之后：

- CPU 通过 native store stub 写 IWRAM/EWRAM 时会检查 shadow 非零
- DMA 写 IWRAM/EWRAM 时也会检查 shadow 非零
- 一旦发现写入命中代码区域，就产生 `CPU_ALERT_SMC`
- 汇编 stub / 上层再调用 `flush_translation_cache_ram()`

`flush_translation_cache_ram()` 不是简单全 memset，它还会利用 `iwram_code_min/max` 与 `ewram_code_min/max` 尽量缩小清理范围，然后重置 `ram_block_tag`。

这给出两个重要教训：

1. **gpSP 已经把“代码是否在 RAM 中存在”编码在 shadow memory 里。**
2. **它现在的 invalidation 粒度很粗——几乎是 flush 整个 RAM translation cache——这正是新设计可以改进的地方。**

### 2.9 block 扫描逻辑里藏着很多 gpSP 特有行为

`scan_block()` 并不只是“扫到 branch 就停”那么简单。它还会处理：

- exit points 分类
- direct / indirect branch target 记录
- SWI 进入 BIOS 0x8
- 条件码相关 metadata
- `translation_gate_target_pc[]` 命中时强制截断 block
- `MAX_BLOCK_SIZE`
- 特定地址终止（如 0x3007FF0 / 0x203FFFF0）
- RAM block 的 SMC tag 标记

另外：

- ARM 有 `arm_dead_flag_eliminate()`，但当前只是 `flag_status = 0xF`，也就是没做真正消除
- Thumb 有真实的 flag liveness 分析

这说明 gpSP 现有 dynarec 前端已经混入了很多“面向特定 backend 的优化”以及“历史兼容逻辑”。新设计需要把这些行为重新分类，而不是一股脑照抄。

### 2.10 BIOS hook、translation gate、cheat hook 都是必须保留的特殊路径

这三类逻辑不能丢：

1. **BIOS SWI 入口**
   - `init_bios_hooks()` 会预先编译 PC=0x8 的 block，并通过 `rom_cache_watermark` 保护它免受 ROM cache flush 影响。
   - 这意味着 BIOS SWI 入口在 gpSP 的 dynarec 中是“常驻入口”。

2. **translation gate**
   - 来自 `gba_over.h` 的每游戏配置。
   - block 扫描遇到目标地址时强制结束 block。
   - 这是兼容性补丁，不是性能优化小技巧。

3. **cheat master hook**
   - block 翻译过程中如果 `pc == cheat_master_hook` 要插入特定 hook。

任何“更好”的实现，如果把这些都扔掉，那么即使架构更优雅，也不是 gpSP-compatible。

---

## 3. 现有 JS 原型给出的经验与教训

这里的“现有 JS 原型”指之前做出来的那条验证路线：ARM guest -> JS basic block JIT -> Node 运行 Dhrystone。

### 3.1 证明了什么

它至少证明了三件事：

1. **“把 ARM guest block 翻译成 JS 字符串，再交给 JS 引擎 JIT” 这条路本身是可行的。**
2. **Node/V8 可以稳定执行这类 block 函数，且在小基准上能通过状态校验。**
3. **block cache + direct block translation + helper fallback 的总体结构是对的。**

这很重要，因为它说明目标方向不是空想。

### 3.2 没有证明什么

它没有证明以下事情：

- 它没有证明 gpSP 的 `update_gba()` 契约已经正确接入。
- 它没有证明 gpSP 的 memory map / BIOS / open-bus / DMA / backup / GPIO 行为已经一致。
- 它没有证明 RAM SMC、translation gates、BIOS hook、cheat hook 已被按 gpSP 方式处理。
- 它没有证明 libretro 一帧运行路径已被完整打通。

因此，**那个原型只能算“guest ARM -> JS JIT 的可行性验证”，不能算“gpSP JS dynarec 已集成完成”。**

### 3.3 最重要的教训

最重要的教训是：

> 对 gpSP 而言，真正困难的不是“把一条 ARM 指令翻成 JS”，而是“把翻译后的 block 嵌回 gpSP 既有的 CPU/内存/事件/兼容 hack 契约中”。

所以接下来的重实现不应围绕“继续扩大小原型”，而应围绕“如何在 gpSP 现有系统内重建一套更干净的 dynarec 分层”。

---

## 4. 设计目标

新的实现目标应该写得非常具体：

### 4.1 目标一：纯 JS backend

- CPU 动态翻译产物必须是 **JS 代码**，不是 Wasm。
- 可以使用 Emscripten，但它只负责把 gpSP 其余 C/C++ 核心编译成 JS 宿主环境。
- dynarec 自身的 backend 仍然应是“guest ARM/Thumb -> JS block function”。

### 4.2 目标二：兼容 gpSP 现有系统

新后端必须能够挂接到 gpSP 现有的：

- `main.c` 事件循环
- `gba_memory.c` 内存/IO/backup/ROM paging
- `cpu.cc` 语义 helper
- `libretro` 帧驱动

### 4.3 目标三：比现有 native dynarec 更可维护

具体意味着：

- 前端扫描与后端发射解耦
- 语义与优化解耦
- helper 接口清晰
- block metadata 可观测
- invalidation 不再只能“整块 RAM cache flush”
- 测试体系可以对 interpreter 做差分

### 4.4 目标四：保留 fallback

任何阶段都必须保留：

- 解释器 fallback
- 稀有/复杂指令 helper fallback
- CSP/禁用 `new Function` 环境的 fallback（至少 Node/非 CSP 环境正常，浏览器可降级）

---

## 5. 推荐的总体路线：分成两层，而不是一步到位全改

我推荐 **两层路线**，并且明确说明哪条是主线。

### 5.1 主线（推荐）：兼容性优先路线

用 Emscripten 的纯 JS 输出把 gpSP 其余核心编译成 JS，然后让 **JS dynarec 直接操作 Emscripten JS heap**。

这条路线的好处：

- `main.c`、`gba_memory.c`、`video.cc`、`sound.c`、`serial.c` 等已有逻辑可以继续用。
- `cpu.cc` 可以继续作为解释器与 helper 语义来源。
- JS dynarec 只需要替换 CPU 执行后端，不需要一次性重写整个模拟器。
- 仍然满足“JS，不是 Wasm”。

这也是最贴近你最初要求“用 emscripten，是 js，不是 wasm”的工程路线。

### 5.2 次线（长期）：完全 JS 化路线

把 CPU、memory bridge、scheduler 乃至更多外围逻辑都逐步转成手写 JS。

这条路线长期可能更漂亮，但工程风险显著更高。对于“先做出一个更好的 gpSP JS 版本”而言，不应作为第一阶段主线。

**结论：第一阶段应该优先做兼容性优先路线。**

---

## 6. 新架构应该怎么拆

### 6.1 不再使用 `CPU_ARCH = js` 这种思维

当前构建系统把 dynarec 选择和宿主 CPU 架构绑在一起，这是 native backend 的历史产物。新的设计应改成：

- `CPU_FRONTEND = arm7tdmi`
- `CPU_BACKEND = interp | native_x86 | native_arm | native_arm64 | native_mips | jsjit`

也就是说，**JS backend 是一种 CPU 执行后端，不是一种宿主机器架构。**

这会直接改善工程结构：

- `cpu.cc` 继续作为 interpreter/reference
- `cpu_threaded.c` 不再直接决定 backend
- `libretro/libretro.c` / 新调度层只看 backend 类型

### 6.2 新模块划分

建议新加以下模块（名称可调整，职责不要变）：

#### A. `cpu_backend_api.h/.c`
统一定义 CPU backend 接口，例如：

- `cpu_backend_init()`
- `cpu_backend_reset()`
- `cpu_backend_execute(cycles)`
- `cpu_backend_flush_rom_cache()`
- `cpu_backend_flush_ram_cache()`
- `cpu_backend_invalidate_ram_range(addr, size)`

这样上层不再直接依赖 `execute_arm_translate()` 这个 native dynarec 名字。

#### B. `jsjit_bridge.c`
C 到 JS 的桥接层，负责导出：

- `reg[]`、`spsr[]`、`reg_mode[][]`
- `ewram`、`iwram`、`vram`、`io_registers`
- `memory_map_read`
- helper wrapper：
  - `read_memory8/16/32`
  - `write_memory8/16/32`
  - `check_and_raise_interrupts`
  - `flag_interrupt`
  - `update_gba`
  - `execute_swi_arm/thumb`
  - `execute_store_cpsr`
  - `execute_spsr_restore`
  - `load_gamepak_page`
  - 其他必要 helper

#### C. `jsjit_runtime.js`
JS 运行时总控：

- block cache
- dispatcher
- compiler API (`compileSource`)
- helper bridge
- fast path memory access
- RAM invalidation
- profile counters

#### D. `jsjit_decode.js`
ARM/Thumb decoder metadata 层：

- decode tables
- opcode 分类
- 每条指令的 flags use/def 信息
- `mayExit` / `mayWritePC` / `needsHelper` 等属性

#### E. `jsjit_scan.js`
block scanner：

- 从 entry PC 出发
- 遵守 gpSP 当前 block 结束条件
- 记录 exits、内部 target、translation gates、cheat hook、idle 相关 metadata

#### F. `jsjit_ir.js`
中间表示（IR）：

- 不需要做复杂 SSA
- 但必须提供比“直接拼 JS 字符串”更稳定的一层语义载体

#### G. `jsjit_emit.js`
IR -> JS source / JS function 的 emitter。

#### H. `jsjit_tests/`
Node harness、差分测试、block trace、ROM smoke 测试。

---

## 7. 为什么必须引入 IR，而不能直接把 `cpu_threaded.c` 改成字符串发射器

这是这次重实现最关键的架构决策之一。

### 7.1 当前 `cpu_threaded.c` 的问题不在“代码写得老”

它的问题在于：**扫描、分析、优化、发射、patch、cache 管理都混在一起。**

具体表现为：

- include backend emit header 的那一刻，前端和 backend 已经不可分。
- block 扫描过程直接依赖 backend 提供的很多宏。
- second pass 里 instruction translation 直接生成 host code。
- 内部分支 patch / 外部 block lookup 都是按“宿主机器码地址”思维写的。

### 7.2 JS backend 与 native backend 的控制流模型不一样

native backend 的假设：

- 有可执行 buffer
- 可以拿到原始地址
- 可以回填原始跳转位移
- 可以靠汇编 stub 管 flags、stack、寄存器、返回地址

JS backend 不具备这些条件。它更适合的模型是：

- block 编译成 JS function
- block 之间通过 dispatcher 循环衔接，而不是递归互跳
- link 通过缓存的 block id / function 引用完成，而不是 patch 原始机器码偏移
- flags 在 block 内用局部变量懒计算，helper/exit 时再 collapse

这意味着如果不引入 IR，而是直接把 `cpu_threaded.c` 现有 emit 宏改成 JS 字符串发射，只会得到一套更脆弱的架构。

### 7.3 建议的 IR 够用即可，不要过度设计

这里不需要引入复杂 SSA IR。足够的做法是定义一套 **线性 block IR**，每条 IR 记录：

- 操作类型（ALU、shift、load/store、branch、helper call、cpsr op、swi、block mem 等）
- 源/目的寄存器
- 立即数
- 地址模式
- flags use/def
- cycle cost
- 可能的退出类型
- 需要 slow helper 的原因

它的作用不是为了炫技，而是为了：

- 让 decode/scanning 与 emit 解耦
- 让测试可以在“翻译前/翻译后”看见语义
- 让优化（flag 消除、常量折叠、block splitting）有地方插入

---

## 8. 新后端的真实执行模型

### 8.1 不要让 block 之间直接递归调用

JS 里如果让 block function A 直接调 block function B，再调 C，再调 D，长帧运行很容易造成：

- 深调用栈
- deopt
- 调试困难
- 控制权难以统一回到 scheduler

因此推荐统一用 **trampoline / dispatcher loop**：

```js
while (true) {
  const block = lookup(pc, thumbMode);
  const rc = block(ctx);
  if (rc >= 0) {
    currentBlockId = rc;
    continue;
  }
  if (rc === EXIT_TO_SCHEDULER) { ... }
  if (rc === EXIT_FRAME_DONE) { ... }
  if (rc === EXIT_RELOOKUP) { ... }
}
```

### 8.2 block function 的职责

每个 compiled block 的职责应该是：

- 从当前 `pc` 作为入口执行到 block 结束
- 在 block 内直接操作 `reg[]` / heap / local flags
- 在需要时调用 helper
- 返回：
  - 下一个已链接 block id
  - 或者“重新 lookup”
  - 或者“让出给 scheduler”
  - 或者“frame 完成”

### 8.3 dispatcher 负责统一处理这些事情

dispatcher 负责：

- HALT / DMA sleep 入口处理
- `update_gba()` 调用
- `check_and_raise_interrupts()`
- block lookup / lazy compile
- cache invalidation 后重取 block
- frame 结束后返回外层

这相当于把 native stub 中的 ABI/control-flow 逻辑，重建成 JS 可维护版本。

---

## 9. 状态与内存桥接设计

### 9.1 推荐直接使用 gpSP 原有状态布局

对于兼容性优先阶段，JS runtime 不应该重新发明状态布局，而应直接桥接到现有 C/emscripten heap 中的：

- `reg[]`
- `spsr[]`
- `reg_mode[][]`
- `memory_map_read[]`
- `ewram` / `iwram` / `vram` / `io_registers` 等

这样好处很明显：

- JS backend 和 interpreter 可以直接用同一份真实状态做差分
- savestate 不需要额外做映射
- helper C 函数可以直接工作

### 9.2 但 block 内部可以使用更适合 JS 的局部变量

例如 flags：

- 外部状态仍然以 `reg[REG_CPSR]` 为准
- block 内可以拆成 `n`, `z`, `c`, `v` 局部变量
- 只在需要 helper/exit/PC-write/CPSR-write 时 collapse 回去

这样既兼容 gpSP，又不会被 `REG_N_FLAG` 等 native dynarec 历史伪寄存器束缚。

### 9.3 fast memory path 建议直接镜像 `cpu.cc`

`cpu.cc` 的 `fast_read_memory` / `fast_write_memory` 已经把正确性与快路径条件写清楚了。JS 版应直接按同一条件构造快路径，而不是拍脑袋简化。

例如 32 位读取快路径的判断应保留这些要点：

- 地址是否落在 fast map 可访问区域
- 是否对齐
- 是否是 BIOS 受限读取
- `memory_map_read[address >> 15]` 是否为 NULL

伪代码应当接近：

```js
function fastRead32(addr) {
  const region = addr >>> 24;
  if (addr < 0x10000000) cycles -= ws_cyc_nseq[region][1];
  const page = memoryMapRead[addr >>> 15];
  if (((region === 0) && regPC >= 0x4000) || (addr & 3) || !page) {
    return slowRead32(addr);
  }
  return HEAPU32[(page + (addr & 0x7fff)) >>> 2] >>> 0;
}
```

同理，写路径要保留写后 alert 语义。

### 9.4 重要：shadow 区布局必须在 bridge 层暴露清楚

开发者必须在桥接接口上明确暴露：

- EWRAM base pointer
- IWRAM base pointer
- 真实区和 shadow 区的偏移规则

否则 JS runtime 很容易错误地把 IWRAM 当成普通 32KB 数组，从而完全丢失 SMC 检测。

---

## 10. block 扫描器应如何重写

### 10.1 必须保留 gpSP 当前的 block 边界语义

新的 scanner 不能随意定义“什么是一个 block”，必须至少保留以下行为：

- unconditional branch 可能结束 block
- 条件 branch 记录 exit 但不一定立刻结束
- SWI 指向 BIOS 0x8
- translation gate 强制截断
- `MAX_BLOCK_SIZE`
- RAM block 时打 SMC code tag
- 特定地址边界条件（兼容性相关的历史逻辑）
- internal branch target 标记 `update_cycles` 或其等价概念

### 10.2 scanner 输出不应该是“直接发射代码”，而应是 `ScannedBlock`

建议定义一个结构：

```text
ScannedBlock
- entryPc
- mode (arm/thumb)
- ramRegion (bool)
- blockStartPc
- blockEndPc
- instructions[]
- exits[]
- internalTargets[]
- usesTranslationGate
- touchesCheatHook
- idleLoopHint
- codeTagRanges[]
```

### 10.3 重新分类哪些事情属于 scanner，哪些不属于

#### scanner 应做的
- 线性扫描 guest opcode
- 判断 exit point
- 记录块内控制流
- 记录 flag metadata
- 记录需要的 helper 类别
- 记录 code ranges / page ranges

#### scanner 不应做的
- 直接拼 JS 代码
- 决定宿主局部变量名字
- 直接做 branch patch
- 关心 `new Function` 还是 `vm.compileFunction`

---

## 11. IR 设计建议

### 11.1 以“足够表达 gpSP 语义”为标准，不做学术化 IR

推荐把 IR 分成几类：

1. `MOV`, `ALU`, `SHIFT`, `MUL`, `MULL`
2. `LOAD8/16/32`, `STORE8/16/32`
3. `LOAD_SIGNED`
4. `BRANCH_DIRECT`, `BRANCH_INDIRECT`, `CALL_HELPER`
5. `READ_CPSR`, `WRITE_CPSR`, `RESTORE_SPSR`
6. `BLOCK_MEM_ARM`, `BLOCK_MEM_THUMB`
7. `SWI`, `CHEAT_HOOK`, `TRANSLATION_GATE`
8. `CYCLE_FLUSH`, `FLAG_FLUSH`

### 11.2 对复杂指令，第一阶段不要强行内联

这是非常重要的工程纪律。

对于这些类别，第一阶段应优先 lower 到 helper，而不是急着手写内联 JS：

- ARM block memory（LDM/STM，特别是带 S 位、PC、writeback 组合）
- CPSR/SPSR 恢复路径
- SWI / BIOS hook
- 特殊 IO/backup/GPIO 相关访存
- 某些复杂乘法长指令
- 稀有未定义/边缘行为

原因很直接：

- `cpu.cc` 里这类语义已经存在，而且有历史兼容经验
- 现有源码自己也在这些点上留有 TODO
- 先用 helper 保正确，再按 profile 选热点内联，效率更高

### 11.3 flags 优化应当对 ARM 和 Thumb 统一设计

当前 gpSP：

- ARM：没做真正 dead-flag elimination
- Thumb：有 liveness 分析

新实现建议：

- 第一阶段：ARM/Thumb 都支持统一的 flags metadata，但都可先保守生成
- 第二阶段：把 Thumb 那套分析思路推广到 ARM
- 第三阶段：做更细粒度的 lazy flag materialization

这样架构会比当前实现整齐得多。

---

## 12. cache 与 invalidation 重新设计

### 12.1 ROM cache 与 RAM cache 继续分离，但接口统一

推荐的逻辑：

- **ROM cache**
  - key: `pc | thumb`
  - 基本不受 SMC 影响
  - flush 时保留 BIOS SWI immortal entry

- **RAM cache**
  - key 也可以是 `pc | thumb`
  - 但 block validity 必须依赖 RAM code version / page version

### 12.2 不要照抄 native 的内存布局打包技巧

当前 native 做法：

- ROM hash header 直接塞进可执行缓存
- RAM tag 表从 cache 尾部反向长出来

这在 C/汇编时代是合理的内存打包优化；在 JS 里没有必要照抄。

JS 里更适合：

- cache metadata 单独存在
- 编译后的 block function 与 metadata 分离
- 直接用 `Map` / typed hash table / array 索引结构管理

### 12.3 SMC 检测仍然应继续利用 shadow memory

这部分建议区分“信号源”和“失效策略”：

- **信号源**：继续沿用 shadow memory / code tag 机制
- **失效策略**：从“整块 RAM cache flush”升级到“按页/按范围失效”

也就是说，不要一上来就把 shadow tag 机制推倒重来。它已经深度融入：

- CPU store
- DMA store
- scan_block 的 code 标记

更现实的做法是：

1. 保留 shadow tag
2. 在 JS runtime 中额外维护 `ramPageVersion[]`
3. block 编译时记录覆盖页集合
4. RAM 写命中代码区时只提升相关页 version
5. block lookup/entry 时对比 version snapshot，失效则重编译

### 12.4 推荐的 page 粒度

IWRAM / EWRAM 的 invalidation page 粒度建议选 **16B 到 64B** 之间的小粒度页。理由：

- block 边界往往较短
- 自修改代码一般集中
- 太粗会导致无谓重编译
- 太细会放大 metadata 和 version 检查成本

第一阶段可以选 **32B** 作为折中。

### 12.5 BIOS SWI immortal entry 继续保留

native 里 `rom_cache_watermark` + `init_bios_hooks()` 的本质，是“保证 0x8 对应入口不随普通 ROM cache flush 丢失”。

JS 版不需要照搬 watermark 算术，但要保留行为：

- BIOS SWI block 单独放进 `immortalBlocks`
- ROM flush 时不清掉它
- 分支到 0x8 时直接 special-case 到它

---

## 13. helper 边界应该怎样定义

### 13.1 新后端的 helper 集合应显式列出来

当前 native emit 其实已经隐含了一组 helper 接口。新 JS backend 应把它们显式化。第一阶段至少需要：

#### CPU / scheduler
- `update_gba`
- `check_and_raise_interrupts`
- `flag_interrupt`
- `set_cpu_mode`（如果 block 不直接内联 mode switch）

#### Memory
- `read_memory8/16/32`
- `read_memory16_signed`
- `write_memory8/16/32`
- `load_gamepak_page`

#### Special execution
- `execute_swi_arm`
- `execute_swi_thumb`
- `execute_store_cpsr`
- `execute_spsr_restore`

#### Optional / compatibility
- cheat hook
- trace hook
- debug helpers

### 13.2 helper 调用前后要统一做哪些事情

这点要形成规则，否则很快会乱：

- helper 前是否必须 collapse flags
- helper 是否可能改变 PC / mode / halt state
- helper 后是否必须重新读取 `memory_map_read` / CPU mode / cycle state
- helper 是否可能触发 SMC / IRQ

建议在 IR 上明确标注每个 helper 的 side-effect class，例如：

- `HelperPure`
- `HelperMayTouchMemory`
- `HelperMayChangePC`
- `HelperMayRaiseIRQ`
- `HelperMaySleep`
- `HelperMayInvalidateRAM`

这样 emitter 与 dispatcher 才能正确处理。

---

## 14. 指令实现策略：哪些第一天就应该内联，哪些不应该

### 14.1 第一阶段建议直接内联的类别

这些是高频且语义相对清晰的，应尽早内联：

- ARM/Thumb 基础 ALU
- 立即数/寄存器 shift
- 常规 load/store byte/half/word
- 常规 branch / cond branch / link
- BX / BLX（但间接跳转通过统一退出路径处理）
- 常规 multiply（如果已验证）
- 常见 high-register Thumb 指令

### 14.2 第一阶段建议 helper 化的类别

这些不要逞强：

- ARM LDM/STM 所有复杂组合
- 带 `S` 且写 PC 的数据处理类
- CPSR/SPSR restore
- SWI / BIOS call
- 稀有半字/符号扩展边缘路径
- 某些未对齐/边界访存特殊语义
- 与 IO/backup 关系特别紧的 memory op

### 14.3 为什么 helper-first 是对的

因为 gpSP 已有两个事实：

1. `cpu.cc` 已经有成熟语义可用；
2. 自己也在某些复杂点留了 TODO。

更好的设计不是“什么都内联”，而是“先把所有难点通过 helper 走通，再基于 profile 选择性下沉到 inline fast path”。

---

## 15. `main.c` / `update_gba()` 契约如何落地到 JS runtime

### 15.1 外层执行循环建议

推荐 JS 后端统一用这样的控制逻辑：

1. 收到 `cycles`
2. 如果 `CPU_HALT_STATE != CPU_ACTIVE`，先调用 `update_gba()`
3. 如果 frame complete，返回主线程
4. 否则按 `PC + mode` lookup block
5. 运行 block
6. block 返回后根据返回码：
   - 继续到另一个 block
   - 回到 scheduler
   - 重新 lookup
   - frame complete

### 15.2 不要让 block 自己直接决定 frame 生命周期

block 内部只应该决定：

- 我接下来跳去哪
- 我是否需要进入慢路径/helper
- 我是否需要让出给 scheduler

frame 是否完成，应该仍由 `update_gba()` 负责。这符合 gpSP 当前设计。

### 15.3 idle loop 处理建议

`cpu.cc` 中解释器在命中 `idle_loop_target_pc` 时会把 `cycles_remaining` 压到 0。native dynarec 也有 `arm_update_gba_idle_*` helper。

新设计里建议：

- scanner 在 block metadata 里标出 `entryPc == idle_loop_target_pc`
- emitter 对这类 block 生成特殊入口逻辑：
  - 如果剩余 cycle > 0，则直接让出到 scheduler
- 不要把 idle loop 逻辑散落在每条指令实现里

---

## 16. translation gate、cheat hook、BIOS hook 的重新表达方式

### 16.1 translation gate

保留现有行为：**它是 block 边界强制器。**

实现方式：

- scanner 扫描时读 `translation_gate_target_pc[]`
- 命中后立即终止 block
- metadata 里打上 `forcedGate` 标志
- emitter 在 block 尾部强制走 dispatcher，而不是试图跨过去继续链接

### 16.2 cheat hook

建议保留当前行为模型：

- 如果某条 PC 命中 `cheat_master_hook`
- 则插入 helper call
- helper 之后强制 flush flags / state，并允许重新 lookup

### 16.3 BIOS hook

继续保留“一条特殊的、常驻的、可直接分支到的 BIOS SWI block”这个概念，不再依赖 native watermark 布局。

---

## 17. Emscripten 纯 JS 集成方案

### 17.1 Emscripten 在这里的角色

Emscripten 不是用来“把 dynarec 变成 Wasm”的，而是用来：

- 把 gpSP 现有 C/C++ 核心编译成 **JS 版宿主环境**
- 提供 heap、导出函数、生命周期控制
- 让 JS dynarec 与 C 核心共存于同一 JS 进程内

### 17.2 构建建议

推荐单独做一个 jsjit 目标，例如：

- `CPU_BACKEND=jsjit`
- `-s WASM=0`
- 关闭不必要的内存增长，或者对 HEAP view 重绑定做明确处理
- 暴露桥接函数与关键全局状态地址

### 17.3 需要额外注意的事项

#### A. heap view 稳定性
如果启用内存增长，`HEAPU8/HEAPU32` 等 view 可能重绑。JIT runtime 必须能够刷新自己的引用。更简单的方式是第一阶段固定内存大小，避免 growth。

#### B. 动态代码生成
Node 下可用 `new Function` 或 `vm.compileFunction`。浏览器环境要考虑 CSP；如果不能动态生成代码，必须 fallback 到解释器或预编译模式。

#### C. bridge 开销
跨 C/JS 边界过于频繁会伤性能。因此要坚持：
- 常规访存走 JS fast path
- 只有 slow path / IO / 稀有语义才走 helper

---

## 18. Node/V8 下的性能工程建议

### 18.1 block 运行时对象形态必须稳定

V8 很看重 hidden class 和对象形态稳定性。建议：

- `ctx` 对象字段固定，不要运行期频繁增删属性
- 把热数据放在 TypedArray 和固定字段中
- block metadata 与 runtime state 分开

### 18.2 避免在热路径里创建对象

block 执行期间不要做：

- 临时对象字面量
- 动态数组扩展
- Map/Set 临时包装
- 字符串拼接

这些都应只发生在编译期，不发生在执行期。

### 18.3 block 尺寸不要无限增大

native dynarec 因为 patch 成本低，较大 block 往往可接受；JS 里过大的函数会带来：

- 编译时间增长
- 反优化概率增加
- source map / debug 困难
- 内联不可控

因此建议保留或略收缩 gpSP 当前 `MAX_BLOCK_SIZE` 概念，第一阶段甚至可以比 native 更保守。

### 18.4 cycle 更新策略建议分阶段做

#### 第一阶段
每条指令直接扣 cycles，保证正确性与可观测性。

#### 第二阶段
在 block 内做局部 cycle 累积，只在：
- label target
- helper 调用
- exit
- translation gate
- cheat hook
- 可能异常路径  
时 flush。

这样可以逐步接近 native 设计，而不是一上来就陷入复杂 patch 逻辑。

### 18.5 编译缓存建议抽象出来

建议提供统一接口：

```text
compileSource(source, options) -> function
```

内部在 Node 下可以切换：

- `new Function`
- `vm.compileFunction`
- 有条件时使用 cachedData

但上层不要写死。

---

## 19. 正确性验证体系：必须比当前更强

这一节决定项目是否能长期维护。

### 19.1 最重要的原则：用 `cpu.cc` 做差分真值

不是拿外部 ARM 模型，不是拿“看起来能跑游戏”当真值，而是：

- **同一份 gpSP 状态**
- **同一份 gpSP 内存**
- **同一份 gpSP helper**
- interpreter 与 JS dynarec 做差分

这最符合这份仓库的现实。

### 19.2 分层测试矩阵

#### A. 单指令语义测试
对每条已支持 ARM/Thumb 指令：

- 造输入状态
- interpreter 执行一步
- JS backend 执行一步或一小 block
- 比较：
  - regs
  - CPSR
  - spsr / mode
  - memory diff
  - alerts / halt

#### B. block 级差分
对真实 ROM 或合成代码段：

- 从同一 savestate 开始
- interpreter 跑到 block end / cycle boundary
- JS backend 跑同样范围
- 比较完整状态

#### C. 随机指令流
在安全内存窗口内随机生成 ARM/Thumb 片段，避开未实现/未定义区域，做大量 differential fuzz。

#### D. Dhrystone / 小基准
保留 Dhrystone，但把它定位成：
- smoke/perf baseline
- 不是正确性的唯一证明

#### E. 真实 ROM smoke tests
挑若干类型不同的 ROM：
- Thumb-heavy
- ARM-heavy
- 强 IRQ/timer
- 强 DMA/audio
- 带 EEPROM/flash/RTC
- 带 translation gate/idle loop override

每个 ROM 至少验证：
- 能启动
- 若干帧无状态分歧
- video/audio/frame hash 一致或可解释

### 19.3 充分利用现有 savestate 机制

这是 gpSP 仓库自带的巨大优势，应该善用。

由于 `cpu.cc`、`main.c`、`gba_memory.c` 都有 savestate 读写逻辑，建议：

- 在多个真实 ROM 时间点生成 golden savestate
- 用同一 savestate 同时喂给 interpreter 和 JS backend
- 对比 N 个 block / M 帧后的状态

这样比从冷启动每次重跑更高效，也更容易定位问题。

### 19.4 需要强制比对的字段

至少应比较：

- `reg[0..18]` 和必要的扩展状态
- `spsr[]`
- `reg_mode[][]`
- `cpu_ticks`, `execute_cycles`, `video_count`
- `memory_map_read` 关键映射区状态（或更现实地比对 underlying memory）
- IWRAM / EWRAM / IO / palette / OAM / VRAM 的 hash
- backup / EEPROM / flash 状态
- `CPU_HALT_STATE`
- frame counter

### 19.5 trace 工具建议内建

建议给 JS backend 增加可开关的 trace 模式：

- block compile trace
- block entry/exit trace
- helper call trace
- invalidate trace
- IRQ raise trace

并尽量复用 gpSP 现有 instrumentation 思路，而不是另起一套完全无关的日志体系。

---

## 20. 迁移计划：怎样在现有仓库里一步步落地

### 20.1 阶段 0：重构工程入口，不动 CPU 语义

目标：

- 引入 `CPU_BACKEND` 概念
- 把 interpreter / native dynarec / jsjit 变成平级后端
- 不改 `cpu.cc` 语义，不改 `gba_memory.c` 语义

交付物：

- 新 backend API
- `libretro/libretro.c` 改用 backend 分发
- 现有 native dynarec 仍能跑

验收标准：

- 原 native / interpreter 行为不变
- 编译系统中 JS backend 可以占位但未启用

### 20.2 阶段 1：桥接层与 heap 视图打通

目标：

- 在 Emscripten 纯 JS 输出下，拿到 `reg[]`、`memory_map_read[]`、RAM/VRAM/IO 地址
- Node 中能读取/写入这些真实状态

交付物：

- `jsjit_bridge.c`
- `jsjit_runtime.js` 能打印真实 PC、模式、内存页映射

验收标准：

- 能从 JS 读取并修改 gpSP CPU/内存状态
- helper wrapper 可正常调用

### 20.3 阶段 2：先做 dispatcher，不做 JIT

目标：

- 用 JS 实现一个“后端调度器”
- block lookup 先直接 fallback 到 interpreter 单步/小步执行
- 验证 scheduler 契约与 frame 返回逻辑

验收标准：

- JS backend 模式下可以靠解释器 fallback 驱动若干帧
- `update_gba()` 路径、HALT/DMA 路径正确

### 20.4 阶段 3：实现 scanner 和 block metadata

目标：

- ARM/Thumb 扫描器输出 `ScannedBlock`
- 先不做 codegen，只 dump metadata

验收标准：

- 对同一入口 PC，能得到稳定 block 边界
- translation gate / SWI / branch exit / code tag 范围记录正确

### 20.5 阶段 4：实现最小 IR 与最小 emitter

目标：

- 支持一小部分常见 ARM 指令
- 编译成 JS block function
- 其余指令自动 fallback 到 helper/interpreter slow path

验收标准：

- 可在 Node 下跑通小型 ARM 测试和 Dhrystone
- block cache 生效
- interpreter 差分通过

### 20.6 阶段 5：接入真实 gpSP 内存与 scheduler

目标：

- JS block 不再只跑 toy runtime
- 直接读写 gpSP heap
- 正确处理 `update_gba()`, IRQ, HALT, SMC

验收标准：

- 可以在实际 gpSP ROM 上启动并跑若干帧
- 与 interpreter 做块级差分

### 20.7 阶段 6：补 Thumb、补复杂 helper、补 BIOS hook

目标：

- Thumb 常见路径完整化
- SWI/BIOS/cheat/idle/gate 全接上
- ROM cache immortal BIOS entry 生效

验收标准：

- 多类型 ROM 启动率明显提升
- 游戏 override 生效

### 20.8 阶段 7：引入 RAM 局部 invalidation

目标：

- 从“整 RAM flush”升级到“按页/按范围失效”
- 继续利用 shadow tag 作为写命中代码区的信号

验收标准：

- 正确性不退化
- SMC 压力下重编译量显著下降

### 20.9 阶段 8：性能整理

目标：

- 优化 hot helper 边界
- 做 flags 优化
- 调整 block size / compile threshold
- 统计 compile time / exec time / cache hit / invalidation

验收标准：

- 在 Node/V8 下达到可接受的 frame rate
- profile 数据可观测

---

## 21. 具体到文件：开发者应该先读哪些文件，按什么顺序读

推荐阅读顺序如下：

1. `libretro/libretro.c`
   - 先确认一帧入口如何调用 CPU backend

2. `main.c`
   - 重点读 `init_main()` 和 `update_gba()`
   - 先理解“CPU 什么时候必须返回给外层”

3. `cpu.h`
   - 先固定 CPU 状态布局和后端接口认知

4. `cpu.cc`
   - 重点读：
     - `fast_read_memory` / `fast_write_memory`
     - `load_aligned32` / `store_aligned32`
     - `set_cpu_mode()`
     - `check_and_raise_interrupts()`
     - `exec_arm_block_mem()` / `exec_thumb_block_mem()`
     - `init_cpu()`

5. `gba_memory.c` / `gba_memory.h`
   - 重点读：
     - `init_memory()`
     - `read_memory*`
     - `write_memory*`
     - `load_gamepak_page()`
     - DMA 读写宏
     - `load_game_config_over()`

6. `cpu_threaded.c`
   - 重点读：
     - `block_lookup_translate_builder`
     - RAM tag / ROM hash 设计
     - `scan_block`
     - `translate_block_arm/thumb`
     - `init_bios_hooks`
     - cache flush 逻辑

7. `x86/x86_stub.S`（或你最熟悉的 stub）
   - 重点读：
     - `write_epilogue`
     - `cpu_sleep_loop`
     - `lookup_pc`
     - store stub 中的 SMC 检查

8. 一个 emit header（例如 `arm/arm_emit.h`）
   - 目的不是照抄，而是列出 helper 边界与 backend 责任

---

## 22. “更好的实现”具体体现在哪里

“更好的”不能只是“结构看起来现代”，它应该在这些方面优于当前实现：

### 22.1 架构上更好
- 前端/后端解耦
- IR 明确
- helper 边界明确
- 可测试可观测

### 22.2 正确性上更好
- 有 interpreter 差分体系
- 有 savestate fork 验证
- 稀有/复杂路径不靠猜，用 helper 保底

### 22.3 性能上更好
- JS 执行模型适配 V8
- 不依赖 native exec memory
- RAM invalidation 更细粒度

### 22.4 工程上更好
- JS backend 是正式 backend，不是“伪装成新 CPU 架构”
- Emscripten 只是宿主壳，不与 dynarec 设计纠缠
- 可以在 Node 下单独验证，也能回到 libretro 主入口

---

## 23. 明确列出不该做的事

以下做法应当明确禁止：

### 23.1 不要直接在 `cpu_threaded.c` 里新增 `js_emit.h`
这会把当前宏耦合体系继续扩大，后期几乎不可维护。

### 23.2 不要把 gpSP 的内存模型简化成“一块平坦 RAM”
这会直接破坏：
- BIOS 读取限制
- open-bus
- ROM 分页
- SMC 检测
- VRAM/OAM/palette 特殊行为

### 23.3 不要把 Dhrystone 当成“已完成集成”的证明
Dhrystone 只能证明 JS translator 的一个子集能跑，不代表 gpSP 已经接通。

### 23.4 不要一开始就全内联所有复杂指令
helper-first 是更可靠的工程策略。

### 23.5 不要依赖 JS block 之间的递归互调
统一 dispatcher loop 更稳。

### 23.6 不要在热路径里频繁跨 C/JS 边界
fast path 要在 JS 中完成，helper 只处理慢路径和复杂语义。

---

## 24. 建议的目录与文件组织

建议在仓库里新增类似结构：

```text
jsjit/
  README.md
  bridge/
    jsjit_bridge.c
    jsjit_bridge.h
  runtime/
    jsjit_runtime.js
    jsjit_dispatch.js
    jsjit_cache.js
    jsjit_memory.js
  frontend/
    jsjit_decode_arm.js
    jsjit_decode_thumb.js
    jsjit_scan.js
    jsjit_ir.js
    jsjit_lower.js
  backend/
    jsjit_emit.js
    jsjit_compile.js
  tests/
    node_smoke.js
    node_differential.js
    node_dhrystone.js
    rom_smoke_manifest.json
```

核心思想不是名字，而是职责边界要清楚。

---

## 25. 里程碑式任务清单

下面这份清单可以直接拿来拆 issue。

### M0 — 工程整理
- [ ] 新增 `CPU_BACKEND` 概念
- [ ] interpreter/native/jsjit 后端入口统一
- [ ] 保证原有 interpreter/native 行为不变

### M1 — Bridge
- [ ] 暴露 `reg[]`
- [ ] 暴露 `memory_map_read[]`
- [ ] 暴露 `ewram/iwram/vram/io_registers`
- [ ] 暴露 helper wrappers
- [ ] Node 下打印和修改真实状态

### M2 — Dispatcher
- [ ] JS runtime 进入/退出契约建立
- [ ] HALT/DMA sleep 路径接上
- [ ] `update_gba()` 路径接上
- [ ] frame return 正常

### M3 — Scanner
- [ ] ARM scanner
- [ ] Thumb scanner
- [ ] exits 记录
- [ ] translation gates
- [ ] cheat hook metadata
- [ ] RAM code tag 记录

### M4 — Minimal IR + Emitter
- [ ] 常见 ARM ALU
- [ ] 常见 load/store
- [ ] direct branch
- [ ] cond branch
- [ ] helper fallback
- [ ] Node Dhrystone 跑通

### M5 — gpSP Integration
- [ ] 真正使用 gpSP heap/memory helpers
- [ ] scheduler 契约跑通
- [ ] interpreter 差分 harness 跑通

### M6 — Thumb & Hooks
- [ ] Thumb 常见路径
- [ ] BIOS SWI entry
- [ ] idle loop path
- [ ] cheat hook
- [ ] translation gate

### M7 — Invalidation
- [ ] shadow tag 继续使用
- [ ] RAM page version
- [ ] 局部失效
- [ ] invalidate trace

### M8 — Validation
- [ ] 单指令差分
- [ ] block 差分
- [ ] savestate fork
- [ ] ROM smoke tests

### M9 — Performance
- [ ] hotness threshold
- [ ] compile profile
- [ ] flags 优化
- [ ] helper 边界收缩
- [ ] V8 profile 调优

---

## 26. 最终建议：把“语义正确”与“高性能”拆开做

对于这个项目，最合理的实施原则是：

### 第一步
先做一个 **严格 obey gpSP 现有契约** 的 JS backend，即使它一开始并不快。

### 第二步
在这个基础上做性能迭代：
- flags 优化
- helper 下沉
- RAM 局部 invalidation
- block size 调整
- compile cache 优化

而不是反过来，一开始就追求“看起来像成熟 JIT 的激进优化”。

---

## 27. 一句话总结这个重实现方案的核心思想

这次重实现的中心不应该是“如何把 ARM 指令翻译成 JS”，而应该是：

> **如何在不破坏 gpSP 现有 CPU/内存/事件/兼容补丁契约的前提下，把 current dynarec 中与宿主机器码强耦合的部分抽离出来，重建为一套以 JS block function 为后端、以 interpreter 为语义真值、以 `update_gba()` 为调度边界、以 shadow-tag + page-version 为 RAM invalidation 基础的可测试后端。**

这才是“更好的 gpSP JS 实现”。

---

## 28. 附录：本计划依赖的关键源码位置（便于开发者回看）

以下是这份计划主要对应的源码热点，建议开发者直接对照阅读：

- `Makefile.common`
  - dynarec 相关构建入口
- `libretro/libretro.c`
  - 帧级 CPU 调用入口
- `main.c`
  - `init_main()`
  - `update_gba()`
- `cpu.h`
  - `reg[]` 布局
  - dynarec/public API
- `cpu.cc`
  - `fast_read_memory` / `fast_write_memory`
  - `load_aligned32` / `store_aligned32`
  - `set_cpu_mode()`
  - `check_and_raise_interrupts()`
  - `flag_interrupt()`
  - `exec_arm_block_mem()`
  - `exec_thumb_block_mem()`
  - `init_cpu()`
- `gba_memory.h`
  - sticky bit helper
- `gba_memory.c`
  - `read_memory*`
  - `write_memory*`
  - `load_game_config_over()`
  - DMA write 宏中的 SMC alert
  - `load_gamepak_page()`
  - `init_memory()`
- `cpu_threaded.c`
  - RAM tag / ROM hash
  - `block_lookup_translate_builder`
  - `scan_block`
  - `translate_block_arm/thumb`
  - `init_bios_hooks`
  - `flush_translation_cache_ram/rom`
- `x86/x86_stub.S`（或其它宿主 stub）
  - `write_epilogue`
  - `cpu_sleep_loop`
  - `lookup_pc`
  - IWRAM/EWRAM store 的 SMC 检查
- `arm/arm_emit.h` / 其它 emit header
  - helper 边界
  - `init_emitter()`
  - `execute_arm_translate()`

---

## 29. 最后一句实话

如果目标只是“再做一个能跑 Dhrystone 的 ARM->JS 翻译器”，事情已经基本证明可行。  
如果目标是“做一个真正更好的 gpSP JS 后端”，那么最重要的不是继续扩大小原型，而是 **尊重 gpSP 源码已经形成的结构事实，并把 dynarec 从宿主汇编绑定中解耦出来**。

这份计划就是按这个方向写的。
