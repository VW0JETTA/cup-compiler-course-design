# 编译原理课程设计 proj1-5

C−− 语言编译器的五个阶段实验：词法/语法分析、语义分析、中间代码生成、目标代码生成、中间代码优化。

## proj1 词法与语法分析

### 源码
- `tree.h` / `tree.c` — 语法树节点定义、创建与先序遍历打印
- `lexical.l` — Flex 词法分析规则文件
- `syntax.y` — Bison 语法分析规则文件
- `main.c` — 主程序，驱动 `yyparse()` 并控制输出
- `Makefile` — 编译脚本

### 测试用例
| 文件 | 说明 |
|---|---|
| `test1_basic.cmm` | 合法 |
| `test2_control.cmm` | 合法 |
| `test3_struct_array.cmm` | 合法 |
| `test4_expr.cmm` | 合法 |
| `test5_err_syntax.cmm` | B 类语法错误 |
| `test6_err_lexical.cmm` | A 类词法错误 |
| `test7_err_comment.cmm` | A 类：未闭合块注释 |

### 编译与运行
```bash
make                        # 编译，生成 parser
./parser test1_basic.cmm    # 运行单个测试
make test                   # 批量运行全部 test*.cmm
make clean                  # 清理构建产物
```

## proj2 语义分析

### 编译与运行
```bash
make                        # 编译
./parser <source.cmm>       # 运行
make test                   # 批量测试
```

### 核心源码
- `syntax.y` — Bison 语法分析器
- `lexical.l` — Flex 词法分析器
- `semantic.h` / `semantic.c` — 语义分析头文件与主实现
- `tree.h` / `tree.c` — 语法树节点定义与构建
- `main.c` — 主入口
- `Makefile` — 编译脚本

### 测试用例

必做 17 类错误：

| 文件 | 说明 |
|---|---|
| `test1_undef_var.cmm` | type 1：变量未定义 |
| `test2_undef_func.cmm` | type 2：函数未定义 |
| `test3_var_redef.cmm` | type 3：变量重复定义 |
| `test4_func_redef.cmm` | type 4：函数重复定义 |
| `test5_assign_type.cmm` | type 5：赋值类型不匹配 |
| `test6_lval.cmm` | type 6：赋值左侧无左值 |
| `test7_operand.cmm` | type 7：操作数类型不匹配 |
| `test8_return.cmm` | type 8：return 类型不符 |
| `test9_arg_mismatch.cmm` | type 9：函数调用参数不匹配 |
| `test10_not_array.cmm` | type 10：非数组用 `[]` |
| `test11_not_func.cmm` | type 11：非函数用 `()` |
| `test12_bad_index.cmm` | type 12：下标非整型 |
| `test13_not_struct.cmm` | type 13：非结构体用 `.` |
| `test14_bad_field.cmm` | type 14：访问不存在域 |
| `test15_field_redef.cmm` | type 15：域名重复 |
| `test16_struct_redef.cmm` | type 16：结构体名重复 |
| `test17_undef_struct.cmm` | type 17：未定义结构体 |

选做：

| 文件 | 说明 |
|---|---|
| `test_31_ok_decl.cmm` | 3.1 正确（声明后定义一致） |
| `test_31_type19_18.cmm` | 3.1 错误（type 18 声明无定义 + type 19 声明不一致） |
| `test_32_nested.cmm` | 3.2 正确（不同函数同名变量不冲突） |
| `test_32_redef.cmm` | 3.2 错误（同作用域重定义 → type 3） |
| `test_33_equiv_ok.cmm` | 3.3 正确（结构等价，赋值合法） |
| `test_33_equiv_fail.cmm` | 3.3 错误（结构不等价 → type 5） |

## proj3 中间代码生成

### 源码
- `Makefile` — 构建脚本
- `lexical.l` — Flex 词法规则
- `syntax.y` — Bison 语法规则
- `ast.h` / `ast.c` — 多叉树抽象语法树数据结构
- `ir.h` / `ir.c` — 符号表、类型系统、中间代码翻译与输出
- `main.c` — 入口

### 构建与运行
```bash
make                    # 编译生成 cc
./cc 输入.cmm 输出.ir    # 翻译为中间代码
./cc 输入.cmm            # 中间代码打印到屏幕
```

