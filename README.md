# SimpleIDE v2.0 — 脚本编程语言 & 集成开发环境

> 一款用 C++ 从零开发的 Windows 桌面编程语言，自带 IDE、解释器、多线程、库系统、语法高亮。

---

## 目录

- [简介](#简介)
- [快速开始](#快速开始)
- [界面指南](#界面指南)
- [语言手册](#语言手册)
  - [基本语法](#基本语法)
  - [变量与常量](#变量与常量)
  - [控制结构](#控制结构)
  - [函数定义](#函数定义)
  - [输入输出](#输入输出)
  - [列表 List](#列表-list)
  - [字典 Dict](#字典-dict)
  - [字符串函数](#字符串函数-25个)
  - [数学函数](#数学函数-13个)
  - [下标访问](#下标访问)
  - [多线程](#多线程)
  - [错误处理](#错误处理-try---iferrortodo---endtry)
  - [内存管理](#内存管理-gc)
- [库系统](#库系统)
  - [SAL 脚本库 GetPack](#sal-脚本库-getpack)
  - [C++ 动态库 GetCPack](#c-动态库-getcpack)
- [文件操作](#文件操作)
- [错误系统](#错误系统)
- [编译方法](#编译方法)
- [技术架构](#技术架构)

---

## 简介

SimpleIDE 是一个完整的编程语言实现，包含：

| 组件 | 说明 |
|------|------|
| **解释器** | 递归下降树遍历，支持函数/递归/闭包 |
| **IDE** | RichEdit 语法高亮、菜单栏、独立输出窗口 |
| **多线程** | 用户脚本可创建并行线程，带锁同步 |
| **库系统** | SAL 脚本库 + C++ DLL 库（支持源码自动编译） |
| **数据结构** | 变量、常量、列表、字典、下标访问 |
| **内置函数** | 25 字符串 + 13 数学 + 16 列表 + 12 字典 + 2 GC = 68+ |
| **错误系统** | 行号+列号+函数名定位，try-IfErrorToDo 异常捕获 |
| **断点恢复** | Input 中断后保存执行状态，输入后从断点恢复 |

---

## 快速开始

### 编译

```bash
g++ -std=c++11 -O2 -o SimpleIDE.exe R1Sys.cpp
```

> 无需任何外部库。Dev-C++ 中直接 F9 即可。

### 第一个程序

启动 SimpleIDE.exe，在编辑区输入：

```
PrintLog("Hello, World!")

listNew(nums, 3, 1, 4, 1, 5, 9, 2, 6)
listSort(nums)
listPrint(nums)
PrintLog("Sum =", listSum(nums))
PrintLog("Upper:", strUpper("hello"))
```

按 **F5** 或菜单 **Run → Run** 运行，输出窗口弹出显示结果。

---

## 界面指南

```
┌─────────────────────────────────────────────┐
│ File  Run  Format  Help          ← 菜单栏   │
├─────────────────────────────────────────────┤
│                                             │
│  # 代码编辑区 (RichEdit + 语法高亮)          │
│  PrintLog("Hello")                          │
│  listNew(nums, 1, 2, 3)                     │
│                                             │
├─────────────────────────────────────────────┤
│                              ← 输出窗口(弹出) │
│  [12:30:01] === Run ===                     │
│  [12:30:01] Hello                           │
│  [12:30:01] === Done ===                    │
│  [输入框]                    [Send]          │
└─────────────────────────────────────────────┘
```

### 菜单

| 菜单 | 项 | 快捷键 | 说明 |
|------|-----|--------|------|
| **File** | Open... | Ctrl+O | 打开 .sal 文件 |
| | Save... | Ctrl+S | 保存为 .sal 文件 |
| | Exit | Ctrl+Q | 退出 |
| **Run** | Run | F5 | 运行全部代码 |
| | Step | F6 | 逐块执行 |
| | Clear Code | Ctrl+L | 清空编辑区 |
| **Format** | Choose Font... | Ctrl+F | 选择字体 |
| | Increase Size | Ctrl++ | 字号+1 |
| | Decrease Size | Ctrl+- | 字号-1 |
| | Reset Font | Ctrl+0 | 重置字体 |
| **Help** | Help | F1 | 显示使用说明 |

### 语法高亮

| 颜色 | 元素 |
|------|------|
| 🟢 绿色 | 注释 `# ...` |
| 🔵 蓝色 | 关键字 `Func` `if` `For` `return` `Thread` 等 |
| 🔴 红色 | 字符串 `"hello"` `'world'` |
| 🟣 紫色 | 数字 `42` `3.14` |
| 🔷 青色 | 内置函数 `strLen` `listNew` `dictGet` 等 |
| ⚫ 黑色 | 默认（变量名、运算符） |

---

## 语言手册

### 基本语法

```python
# 这是注释
x = 42                    # 变量赋值
s = "hello"               # 字符串赋值
y = (3 + 4) * 2           # 表达式赋值
PrintLog("Hello, World!") # 输出
```

### 变量与常量

```python
# 普通变量
x = 10
name = "Alice"
x += 5                    # 复合赋值 += -= *= /=

# 常量 (定义后不可修改)
CboxS(PI, 3.14159)
CboxS(GREETING, "Hello")
PrintLog(PI * 2)          # 6.28318

# 常量函数
getCbox(PI)               # 读取常量值
isCbox(PI)                # true
showAllCboxS()            # 显示所有常量
```

### 控制结构

#### if / elseIf / else

```python
if(x > 0)
  PrintLog("positive")
elseIf(x == 0)
  PrintLog("zero")
else
  PrintLog("negative")
endif
```

#### For 循环

```python
For(i, 1, 10)
  PrintLog(i)
endfor

# 带步长
For(i, 10, 1, -1)
  PrintLog(i)
endfor

# 默认倒序 (start > end)
For(i, 5, 1)
  PrintLog(i)
endfor
```

#### while 循环

```python
i = 0
while(i < 5)
  PrintLog(i)
  i = i + 1
endwhile
```

#### break

```python
For(i, 1, 100)
  if(i == 7)
    break
  endif
endfor
```

### 函数定义

```python
Func(add, a, b)
  return a + b
EndFunc

PrintLog(add(3, 4))       # 7

# 递归
Func(fact, n)
  if(n <= 1)
    return 1
  endif
  return n * fact(n - 1)
EndFunc

PrintLog(fact(10))        # 3628800

# 嵌套调用
PrintLog(add(add(1, 2), add(3, 4)))  # 10
```

### 输入输出

```python
# 输出
PrintLog("Hello")
PrintLog("Sum:", 3 + 4)
PrintLog("Name:", name, "Age:", age)

# 输入 (语句模式)
Input("Enter name:", name)
PrintLog("Hi,", name)

# 整数输入
InputInt("Enter age:", age)
PrintLog("Age:", age)

# 表达式模式 Input
Func(greet, n)
  return "hi " + n
EndFunc
PrintLog(greet(Input()))   # Input() 返回输入值

# box / boxS (赋值语法糖)
box(x, 42)                 # 等价于 x = 42
boxS("Label", x, 42)       # 带标签的赋值
box(x, Input("Value:"))    # box + Input
```

### 列表 List

```python
listNew(nums, 3, 1, 4, 1, 5, 9, 2, 6)  # 创建
listLen(nums)               # 9
listGet(nums, 0)            # 3
listSet(nums, 1, 99)        # 修改
listAppend(nums, 100)       # 追加
listInsert(nums, 0, 0)      # 插入
listRemove(nums, 2)         # 删除
listPop(nums)               # 弹出末尾
listSort(nums)              # 排序
listReverse(nums)           # 反转
listFind(nums, 5)           # 查找位置
listContains(nums, 5)       # true
listCopy(nums, backup)      # 复制
listSum(nums)               # 求和
listJoin(nums, ",")         # "3,1,4,..."
listPrint(nums)             # [3, 1, 4, ...]
listClear(nums)             # 清空
```

### 字典 Dict

```python
dictNew(scores)
dictSet(scores, "Alice", 95)
dictSet(scores, "Bob", 87)
dictGet(scores, "Alice")    # 95
dictHas(scores, "Bob")      # true
dictLen(scores)             # 2
dictRemove(scores, "Bob")
dictKeys(scores)            # 返回键列表
dictValues(scores)          # 返回值列表
dictCopy(scores, backup)
dictMerge(scores, extra)    # 合并
dictPrint(scores)           # {Alice: 95}
dictClear(scores)
```

### 字符串函数 (25个)

| 函数 | 示例 | 结果 |
|------|------|------|
| `strLen(s)` | `strLen("hello")` | 5 |
| `strUpper(s)` | `strUpper("hello")` | HELLO |
| `strLower(s)` | `strLower("WORLD")` | world |
| `strSub(s, start, len)` | `strSub("hello", 0, 3)` | hel |
| `strCat(s1, s2, ...)` | `strCat("a","b")` | ab |
| `strRep(s, n)` | `strRep("ab", 3)` | ababab |
| `strFind(s, sub)` | `strFind("hello", "ll")` | 2 |
| `strRFind(s, sub)` | `strRFind("abab", "ab")` | 2 |
| `strReplace(s, old, new)` | `strReplace("aaa","a","b")` | bbb |
| `strTrim(s)` | `strTrim("  hi  ")` | hi |
| `strSplit(s, delim)` | `strSplit("a,b",",")` | 列表 |
| `strJoin(list, delim)` | `strJoin(lst, "-")` | a-b |
| `strChar(s, i)` | `strChar("hello", 1)` | e |
| `strAscii(c)` | `strAscii("A")` | 65 |
| `strChr(n)` | `strChr(65)` | A |
| `strReverse(s)` | `strReverse("abc")` | cba |
| `strStartsWith(s, p)` | `strStartsWith("hello","he")` | true |
| `strEndsWith(s, p)` | `strEndsWith("f.txt",".txt")` | true |
| `strContains(s, sub)` | `strContains("hello","ell")` | true |
| `strCount(s, sub)` | `strCount("abab","ab")` | 2 |
| `strLeft(s, n)` | `strLeft("hello", 2)` | he |
| `strRight(s, n)` | `strRight("hello", 2)` | lo |
| `strToInt(s)` | `strToInt("42")` | 42 |
| `strToFloat(s)` | `strToFloat("3.14")` | 3.14 |
| `numToStr(n)` | `numToStr(42)` | 42 |

### 数学函数 (13个)

| 函数 | 示例 | 结果 |
|------|------|------|
| `abs(n)` | `abs(-5)` | 5 |
| `sqrt(n)` | `sqrt(9)` | 3 |
| `pow(b, e)` | `pow(2, 10)` | 1024 |
| `max(a, b)` | `max(10, 20)` | 20 |
| `min(a, b)` | `min(10, 20)` | 10 |
| `floor(n)` | `floor(3.7)` | 3 |
| `ceil(n)` | `ceil(3.2)` | 4 |
| `round(n)` | `round(3.6)` | 4 |
| `random(min, max)` | `random(1, 100)` | 随机整数 |
| `mod(a, b)` | `mod(17, 5)` | 2 |
| `sin(n)` | `sin(0)` | 0 |
| `cos(n)` | `cos(0)` | 1 |
| `log(n)` | `log(1)` | 0 |

### 下标访问

```python
listNew(arr, 10, 20, 30, 40, 50)

# 读取
PrintLog(arr[0])           # 10
PrintLog(arr[4])           # 50
i = 2
PrintLog(arr[i])           # 30
PrintLog(arr[i + 1])       # 40

# 赋值
arr[1] = 99
arr[0] = arr[4]

# 字典下标
dictNew(d)
d["name"] = "Alice"
d["age"] = 25
PrintLog(d["name"])        # Alice
PrintLog(d["age"])         # 25

# 在表达式中使用
PrintLog(arr[0] + arr[1])  # 109
PrintLog("First: " + arr[0])  # First: 10

# For 循环中
sum = 0
For(i, 0, 4)
  sum = sum + arr[i]
endfor
PrintLog("Sum:", sum)      # 230
```

### 多线程

```python
# 启动并行线程
Thread
  PrintLog("thread 1")
  For(i, 1, 100)
    counter = counter + 1
  endfor
endThread

Thread
  PrintLog("thread 2")
  For(i, 1, 100)
    counter = counter + 1
  endfor
endThread

# 等待所有线程完成
ThreadRun
PrintLog("counter =", counter)

# 线程锁 (保护共享变量)
Thread
  Lock
  counter = counter + 1
  Unlock
endThread
```

| 语句 | 说明 |
|------|------|
| `Thread ... endThread` | 启动新线程并行执行 |
| `ThreadRun` | 等待所有线程完成 |
| `Lock` | 获取锁 |
| `Unlock` | 释放锁 |

### 错误处理 (try - IfErrorToDo - endTry)

```python
try
  x = sqrt(-1)
  PrintLog("no error")
IfErrorToDo
  PrintLog("error caught")
  x = 0
endTry
PrintLog("continued", x)
```

输出：
```
[Error] L1:C5 NumError: sqrt of negative number
error caught
continued 0
```

- try 中出错时自动中断，跳转到 IfErrorToDo
- IfErrorToDo 可省略（仅捕获不处理）
- 支持嵌套 try

### 内存管理 (gc)

```python
# 统计资源
PrintLog(gc(0))

# 清理错误记录
gc(1)

# 清理变量/列表/字典
gc(2)

# 全清理 (不含函数/DLL)
gc(3)

# 资源统计
PrintLog(memStats())
```

输出示例：
```
[GC] Vars:5->5 Lists:2->2 Dicts:1->1 Consts:1->1 Funcs:3 Errors:0->0 Total:12->12
```

---

## 库系统

### SAL 脚本库 GetPack

用我们的编程语言编写库，放在 `libs/` 文件夹：

```
SimpleIDE.exe
libs/
  mathlib.sal      ← 用我们的语言编写
```

**libs/mathlib.sal**:
```python
CboxS(PI, 3.14159)

Func(square, x)
  return x * x
EndFunc

Func(cube, x)
  return x * x * x
EndFunc
```

**使用**:
```python
GetPack("mathlib")
PrintLog(square(5))       # 25
PrintLog(cube(3))         # 27
PrintLog(PI)              # 3.14159
```

### C++ 动态库 GetCPack

用 C++ 编写高性能库，放在 `cpacks/` 文件夹，支持 `.cpp` 源码自动编译：

```
SimpleIDE.exe
cpacks/
  fastmath.cpp      ← C++ 源码 (首次自动编译为 DLL)
  fastmath.dll      ← 编译后自动生成
```

**cpacks/fastmath.cpp**:
```cpp
typedef const char* (*NativeFunc)(const char**, int);
typedef void (*RegisterFunc)(const char*, NativeFunc);

const char* fastAdd(const char** a, int n) {
    static char b[32];
    sprintf(b, "%d", atoi(a[0]) + atoi(a[1]));
    return b;
}

extern "C" __declspec(dllexport) void registerPack(RegisterFunc reg) {
    reg("fastAdd", fastAdd);
}
```

**使用**:
```python
GetCPack("fastmath")
PrintLog(fastAdd(100, 200))    # 300
```

**加载顺序**: `.dll` 优先 → 无 `.dll` 则自动编译 `.cpp`

---

## 文件操作

### 保存

菜单 **File → Save**，输入文件路径（如 `test.sal`），保存代码。

文件格式：`SALF1` 头 + UTF-8 文本

### 打开

菜单 **File → Open**，输入文件路径，加载代码到编辑区。

### 库文件

| 类型 | 目录 | 扩展名 |
|------|------|--------|
| SAL 脚本库 | `libs/` | `.sal` |
| C++ 源码库 | `cpacks/` | `.cpp` |
| C++ 编译库 | `cpacks/` | `.dll` |

---

## 错误系统

### 错误格式

```
[Error] 函数名@L行号:C列号 错误类型: 消息 | 上下文
```

示例：
```
[Error] fact@L3:C10 StackOverflowError: call depth exceeded 200 | fact
[Error] L5:C12 RangeError: list index out of range | nums[99]
[Error] L2:C8 IOError: cannot load pack: nonexistent | libs/nonexistent.sal
```

### 错误类型

| 代码 | 名称 | 说明 |
|------|------|------|
| E1001 | SyntaxError | 语法错误 |
| E1002 | NameError | 未定义变量 |
| E1003 | FuncError | 未定义函数 |
| E1004 | ArgError | 无效参数 |
| E1005 | ArgCountError | 参数数量错误 |
| E1006 | NumError | 无效数字 |
| E1008 | ListError | 无效列表 |
| E1009 | DivZeroError | 除零 |
| E1010 | RangeError | 越界 |
| E1011 | TypeError | 类型错误 |
| E1012 | NameError | 无效标识符 |
| E1013 | IOError | 文件IO错误 |
| E1014 | StackOverflowError | 栈溢出 (递归深度>200) |

### 栈溢出保护

```python
Func(inf, n)
  return inf(n + 1)    # 无限递归
EndFunc
inf(1)
```

输出：
```
[Error] inf@L2:C9 StackOverflowError: call depth exceeded 200 | inf
=== Error Summary (1 errors) ===
  E1014 StackOverflowError inf@L2:C9: call depth exceeded 200 | inf
=== Done ===
```

安全停止，不崩溃。

---

## 编译方法

### MinGW g++

```bash
g++ -std=c++11 -O2 -o SimpleIDE.exe R1Sys.cpp
```

### Dev-C++

1. 新建空项目 → 添加 `R1Sys.cpp`
2. 项目属性 → 参数 → 编译器填 `-std=c++11`
3. 链接器留空（无需任何外部库）
4. 按 F9

### Visual Studio

```bash
cl /EHsc /std:c++17 R1Sys.cpp user32.lib gdi32.lib
```

---

## 技术架构

### 文件结构

```
R1Sys.cpp           主程序 (解释器 + IDE)
R1Sys_test.cpp      测试套件 (853 个测试)
cpacks/mymath.cpp   C++ 库示例
```

### 代码统计

| 指标 | 数值 |
|------|------|
| 代码行数 | ~4000 行 |
| 测试用例 | 853 个 |
| 内置函数 | 68+ |
| 关键字 | 30+ |
| 错误类型 | 15 种 |

### 架构概览

```
┌──────────────────────────────────────┐
│              SimpleIDE.exe            │
├──────────────┬───────────────────────┤
│   IDE 层     │  RichEdit 高亮、菜单栏 │
│              │  独立输出窗口、字体管理 │
├──────────────┼───────────────────────┤
│  解释器层    │  词法分析(行级)        │
│              │  递归下降表达式求值     │
│              │  树遍历语句执行        │
│              │  断点恢复机制          │
├──────────────┼───────────────────────┤
│  运行时层    │  变量/常量/列表/字典    │
│              │  函数调用+作用域隔离    │
│              │  多线程+锁同步         │
│              │  GC 内存管理           │
│              │  栈溢出保护            │
├──────────────┼───────────────────────┤
│  库系统      │  GetPack: SAL 脚本库   │
│              │  GetCPack: C++ DLL 库  │
│              │  .cpp 自动编译         │
├──────────────┼───────────────────────┤
│  错误系统    │  行号+列号+函数名      │
│              │  try-IfErrorToDo       │
│              │  错误汇总              │
└──────────────┴───────────────────────┘
```

### 多线程架构

```
主线程 (UI)                    工作线程 (脚本执行)
  │                               │
  ├─ handleRun()                  │
  ├─ CreateThread ──────────────→ ├─ runLines(ls)
  ├─ (UI 保持响应)                │   ├─ PrintLog → PostMessage → 主线程
  ├─ (可编辑代码)                 │   ├─ Input() → 等待
  ├─ WM_APP_RUNDONE ←────────────┤   ├─ Thread → CreateThread (用户线程)
  └─ 显示结果                     │   └─ PostMessage(RUNDONE)
                                  │
                                  ├─ 用户线程1 (Lock保护)
                                  ├─ 用户线程2 (Lock保护)
                                  └─ ThreadRun → WaitForSingleObject
```

### 断点恢复机制

当 `Input()` 中断执行时，保存所有嵌套层级的断点状态：

```
runLines(主程序)     → 遇到 Input, 保存 SEQ 帧
  └─ For 循环        → 保存 FOR 帧 (循环变量/步长/循环体)
      └─ if 块       → 保存 SEQ 帧
          └─ Input() → 设置 g_waitingInput, 中断

用户输入后:
  resumeExecution()  → 从 resumeStack 依次恢复
      ├─ 恢复 FOR 帧 → 继续循环
      └─ 恢复 SEQ 帧 → 继续执行剩余行
```

---

## 完整示例

### 斐波那契数列

```python
Func(fib, n)
  if(n < 2)
    return n
  endif
  return fib(n - 1) + fib(n - 2)
EndFunc

For(i, 0, 15)
  PrintLog("fib(" + i + ") =", fib(i))
endfor
```

### 学生成绩管理

```python
dictNew(grades)
grades["Alice"] = 95
grades["Bob"] = 87
grades["Carol"] = 92

keys = dictKeys(grades)
For(i, 0, dictLen(grades) - 1)
  name = listGet(keys, i)
  score = grades[name]
  PrintLog(name, ":", score)
endfor

PrintLog("Average:", dictLen(grades))
```

### 多线程并行计算

```python
result = 0

Thread
  Lock
  For(i, 1, 500)
    result = result + 1
  endfor
  Unlock
endThread

Thread
  Lock
  For(i, 1, 500)
    result = result + 1
  endfor
  Unlock
endThread

ThreadRun
PrintLog("Result:", result)
```

### 库导入

```python
# 导入 SAL 脚本库
GetPack("mathlib")
PrintLog(square(5))

# 导入 C++ 库 (自动编译)
GetCPack("mymath")
PrintLog(cAdd(100, 200))
PrintLog(cFactorial(10))
PrintLog(cIsPrime(97))
```

### 错误处理

```python
try
  x = sqrt(-1)
IfErrorToDo
  PrintLog("caught error, using default")
  x = 0
endTry
PrintLog("x =", x)
```

---

## 版本历史

| 版本 | 主要功能 |
|------|---------|
| v2.0 | 菜单栏、字体管理、语法高亮、多线程、库系统、GC、try-IfErrorToDo |
| v1.5 | 列表/字典/常量、下标访问、Input 断点恢复 |
| v1.0 | 基本解释器、变量/函数/控制结构、输入输出 |

---

## License

MIT