### 已生成的中间代码（教材四个样例）
| 文件 | 说明 |
|---|---|
| `out1_sign.ir` | 必做样例1：符号函数 |
| `out2_factorial.ir` | 必做样例2：阶乘 |
| `out3_struct.ir` | 选做样例1：结构体传参 |
| `out4_multidim.ir` | 选做样例2：高维数组 + 数组传参 |

### 测试用例
- `test1.cmm` — 必做样例1 源码
- `test2.cmm` — 必做样例2 源码
- `opt1_struct.cmm` — 选做样例1 源码
- `opt2_array.cmm` — 选做样例2 源码

### 虚拟机验证
```bash
cd ../irsim && python3.8 irsim.pyc
# File → Open 打开上面的 .ir 文件，F5 运行
```

## proj4 目标代码生成

### 源程序
- `main.c` — 编译流程主程序
- `syntax.y` — 语法分析
- `lexical.l` — 词法分析
- `ast.h` / `ast.c` — 抽象语法树（AST）数据结构与构造
- `ir.h` / `ir.c` — 中间代码（IR）生成、输出与窥孔优化
- `codegen.h` / `codegen.c` — 目标代码生成器：指令选择 + 局部寄存器分配（教材图5.10/5.11）+ 活跃变量分析（教材图5.12）+ 栈管理与过程调用序列（教材5.2.9）

### 构建
```bash
make          # 编译生成可执行文件 cc
make clean    # 清理中间文件
```

### 测试用例
- `test1.cmm` — Fibonacci 数列
- `test2.cmm` — 递归阶乘
- `test3.cmm` — 多参数函数调用
- `test4.cmm` — 一维数组读写

### 运行方式
```bash
make                      # 编译
./cc test1.cmm test1.s    # 生成 MIPS32 汇编
# QtSPIM 图形界面打开 .s 文件运行
```

## proj5 中间代码优化

### 源码文件
- `main.c` — 驱动程序，读取 IR 文件 → 优化 → 输出
- `ir_opt.h` — 数据结构定义（Operand, InterCode, Program 等）
- `ir_reader.c` — IR 文本文件解析器
- `ir_writer.c` — IR 文本输出器
- `opt_passes.c` — 全部优化 Pass（基于 CFG + 数据流分析的全局优化）
- `Makefile` — 编译脚本（`make` 编译，`make clean` 清理）

### 优化实现（opt_passes.c）
- CFG 框架 — 基本块划分、前驱/后继、支配关系（dominators）
- 全局常量传播 — UNDEF/常量/NAC 格上的前向数据流分析
- 全局公共子表达式 — 可用表达式（值-持有者）前向 must 分析，含跨块 CSE
- 全局无用代码消除 — 活跃变量后向分析（变量在函数出口视为 dead）
- 循环不变代码外提 — 基于支配关系，将不变式提到循环 preheader
- 归纳变量强度削减 — 将循环内 `c*i` 乘法转为加/减法
- 常量折叠 / 代数化简 / 复制传播 / 控制流化简 为辅助 Pass

### 教材样例
| 文件 | 说明 |
|---|---|
| `sample1_cse.ir` | 样例1输入：局部公共子表达式消除 |
| `sample2_dce.ir` | 样例2输入：无用代码消除 |
| `sample3_cp.ir` | 样例3输入：常量传播与折叠 |
| `sample4_gcse.ir` | 样例4输入：全局 CSE（选做） |
| `sample5_licm.ir` | 样例5输入：循环不变代码外提（选做） |
| `sample6_sr.ir` | 样例6输入：归纳变量强度削减（选做） |
| `s1.ir` ~ `s6.ir` | 对应优化后的输出结果 |

### 测试文件
- `demo_big.ir` — 组合优化演示（17 行 → 9 行，减少 47%）

### 使用方式
```bash
make                          # 编译生成 ./cc
./cc sample3_cp.ir output.ir  # 优化单个 IR 文件
```

### 正确性
所有优化基于数据流分析，保证优化前后程序语义（WRITE 输出与 RETURN 值）一致。注意：样例程序均为 `RETURN #0` 且无 WRITE，所有变量在出口处均为死变量，故全局无用代码消除会将其大幅精简（这是正确且更高效的结果）；对含输出的真实程序，各项优化（CSE/常量传播/外提/强度削减）会正常体现。
