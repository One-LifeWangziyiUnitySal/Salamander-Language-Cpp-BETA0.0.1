# SimpleIDE Language Standard (SLS) v1.0

**SimpleIDE Scripting Language — Formal Specification**

| 字段 | 值 |
|------|------|
| 标准编号 | SLS-1.0 |
| 状态 | Final |
| 实现参考 | R1Sys.cpp (SimpleIDE v3.0) |
| 文件扩展名 | `.sal` |
| 文件头 | `SALF1` (5 字节, 可选) |

---

## 目录

1. [范围](#1-范围)
2. [术语与定义](#2-术语与定义)
3. [符合性](#3-符合性)
4. [词法约定](#4-词法约定)
5. [类型与值](#5-类型与值)
6. [变量与常量](#6-变量与常量)
7. [表达式](#7-表达式)
8. [语句](#8-语句)
9. [函数](#9-函数)
10. [控制结构](#10-控制结构)
11. [数据结构](#11-数据结构)
12. [类与面向对象](#12-类与面向对象)
13. [模块与命名空间](#13-模块与命名空间)
14. [运算符重载](#14-运算符重载)
15. [迭代器](#15-迭代器)
16. [错误处理](#16-错误处理)
17. [多线程](#17-多线程)
18. [输入输出](#18-输入输出)
19. [标准库](#19-标准库)
20. [库系统](#20-库系统)
21. [限制与实现定义的行为](#21-限制与实现定义的行为)
22. [语法文法 (BNF)](#22-语法文法-bnf)

---

## 1. 范围

本标准规定 SimpleIDE 脚本编程语言 (以下简称 "SAL 语言") 的语法、语义和标准库。

SAL 是一种动态类型、解释执行的脚本语言, 具备以下特性:

- 递归下降树遍历解释器
- 动态类型, 所有值在内部以字符串存储
- 闭包与匿名函数 (Lambda)
- 模块化命名空间隔离
- 面向对象编程 (类、实例、方法、`this`)
- 运算符重载
- 迭代器协议
- 多线程 (基于临界区序列化)
- 输入断点恢复机制

本标准适用于:
- SAL 语言的实现者
- SAL 程序的编写者
- SAL 库的开发者

---

## 2. 术语与定义

### 2.1 规范性术语

- **shall / 必须**: 实现必须满足的强制要求
- **should / 应当**: 推荐但非强制的要求
- **may / 可以**: 允许但非必须的行为
- **implementation-defined / 实现定义的**: 行为由实现决定, 但必须文档化

### 2.2 核心术语

| 术语 | 定义 |
|------|------|
| **值 (value)** | 程序操作的基本数据单元, 内部以 `std::string` 存储 |
| **变量 (variable)** | 命名的值存储位置, 存储在全局变量表中 |
| **常量 (constant)** | 不可变的命名值, 定义后不可修改或删除 |
| **函数 (function)** | 命名的可执行代码块, 接受参数并返回值 |
| **闭包 (closure)** | 捕获了创建时作用域变量的函数 |
| **类 (class)** | 用户定义的类型模板, 含字段和方法 |
| **实例 (instance)** | 类的具体化对象, 存储为含 `__class__` 字段的字典 |
| **模块 (module)** | 命名空间隔离单元, 成员自动加前缀 |
| **迭代器 (iterator)** | 可遍历集合的协议对象 |
| **真值 (truthiness)** | 值在布尔上下文中的解释 |

---

## 3. 符合性

### 3.1 符合性实现

符合本标准的实现 **shall** 满足以下全部要求:

1. 实现第 4 章规定的词法规则
2. 实现第 5 章规定的类型系统
3. 实现第 7-10 章规定的表达式和语句语义
4. 实现第 19 章列出的全部标准库函数
5. 实现第 16 章规定的错误处理机制
6. 遵守第 21 章规定的限制

### 3.2 符合性程序

符合本标准的程序 **shall** 不依赖以下行为:

- 未定义行为 (UB)
- 实现定义的行为 (除非可移植性不重要)
- 本标准标记为 "缺陷" 的行为

---

## 4. 词法约定

### 4.1 字符集

SAL 源程序 **shall** 使用 ASCII 字符集。Unicode 标识符 **不** 被支持。

### 4.2 行结束符

源文件使用 `\n` (LF) 或 `\r\n` (CRLF) 作为行结束符。实现 **shall** 透明处理两种格式。

### 4.3 注释

```
comment ::= '#' {any-char-except-newline}
```

- 注释以 `#` 开始, 到行末结束。
- 注释可以出现在行首 (前导空白后)。
- **不** 支持行内尾随注释: 一行要么是注释, 要么是代码, 不能混合。

```python
# 这是注释
x = 5          # 这是非法的: 整行会被当作 "x = 5  # 这是非法的"
```

### 4.4 标识符

```
identifier ::= identifier-start {identifier-continue}
identifier-start ::= [A-Za-z_]
identifier-continue ::= [A-Za-z0-9_]
```

- 标识符区分大小写: `foo`, `Foo`, `FOO` 是三个不同的标识符。
- 标识符长度无限制 (受实现内存约束)。

### 4.5 关键字

以下 **38 个** 标识符保留为关键字, 不可用作变量名或函数名:

| 分类 | 关键字 |
|------|--------|
| 函数定义 | `Func`, `EndFunc`, `lambda` |
| 条件 | `if`, `elseIf`, `else`, `endif` |
| 循环 | `For`, `endfor`, `while`, `endwhile`, `break`, `continue` |
| 返回/抛出 | `return`, `throw` |
| 错误处理 | `try`, `IfErrorToDo`, `endTry` |
| 输入输出 | `Input`, `InputInt`, `PrintLog`, `showAllBoxes`, `clearAllBoxes`, `showFuncs` |
| 赋值糖 | `box`, `boxS` |
| 常量 | `CboxS`, `getCbox`, `isCbox`, `showAllCboxS` |
| 库 | `GetPack`, `GetCPack` |
| 线程 | `Thread`, `endThread`, `ThreadRun`, `Lock`, `Unlock` |
| 面向对象 | `Class`, `EndClass`, `new`, `this` |
| 模块 | `Module`, `EndModule`, `import` |

**注意**: 块关键字 (`if`, `For`, `while`, `Func`, `Class`, `Module` 等) **must** 紧跟 `(`, 中间不允许空白。例如 `if (cond)` 是非法的, **must** 写成 `if(cond)`。

### 4.6 字面量

#### 4.6.1 数字字面量

```
number ::= [sign] digits ['.' digits] [exponent]
sign    ::= '+' | '-'
digits  ::= digit {digit}
digit   ::= '0'..'9'
exponent::= ('e' | 'E') [sign] digits
```

- 至少一位数字是 **必须** 的。
- 小数点最多一个, 且必须在指数之前。
- 指数部分 **must** 包含至少一位数字。

**有效示例**: `42`, `-42`, `+42`, `3.14`, `.5`, `5.`, `1e5`, `1E5`, `1e+5`, `1e-5`, `3.14e10`

**无效示例**: `1e` (无指数数字), `1.2.3` (双小数点), `1e1e2` (双指数), `--5` (双符号)

#### 4.6.2 字符串字面量

```
string ::= '"' {char-or-escape} '"' | "'" {char-or-escape} "'"
escape ::= '\' ('n' | 't' | 'r' | '\' | '"' | "'" | other-char)
```

- 单引号 `'...'` 和双引号 `"..."` 等价, 行为完全相同。
- 开头和结尾的引号 **must** 是同一种。
- 转义序列:

| 转义 | 含义 | 码点 |
|------|------|------|
| `\n` | 换行 | U+000A |
| `\t` | 制表符 | U+0009 |
| `\r` | 回车 | U+000D |
| `\\` | 反斜杠 | U+005C |
| `\"` | 双引号 | U+0022 |
| `\'` | 单引号 | U+0027 |
| `\X` | 字符 X 本身 | — |

```python
"hello"
'world'
"line1\nline2"
"It's a \"test\""
```

### 4.7 运算符与标点

```
operator    ::= '+' | '-' | '*' | '/' 
              | '==' | '!=' | '>' | '<' | '>=' | '<='
              | '&&' | '||' | '!'
              | '+=' | '-=' | '*=' | '/='
punctuation ::= '(' | ')' | ',' | '.' | '[' | ']' | '=' | '#'
```

### 4.8 文件结构

```
source-file ::= [file-header] {line}
file-header ::= 'SALF1'      ; 5 字节可选头
line        ::= {whitespace} (statement | comment | blank) line-end
```

---

## 5. 类型与值

### 5.1 类型分类

SAL 是动态类型语言。所有值在内部以 `std::string` 存储, 运行时通过检查推断类型:

| 类型 | 判定方式 | 存储位置 |
|------|----------|----------|
| **number** | `isPureNumber(v)` 为真 | 内联字符串 |
| **string** | `isQuoted(v)` 为真 | 内联字符串 (含引号) |
| **list** | `g_lists` 中存在该名 | `g_lists` 映射表 |
| **dict** | `g_dicts` 中存在该名 | `g_dicts` 映射表 |
| **function** | `g_funcs` 中存在该名 | `g_funcs` 映射表 |
| **class** | `g_classes` 中存在该名 | `g_classes` 映射表 |
| **object** | dict 且含 `__class__` 字段 | `g_dicts` 映射表 |

### 5.2 类型内省

使用 `type(value)` 内省函数返回类型名字符串:

```python
type(42)              # "number"
type("hello")         # "string"
type(myList)          # "list"
type(myDict)          # "dict" 或类名 (若是对象)
type(myFunc)          # "function"
```

类型谓词函数: `isNum`, `isStr`, `isList`, `isDict`, `isFunc`, `isClass`。

### 5.3 真值 (Truthiness)

在布尔上下文 (`if`, `while`, `&&`, `||`, `!`) 中, 值按以下规则解释:

| 值 | 真值 |
|----|------|
| 字符串 `"true"` | true |
| 字符串 `"false"` | false |
| 非零数字 (`"42"`, `"-1"`, `"3.14"`) | true |
| 数字零 (`"0"`, `"0.0"`) | false |
| 空字符串 `""` | false |
| 其他字符串 | 由 `atof(v) != 0` 决定 |

### 5.4 数值表示

- 数值内部以 `double` (IEEE 754 双精度) 存储。
- 整数值: 若 `|val| < 2×10¹⁸` 且为整数, 输出无小数点 (`"42"`)。
- 浮点值: 使用 `%g` 格式, 最多 6 位有效数字, 去尾零 (`"3.14"`)。

### 5.5 空值

SAL 无显式的 `null`/`nil`/`None` 类型。空字符串 `""` 充当默认空值。

---

## 6. 变量与常量

### 6.1 变量声明与赋值

变量无需声明, 首次赋值即创建:

```
assignment ::= identifier '=' expression
```

```python
x = 42
name = "Alice"
result = x + 10
```

### 6.2 作用域规则

SAL 使用 **全局变量表 + 函数调用快照恢复** 的作用域模型:

1. 所有变量存储在单一全局表 `g_vars` 中。
2. 函数调用时:
   - 快照当前全局变量表
   - 注入闭包捕获的变量 (若有)
   - 绑定参数 (可覆盖同名捕获变量)
   - 执行函数体
   - 恢复调用前的全局变量表 (清除局部变量和参数)
3. 函数内定义的变量在执行期间可见, 函数返回后被清除。
4. 函数 **不** 能访问调用者的局部变量 (无动态作用域)。

### 6.3 复合赋值

```
compound-assignment ::= identifier ('+=' | '-=' | '*=' | '/=') expression
```

```python
x += 5      # 等价于 x = x + 5
x -= 3
x *= 2
x /= 4
```

若变量未定义, 复合赋值 **shall** 自动初始化为 `0` 并发出警告。

### 6.4 常量

```
constant-def ::= 'CboxS' '(' identifier ',' expression ')'
```

```python
CboxS(PI, 3.14159)
CboxS(MAX_SIZE, 100)
```

常量的特性:
- 定义后 **不可** 修改 (尝试修改引发 `ERR_INVALID_ARG`)。
- 定义后 **不可** 删除 (尝试删除引发 `ERR_INVALID_ARG`)。
- 重复定义同名常量引发 `ERR_INVALID_ARG`。
- 常量名 **must** 是合法标识符, 否则引发 `ERR_INVALID_NAME`。

常量相关函数: `getCbox(name)`, `isCbox(name)`, `showAllCboxS()`。

### 6.5 赋值语法糖

#### 6.5.1 box

```
box(varName, expression)
```

等价于 `varName = expression`。若 `expression` 是 `Input(...)` 或 `InputInt(...)`, 直接触发输入流程并将结果赋给 `varName`。

#### 6.5.2 boxS

```
boxS(label, varName, expression)
```

带标签的 `box`。`label` 参数被解析但在当前实现中未使用。其余行为同 `box`。

---

## 7. 表达式

### 7.1 表达式语法

```
expression  ::= logical-or
logical-or  ::= logical-and {'||' logical-and}
logical-and ::= logical-not {'&&' logical-not}
logical-not ::= '!' logical-not | comparison
comparison  ::= additive [comp-op additive]
comp-op     ::= '==' | '!=' | '>' | '<' | '>=' | '<='
additive    ::= multiplicative {('+' | '-') multiplicative}
multiplicative ::= unary {('*' | '/') unary}
unary       ::= ('+' | '-') unary | primary
primary     ::= literal | identifier | call | indexing | field-access | '(' expression ')'
call        ::= identifier '(' [arg-list] ')'
indexing    ::= identifier '[' expression ']'
field-access::= (identifier | 'this') '.' identifier
literal     ::= number | string
```

### 7.2 运算符优先级

从 **最低** 到 **最高**:

| 优先级 | 运算符 | 说明 |
|--------|--------|------|
| 1 | `!` (前缀) | 逻辑非 |
| 2 | `&&` | 逻辑与 |
| 3 | `\|\|` | 逻辑或 |
| 4 | `== != > < >= <=` | 比较 |
| 5 | `+ -` | 加减 / 字符串拼接 |
| 6 | `* /` | 乘除 |
| 7 | `+ -` (一元) | 正负号 |
| 8 | 字面量、变量、调用、下标、字段访问 | 基本元 |

> **缺陷告知**: 当前实现中 `&&` 的优先级 **高于** `||`, 这与 C/Python/Java 等主流语言 **相反**。程序 **should** 使用括号明确指定求值顺序。详见 §21.2。

### 7.3 算术运算

- `+`: 若任一操作数是非数字字符串, 执行字符串拼接; 否则数值加法。
- `-`, `*`, `/`: 数值运算。`/` 除以零返回 `0` (不抛出异常)。
- 数值以 `double` 计算, 结果按 §5.4 格式化。

```python
3 + 4          # 7 (数值)
"hello" + 5    # "hello5" (拼接)
"a" + "b"      # "ab" (拼接)
10 / 3         # 3.33333
10 / 0         # 0 (不抛异常)
```

### 7.4 比较运算

- 若任一操作数是字符串类型, 执行 **字符串字典序比较**。
- 否则执行 **数值比较**。
- 返回 `"true"` 或 `"false"`。

```python
5 > 3          # "true"
"apple" < "banana"  # "true"
42 == 42       # "true"
"42" == 42     # "true" (字符串 "42" 数值等于 42)
```

### 7.5 逻辑运算

- `&&` (与), `||` (或): 短路求值。
- `!` (非): 前缀, 对操作数取反。
- 操作数按 §5.3 真值规则解释, 返回 `"true"` 或 `"false"`。

### 7.6 字符串拼接

当 `+` 的任一操作数求值后为非纯数字字符串时, `+` 执行字符串拼接:

```python
"Hello, " + "World!"    # "Hello, World!"
"Count: " + 42          # "Count: 42"
x = 10
"result=" + x + "!"     # "result=10!"
```

### 7.7 下标访问

```
indexing ::= identifier '[' expression ']'
```

- 列表下标: 数值, 0-based。越界访问引发 `ERR_OUT_OF_RANGE`。
- 字典下标: 键 (字符串)。键不存在返回空字符串 `""`。

```python
lst[0]          # 列表第一个元素
lst[i + 1]      # 表达式下标
dict["name"]    # 字典按键访问
```

### 7.8 字段访问

```
field-access ::= (identifier | 'this') '.' identifier
```

- `obj.field`: 读取对象 `obj` 的字段 `field`。
- `this.field`: 读取当前对象 (`this`) 的字段 `field`。
- 字段不存在返回空字符串 `""`。

---

## 8. 语句

### 8.1 语句分类

```
statement ::= simple-statement | compound-statement | block-statement
simple-statement  ::= assignment | compound-assignment | return-stmt | break-stmt 
                    | continue-stmt | throw-stmt | expression-statement
block-statement   ::= func-def | if-stmt | for-stmt | while-stmt | try-stmt
                    | class-def | module-def | thread-block
```

### 8.2 return 语句

```
return-stmt ::= 'return' [expression]
```

- `return expr`: 求值 `expr`, 存入 `__ret__`, 设置返回标志。
- `return`: 存入空字符串, 设置返回标志。
- 返回标志导致所有嵌套的 `runLines` 调用展开。

### 8.3 break 语句

```
break-stmt ::= 'break'
```

退出最内层 `For` 或 `while` 循环。

### 8.4 continue 语句

```
continue-stmt ::= 'continue'
```

跳过当前循环迭代的剩余部分, 进入下一次迭代。

> **缺陷告知**: 当前实现中 `continue` 复用 `break` 标志。在简单循环体中行为正确, 但在嵌套 `if` 块内使用 `continue` 可能行为异常。详见 §21.2。

### 8.5 throw 语句

```
throw-stmt ::= 'throw' [expression]
```

- `throw expr`: 抛出异常, 错误码 `ERR_INTERNAL`, 消息 `"throw: <value>"`。
- `throw`: 抛出异常, 消息 `"throw"`。
- 在 `try` 块内, `throw` 触发 `IfErrorToDo` 处理器。

### 8.6 表达式语句

单独的函数调用作为语句:

```
expression-statement ::= call
```

```python
PrintLog("Hello")
add(3, 4)
listAppend(lst, 5)
```

---

## 9. 函数

### 9.1 函数定义

```
func-def ::= 'Func' '(' identifier {',' identifier} ')' 
             {line} 
             'EndFunc'
```

```python
Func(add, a, b)
  return a + b
EndFunc

Func(greet, name)
  PrintLog("Hello, " + name)
EndFunc
```

- 第一个参数是函数名, 其余是参数名。
- 参数按位置绑定, 缺失参数默认为 `""`。
- 函数体在调用时执行, 返回值通过 `return` 语句设置。

### 9.2 函数调用

```
call ::= identifier '(' [arg-list] ')'
arg-list ::= expression {',' expression}
```

```python
add(3, 4)
result = add(1, 2) + add(3, 4)
```

### 9.3 参数传递

- 参数按 **值** 传递 (字符串值复制)。
- 列表和字典传递的是 **名称** (引用), 因此函数内修改列表/字典内容会影响外部。
- 参数在函数作用域内绑定, 返回后清除。

### 9.4 返回值

- `return expr` 设置返回值。
- 无 `return` 的函数返回空字符串 `""`。

### 9.5 递归

函数可以递归调用自身。递归深度限制为 `MAX_CALL_DEPTH = 200` (§21.1)。

```python
Func(fact, n)
  if(n <= 1)
    return 1
  endif
  return n * fact(n - 1)
EndFunc
```

### 9.6 作用域隔离

函数调用时:
1. 快照全局变量表
2. (闭包) 注入捕获的变量
3. 绑定参数
4. 执行函数体
5. 恢复全局变量表

函数内 **不** 能访问调用者的局部变量。

---

## 10. 控制结构

### 10.1 条件语句

```
if-stmt ::= 'if' '(' expression ')'
            {line}
            {'elseIf' '(' expression ')' {line}}
            ['else' {line}]
            'endif'
```

```python
if(x > 0)
  PrintLog("positive")
elseIf(x < 0)
  PrintLog("negative")
else
  PrintLog("zero")
endif
```

- 条件按 §5.3 真值规则求值。
- `elseIf` 和 `else` 可选。
- 首个匹配分支执行后, 跳到 `endif`。

### 10.2 For 循环

```
for-stmt ::= 'For' '(' identifier ',' expression ',' expression [',' expression] ')'
             {line}
             'endfor'
```

```python
For(i, 1, 10)
  PrintLog(i)
endfor

For(i, 0, 100, 5)       # 步长 5
  PrintLog(i)
endfor

For(i, 10, 1, -1)       # 递减
  PrintLog(i)
endfor
```

- 第 1 参数: 循环变量名
- 第 2 参数: 起始值 (数值表达式)
- 第 3 参数: 终止值 (数值表达式)
- 第 4 参数 (可选): 步长。若为纯数字, 作为常量步长; 否则作为表达式每次迭代重新求值。
- 步长省略时: `start <= end` 用 `+1`, 否则用 `-1`。
- 步长为 0 引发 `ERR_INVALID_ARG`。
- 循环变量包含终止值 (闭区间)。

### 10.3 while 循环

```
while-stmt ::= 'while' '(' expression ')'
               {line}
               'endwhile'
```

```python
while(i < 100)
  i += 1
  if(i == 50)
    break
  endif
endwhile
```

- 每次迭代前求值条件。
- `break` 退出循环, `continue` 跳过当前迭代。

### 10.4 循环限制

每个循环最多迭代 **100,000** 次 (§21.1)。超限引发 `ERR_INTERNAL`。

---

## 11. 数据结构

### 11.1 列表 (List)

列表是有序的可变序列, 存储在 `g_lists` 映射表中, 以名称引用。

#### 11.1.1 创建与操作

```python
listNew(myList, 1, 2, 3)      # 创建并初始化
listAppend(myList, 4)         # 追加元素
listGet(myList, 0)            # 获取元素 (0-based)
listSet(myList, 1, 99)        # 修改元素
listLen(myList)               # 长度
listInsert(myList, 0, 0)      # 插入元素
listRemove(myList, 2)         # 删除元素
listPop(myList)               # 弹出末尾元素
listClear(myList)             # 清空
listSort(myList)              # 排序 (字符串字典序)
listReverse(myList)           # 反转
listFind(myList, "x")        # 查找, 返回索引或 -1
listContains(myList, "x")    # 是否包含
listCopy(src, dst)            # 复制
listSum(myList)               # 求和 (数值)
listJoin(myList, ",")        # 拼接为字符串
listPrint(myList)             # 打印
```

#### 11.1.2 字面量语法

```python
myList = list(1, 2, 3, "four")   # 创建列表字面量
empty = list()                    # 空列表
```

#### 11.1.3 下标访问

```python
x = myList[0]        # 读取
myList[1] = "new"    # 写入
```

### 11.2 字典 (Dict)

字典是键值对映射, 存储在 `g_dicts` 映射表中, 以名称引用。

#### 11.2.1 创建与操作

```python
dictNew(myDict)               # 创建空字典
dictSet(myDict, "name", "Alice")  # 设置键值
dictGet(myDict, "name")      # 获取值
dictHas(myDict, "name")      # 是否存在键
dictRemove(myDict, "name")   # 删除键
dictLen(myDict)               # 键数量
dictClear(myDict)             # 清空
dictKeys(myDict)              # 返回键列表
dictValues(myDict)            # 返回值列表
dictCopy(src, dst)            # 复制
dictMerge(dst, src)           # 合并 src 到 dst
dictPrint(myDict)             # 打印
```

#### 11.2.2 字面量语法

```python
myDict = dict("name", "Alice", "age", "30")  # 交替键值对
```

#### 11.2.3 下标访问

```python
x = myDict["name"]            # 读取
myDict["age"] = "31"          # 写入
```

---

## 12. 类与面向对象

### 12.1 类定义

```
class-def ::= 'Class' '(' identifier {',' identifier} ')'
              {method-def | field-init}
              'EndClass'
method-def ::= 'Func' '(' identifier {',' identifier} ')'
               {line}
               'EndFunc'
```

```python
Class(Point, x, y)
  Func(getX)
    return this.x
  EndFunc
  Func(getY)
    return this.y
  EndFunc
  Func(distance, other)
    dx = this.x - other.x
    dy = this.y - other.y
    return sqrt(dx*dx + dy*dy)
  EndFunc
EndClass
```

- 第一个参数是类名, 其余是字段名。
- 类体中用 `Func` 定义方法。
- 方法内用 `this.field` 访问成员变量。

### 12.2 实例化

```
instantiation ::= 'new' '(' identifier ')'
```

```python
p = new(Point)
p.x = 3
p.y = 4
```

- `new(ClassName)` 创建实例, 返回对象引用 (内部为字典名)。
- 对象存储为字典, 含 `__class__` 字段记录类名。
- 声明的字段初始化为空字符串 `""`。

### 12.3 成员访问

#### 12.3.1 读取

```python
p.x              # 对象字段
this.x           # 当前对象字段 (方法内)
```

#### 12.3.2 写入

```python
p.x = 10
this.y = 20
```

### 12.4 方法调用

```python
p.getX()
p.distance(q)
this.getX()
```

- 方法调用时, `this` 设置为当前对象的字典名。
- 方法参数按名称绑定 (非位置 only)。
- 方法返回值通过 `return` 设置。

### 12.5 `this` 关键字

- `this` 在方法体内指向当前对象。
- 在方法外使用 `this` 引发 `ERR_UNDEFINED_VAR`。
- `this` 在方法调用开始时设置, 结束时恢复。

### 12.6 对象的字典表示

对象实例在内部是一个字典, 包含:

| 键 | 值 |
|----|----|
| `__class__` | 类名字符串 |
| `<field1>` | 字段值 |
| `<field2>` | 字段值 |
| ... | ... |

因此, 所有字典操作函数 (`dictGet`, `dictSet` 等) 也可用于对象。

---

## 13. 模块与命名空间

### 13.1 模块定义

```
module-def ::= 'Module' '(' identifier ')'
               {line}
               'EndModule'
```

```python
Module(Math)
  Func(add, a, b)
    return a + b
  EndFunc
  Func(mul, a, b)
    return a * b
  EndFunc
  PI = 3
EndModule
```

### 13.2 命名空间隔离

模块内的函数和变量自动加前缀 `ModuleName__`:

- `Func(add, ...)` 在 `Math` 模块内 → 注册为 `Math__add`
- `PI = 3` 在 `Math` 模块内 → 存储为 `Math__PI`

模块内的变量 **不** 污染全局命名空间。

### 13.3 限定访问

使用点号访问模块成员, 运行时自动展开为前缀形式:

```python
Math.add(3, 4)     # 实际调用 Math__add(3, 4)
Math.mul(5, 6)     # 实际调用 Math__mul(5, 6)
Math.PI            # 实际读取 Math__PI
```

仅当点号前的标识符是 **已注册的模块名** 时才展开, 普通对象的字段访问不受影响。

### 13.4 import 语句

```
import-stmt ::= 'import' identifier
```

```python
Module(Math)
  Func(add, a, b)
    return a + b
  EndFunc
EndModule

import Math
PrintLog(add(10, 20))    # 30 (直接调用, 无需限定)
```

`import` 创建模块成员的 **浅拷贝别名**:
- 对每个 `Module__name` 函数, 创建短名 `name` 的副本。
- 对每个 `Module__name` 变量, 创建短名 `name` 的副本。
- 别名是独立副本, 模块后续修改不影响已导入的别名。

### 13.5 模块嵌套

模块可以嵌套定义。内层模块的成员前缀为 `Outer__Inner__name`。

```python
Module(Outer)
  Module(Inner)
    Func(foo)
      return 42
    EndFunc
  EndModule
EndModule

Outer.Inner.foo()    # 实际调用 Outer__Inner__foo
```

---

## 14. 运算符重载

### 14.1 重载方法

类可以定义以下特殊方法来重载运算符:

| 运算符 | 方法名 | 语义 |
|--------|--------|------|
| `+` | `__add__` | 加法 |
| `-` | `__sub__` | 减法 |
| `*` | `__mul__` | 乘法 |
| `/` | `__div__` | 除法 |

### 14.2 定义语法

```python
Class(Vec, x, y)
  Func(__add__, other)
    r = new(Vec)
    r.x = this.x + other.x
    r.y = this.y + other.y
    return r
  EndFunc
EndClass
```

### 14.3 调度规则

当表达式 `a OP b` 被求值时:

1. 求值 `a` 和 `b`。
2. 若 `a` 是对象 (含 `__class__` 的字典) 且其类定义了对应重载方法, 调用之: `this = a`, 第一个参数 = `b`。
3. 否则, 若 `b` 是对象且其类定义了对应重载方法, 调用之: `this = b`, 第一个参数 = `a`。
4. 否则, 执行普通算术运算。

```python
v1 = new(Vec)
v1.x = 3
v1.y = 4
v2 = new(Vec)
v2.x = 10
v2.y = 20

v3 = v1 + v2      # 调用 v1.__add__(v2), this=v1, other=v2
PrintLog(v3.x)    # 13
PrintLog(v3.y)    # 24
```

### 14.4 重载方法的参数

重载方法 **shall** 声明恰好一个参数 (另一操作数)。`this` 提供接收者对象。

---

## 15. 迭代器

### 15.1 迭代器协议

迭代器是一个字典, 包含以下内部字段:

| 字段 | 值 | 说明 |
|------|----|------|
| `__type__` | `"list"` 或 `"dict"` | 源集合类型 |
| `__data__` | 集合名 | 数据快照的引用 |
| `__keys__` | 列表名 (仅 dict) | 键列表快照 |
| `__pos__` | 数值字符串 | 当前位置 (0-based) |

### 15.2 创建迭代器

```
iter(collection)
```

```python
lst = list("a", "b", "c")
it = iter(lst)
```

- `collection` **must** 是列表名或字典名。
- 创建数据快照 (迭代过程中修改原集合不影响迭代器)。
- 返回迭代器引用。
- 非列表/字典参数引发 `ERR_INVALID_TYPE`。

### 15.3 hasNext

```
hasNext(iterator) -> "true" | "false"
```

- 返回 `"true"` 若还有下一个元素。
- 返回 `"false"` 若已遍历完毕。
- **不** 推进位置。

### 15.4 next

```
next(iterator) -> value | key
```

- 列表: 返回当前元素值。
- 字典: 返回当前键 (不是值)。
- 推进位置 `__pos__` 加 1。
- 迭代器耗尽时引发 `ERR_OUT_OF_RANGE`。

### 15.5 iterValue

```
iterValue(iterator) -> value
```

- 返回当前位置 (`__pos__ - 1`, 即上次 `next` 返回的元素) 对应的值。
- 字典: 返回键对应的值。
- 列表: 返回元素值。
- 用于字典迭代时获取值。

### 15.6 典型用法

```python
# 列表遍历
lst = list(10, 20, 30, 40)
it = iter(lst)
total = 0
while(hasNext(it))
  total = total + next(it)
endwhile
PrintLog(total)      # 100

# 字典遍历
d = dict()
dictSet(d, "name", "Alice")
dictSet(d, "age", "30")
it = iter(d)
while(hasNext(it))
  key = next(it)
  val = iterValue(it)
  PrintLog(key, "=", val)
endwhile
# 输出: age = 30, name = Alice (字典键按 map 内部顺序)
```

---

## 16. 错误处理

### 16.1 错误类型

| 错误码 | 名称 | 描述 |
|--------|------|------|
| 0 | `ERR_NONE` | 无错误 |
| 1001 | `ERR_SYNTAX` | 语法错误 |
| 1002 | `ERR_UNDEFINED_VAR` | 未定义变量 |
| 1003 | `ERR_UNDEFINED_FUNC` | 未定义函数 |
| 1004 | `ERR_INVALID_ARG` | 无效参数 |
| 1005 | `ERR_ARG_COUNT` | 参数数量错误 |
| 1006 | `ERR_INVALID_NUM` | 无效数字 |
| 1007 | `ERR_INVALID_STR` | 无效字符串 |
| 1008 | `ERR_INVALID_LIST` | 无效列表 |
| 1009 | `ERR_DIV_ZERO` | 除以零 |
| 1010 | `ERR_OUT_OF_RANGE` | 越界 |
| 1011 | `ERR_INVALID_TYPE` | 类型错误 |
| 1012 | `ERR_INVALID_NAME` | 无效名称 |
| 1013 | `ERR_FILE_IO` | 文件 I/O 错误 |
| 1014 | `ERR_STACK_OVERFLOW` | 栈溢出 |
| 1099 | `ERR_INTERNAL` | 内部错误 (含 `throw`) |

### 16.2 错误格式

```
[Error] [FuncName@]L<line>:C<col> <ErrorName>: <message> [| <context>]
```

### 16.3 try-IfErrorToDo 语句

```
try-stmt ::= 'try'
             {line}            ; 受保护代码
             'IfErrorToDo'
             {line}            ; 错误处理器
             'endTry'
```

```python
try
  x = 1 / 0
IfErrorToDo
  PrintLog("除以零错误!")
  PrintLog(lastError())
endTry
```

语义:
1. 记录当前错误数。
2. 设置 `g_inTryBlock = true`。
3. 执行受保护代码。首个错误立即中断受保护代码。
4. 若错误数增加, 执行错误处理器。
5. 错误 **不** 被清除, 保留在错误列表中供后续查询。

### 16.4 错误查询函数

| 函数 | 返回值 |
|------|--------|
| `lastError()` | 最后一个错误的消息 |
| `lastErrorCode()` | 最后一个错误的码 (数字字符串) |
| `errorCount()` | 错误总数 |
| `clearErrors()` | 清除所有错误, 返回 `"true"` |

### 16.5 throw 语句

```
throw-stmt ::= 'throw' [expression]
```

抛出 `ERR_INTERNAL` 错误。在 `try` 块内被捕获, 触发 `IfErrorToDo`。

---

## 17. 多线程

### 17.1 Thread 块

```
thread-block ::= 'Thread'
                  {line}
                  'endThread'
```

```python
Thread
  PrintLog("并行代码")
endThread
```

- 收集线程体代码行。
- 创建新线程执行线程体。
- 线程体共享全局状态 (`g_vars`, `g_lists` 等)。
- 执行通过临界区序列化 (线程实际 **不** 并行, 一次只执行一个)。

> **注意**: 当前实现中, 线程通过全局临界区序列化执行。多个线程不会真正并行运行, 而是排队依次执行。详见 §21.2。

### 17.2 ThreadRun

```
ThreadRun
```

等待所有用户创建的线程完成。**should** 在 `Thread` 块后调用以确保线程执行完毕。

### 17.3 Lock / Unlock

```
Lock
... 临界区代码 ...
Unlock
```

用户级互斥锁。`Lock` 进入临界区, `Unlock` 离开临界区。

> **缺陷告知**: 由于 `scriptThreadFunc` 已持有临界区, 用户级 `Lock` 会永久阻塞。详见 §21.2。

---

## 18. 输入输出

### 18.1 输出

```
PrintLog(expr {',' expr})
```

```python
PrintLog("Hello")
PrintLog("Sum:", 3 + 4)
PrintLog("Name:", name, "Age:", age)
```

- 参数以空格分隔, 输出到日志窗口。
- 每次调用输出一行。

### 18.2 输入 (语句模式)

```
Input(prompt [, varName])
InputInt(prompt [, varName])
```

```python
Input("Enter name: ", name)       # 字符串输入, 存入 name
InputInt("Enter age: ", age)      # 整数输入, 存入 age
```

- `prompt`: 提示文本 (可选)。
- `varName`: 目标变量名 (可选, 字面量不求值)。
- `InputInt` 验证输入为 `[+-]?[0-9]+`, 无效则替换为 `"0"`。
- 输入会 **暂停** 程序执行, 等待用户提交后恢复。

### 18.3 输入 (表达式模式)

```python
x = Input("Value: ") + 10
y = InputInt("N: ") * 2
box(name, Input("Name: "))
```

- `Input`/`InputInt` 可作为子表达式使用。
- 求值时暂停, 用户提交后替换为输入值并继续求值。
- 表达式模式 **不** 绑定变量, 仅返回值。

### 18.4 输入恢复机制

当遇到 `Input`/`InputInt`:
1. 保存当前执行上下文 (行索引、循环状态等) 到恢复栈。
2. 暂停运行线程, 等待用户输入。
3. 用户提交后, 从恢复栈弹出上下文, 继续执行。

支持嵌套 `Input` (表达式模式), 以及循环内的 `Input`。

---

## 19. 标准库

### 19.1 字符串函数 (25 个)

| 函数 | 签名 | 说明 |
|------|------|------|
| `strLen(s)` | → number | 字符串长度 |
| `strUpper(s)` | → string | 转大写 |
| `strLower(s)` | → string | 转小写 |
| `strSub(s, start, len)` | → string | 子串 |
| `strCat(s1, s2)` | → string | 拼接 |
| `strRep(s, n)` | → string | 重复 n 次 |
| `strFind(s, sub)` | → number | 查找, 返回索引或 -1 |
| `strRFind(s, sub)` | → number | 反向查找 |
| `strReplace(s, old, new)` | → string | 替换所有匹配 |
| `strTrim(s)` | → string | 去首尾空白 |
| `strSplit(s, sep)` | → list | 分割 |
| `strJoin(list, sep)` | → string | 连接 |
| `strChar(s, i)` | → string | 取第 i 个字符 |
| `strAscii(s)` | → number | 首字符 ASCII 码 |
| `strChr(code)` | → string | ASCII 码转字符 |
| `strReverse(s)` | → string | 反转 |
| `strStartsWith(s, prefix)` | → bool | 是否以 prefix 开头 |
| `strEndsWith(s, suffix)` | → bool | 是否以 suffix 结尾 |
| `strContains(s, sub)` | → bool | 是否包含 |
| `strCount(s, sub)` | → number | 计数 |
| `strLeft(s, n)` | → string | 左取 n 个字符 |
| `strRight(s, n)` | → string | 右取 n 个字符 |
| `strToInt(s)` | → number | 转整数 |
| `strToFloat(s)` | → number | 转浮点数 |
| `numToStr(n)` | → string | 数字转字符串 |

### 19.2 列表函数 (17 个)

| 函数 | 签名 | 说明 |
|------|------|------|
| `listNew(name, ...)` | → name | 创建列表 |
| `listLen(name)` | → number | 长度 |
| `listGet(name, i)` | → value | 获取元素 |
| `listSet(name, i, v)` | → value | 设置元素 |
| `listAppend(name, v)` | → value | 追加 |
| `listInsert(name, i, v)` | → value | 插入 |
| `listRemove(name, i)` | → value | 删除 |
| `listPop(name)` | → value | 弹出末尾 |
| `listClear(name)` | → bool | 清空 |
| `listSort(name)` | → bool | 排序 |
| `listReverse(name)` | → bool | 反转 |
| `listFind(name, v)` | → number | 查找 |
| `listContains(name, v)` | → bool | 是否包含 |
| `listCopy(src, dst)` | → dst | 复制 |
| `listSum(name)` | → number | 求和 |
| `listJoin(name, sep)` | → string | 连接 |
| `listPrint(name)` | → string | 打印 |

### 19.3 字典函数 (12 个)

| 函数 | 签名 | 说明 |
|------|------|------|
| `dictNew(name)` | → name | 创建字典 |
| `dictSet(name, k, v)` | → value | 设置键值 |
| `dictGet(name, k)` | → value | 获取值 |
| `dictHas(name, k)` | → bool | 是否存在键 |
| `dictRemove(name, k)` | → bool | 删除键 |
| `dictLen(name)` | → number | 键数量 |
| `dictClear(name)` | → bool | 清空 |
| `dictKeys(name)` | → list | 键列表 |
| `dictValues(name)` | → list | 值列表 |
| `dictCopy(src, dst)` | → dst | 复制 |
| `dictPrint(name)` | → string | 打印 |
| `dictMerge(dst, src)` | → bool | 合并 |

### 19.4 数学函数 (20 个)

| 函数 | 说明 |
|------|------|
| `abs(x)` | 绝对值 |
| `sqrt(x)` | 平方根 (负数引发错误) |
| `pow(x, y)` | 幂 |
| `max(a, b)` | 最大值 |
| `min(a, b)` | 最小值 |
| `floor(x)` | 向下取整 |
| `ceil(x)` | 向上取整 |
| `round(x)` | 四舍五入 |
| `random()` | 伪随机数 [0, 1) |
| `mod(a, b)` | 取模 (除零引发错误) |
| `sin(x)` | 正弦 |
| `cos(x)` | 余弦 |
| `tan(x)` | 正切 |
| `atan(x)` | 反正切 |
| `exp(x)` | 指数 e^x |
| `log(x)` | 自然对数 (非正数引发错误) |
| `log10(x)` | 常用对数 |
| `clamp(x, lo, hi)` | 限制范围 |
| `lerp(a, b, t)` | 线性插值 |
| `dist(x1, y1, x2, y2)` | 两点距离 |

### 19.5 类型判断函数 (7 个)

| 函数 | 说明 |
|------|------|
| `isNum(v)` | 是否数字 |
| `isStr(v)` | 是否字符串 |
| `isList(v)` | 是否列表 |
| `isDict(v)` | 是否字典 |
| `isFunc(v)` | 是否函数 |
| `isClass(v)` | 是否类 |
| `type(v)` | 类型名字符串 |

### 19.6 迭代器函数 (4 个)

| 函数 | 说明 |
|------|------|
| `iter(collection)` | 创建迭代器 |
| `hasNext(it)` | 是否有下一元素 |
| `next(it)` | 获取下一元素 |
| `iterValue(it)` | 获取当前值 |

### 19.7 错误信息函数 (4 个)

| 函数 | 说明 |
|------|------|
| `lastError()` | 最后错误消息 |
| `lastErrorCode()` | 最后错误码 |
| `errorCount()` | 错误总数 |
| `clearErrors()` | 清除所有错误 |

### 19.8 文件操作函数 (4 个)

| 函数 | 说明 |
|------|------|
| `fileRead(path)` | 读取文件内容 |
| `fileWrite(path, content)` | 写入文件 |
| `fileAppend(path, content)` | 追加写入 |
| `fileExists(path)` | 文件是否存在 |

### 19.9 系统函数 (4 个)

| 函数 | 说明 |
|------|------|
| `sleep(ms)` | 休眠毫秒数 |
| `gc(mode)` | 垃圾回收 / 内存统计 |
| `memStats()` | 内存统计信息 |
| `new(ClassName)` | 创建对象实例 |

### 19.10 字面量构造函数 (2 个)

| 函数 | 说明 |
|------|------|
| `list(...)` | 创建列表字面量 |
| `dict(k1, v1, k2, v2, ...)` | 创建字典字面量 |

---

## 20. 库系统

### 20.1 SAL 脚本库 (GetPack)

```
GetPack("libname")
```

- 从 `<exe_dir>/libs/libname.sal` 加载库文件。
- 执行库文件中的代码 (注册函数、定义变量)。
- 库文件格式同普通源文件 (`.sal`, 可选 `SALF1` 头)。
- 重复加载同一库被忽略 (幂等)。
- 为每个新注册的函数创建命名空间副本 `libname::funcname`。

```python
GetPack("mymath")
result = add(3, 4)         # 直接调用
```

### 20.2 C++ 动态库 (GetCPack)

```
GetCPack("libname")
```

- 从 `<exe_dir>/cpacks/libname.dll` 加载 C++ 编译的动态库。
- 若 `.dll` 不存在但 `libname.cpp` 存在, 自动编译: `g++ -shared -std=c++11 -o "libname.dll" "libname.cpp"`。
- DLL **must** 导出 `registerPack` 函数, 调用宿主提供的 `registerNativeFunc(name, func)` 注册原生函数。
- 原生函数签名: `const char* (*NativeFunc)(const char** args, int argc)`。
- 注册的原生函数可像内置函数一样从脚本调用。

```cpp
// cpacks/mymath.cpp 示例
#include <cstring>
#include <cmath>

typedef const char* (*NativeFunc)(const char**, int);

static const char* mySqrt(const char** args, int argc) {
    static char buf[64];
    double v = atof(args[0]);
    sprintf(buf, "%g", sqrt(v));
    return buf;
}

extern "C" __declspec(dllexport) void registerPack(void (*reg)(const char*, NativeFunc)) {
    reg("mySqrt", mySqrt);
}
```

```python
GetCPack("mymath")
PrintLog(mySqrt(16))    # 4
```

---

## 21. 限制与实现定义的行为

### 21.1 实现限制

| 限制 | 值 | 超限行为 |
|------|------|----------|
| 递归深度 | 200 | `ERR_STACK_OVERFLOW` |
| 循环迭代次数 | 100,000 | `ERR_INTERNAL` |
| 表达式求值安全计数 | 1,000 | 静默停止 |
| 下标替换安全计数 | 1,000 | 静默停止 |
| 内置函数替换安全计数 | 1,000 | 静默停止 |
| 标识符长度 | 无限制 | (受内存约束) |
| 字符串长度 | 无限制 | (受内存约束) |
| 列表/字典大小 | 无限制 | (受内存约束) |

### 21.2 已知实现缺陷

本标准记录以下当前实现中的缺陷。符合性程序 **should not** 依赖这些行为:

#### 21.2.1 `&&` 与 `||` 优先级反转

当前实现中 `&&` 优先级 **高于** `||`, 与主流语言相反。`a || b && c` 被解析为 `(a || b) && c`。

**建议**: 使用括号明确求值顺序。

#### 21.2.2 `continue` 语义不完整

`continue` 复用 `break` 标志, 在嵌套 `if` 块内使用 `continue` 可能行为异常。

**建议**: 避免在嵌套块内使用 `continue`, 或重构为 `if-else` 结构。

#### 21.2.3 线程非真正并行

线程通过全局临界区序列化执行, 不并行运行。

**建议**: 将 `Thread` 视为 "延迟执行" 而非 "并行执行"。

#### 21.2.4 `Lock`/`Unlock` 死锁风险

由于 `scriptThreadFunc` 已持有临界区, 用户级 `Lock` 会永久阻塞。

**建议**: 不要使用 `Lock`/`Unlock`, 依赖线程序列化语义。

#### 21.2.5 `boxS` 标签未使用

`boxS` 的 `label` 参数被解析但未在 UI 中使用。

#### 21.2.6 `type()` 返回值不完整

`type()` 不返回 `"function"` 或 `"class"`。使用 `isFunc`/`isClass` 替代。

#### 21.2.7 `^` 运算符未实现

帮助文档中提及 `^` (幂运算), 但未实现。使用 `pow(x, y)` 替代。

#### 21.2.8 块关键字需紧跟 `(`

`if (cond)` (带空格) 不被识别为条件语句。**must** 写成 `if(cond)`。

### 21.3 实现定义的行为

以下行为由实现决定, 但本标准记录当前实现的选择:

| 行为 | 实现选择 |
|------|----------|
| 浮点精度 | IEEE 754 双精度 (`double`) |
| 数值格式化 | 整数无小数点, 浮点用 `%g` (6 位有效数字) |
| 字典键顺序 | `std::map` 字典序 (按键排序) |
| 随机数算法 | C 标准库 `rand()` |
| 文件路径分隔符 | `\` (Windows) |
| 行结束符 | `\r\n` (输出), `\n` 或 `\r\n` (输入) |

---

## 22. 语法文法 (BNF)

### 22.1 词法文法

```bnf
<source-file>    ::= ['SALF1'] {<line>}
<line>           ::= {<whitespace>} (<statement> | <comment> | <empty>) <EOL>
<comment>        ::= '#' {<any-char-except-EOL>}
<whitespace>     ::= ' ' | '\t'

<identifier>     ::= <id-start> {<id-continue>}
<id-start>       ::= [A-Za-z_]
<id-continue>    ::= [A-Za-z0-9_]

<number>         ::= [<sign>] <digits> ['.' <digits>] [<exponent>]
<sign>           ::= '+' | '-'
<digits>         ::= <digit> {<digit>}
<digit>          ::= '0' | '1' | ... | '9'
<exponent>       ::= ('e' | 'E') [<sign>] <digits>

<string>         ::= '"' {<char-or-escape>} '"' | "'" {<char-or-escape>} "'"
<escape>         ::= '\' ('n' | 't' | 'r' | '\' | '"' | "'" | <any-char>)

<keyword>        ::= 'Func' | 'EndFunc' | 'if' | 'elseIf' | 'else' | 'endif'
                   | 'For' | 'endfor' | 'while' | 'endwhile' | 'return' | 'break'
                   | 'try' | 'IfErrorToDo' | 'endTry' | 'continue' | 'throw'
                   | 'Class' | 'EndClass' | 'new' | 'this' | 'lambda'
                   | 'Module' | 'EndModule' | 'import'
                   | 'Thread' | 'endThread' | 'ThreadRun' | 'Lock' | 'Unlock'
                   | 'Input' | 'InputInt' | 'PrintLog' | 'box' | 'boxS'
                   | 'CboxS' | 'getCbox' | 'isCbox' | 'showAllCboxS'
                   | 'GetPack' | 'GetCPack' | 'showAllBoxes' | 'clearAllBoxes' | 'showFuncs'

<operator>       ::= '+' | '-' | '*' | '/' | '==' | '!=' | '>' | '<' | '>=' | '<='
                   | '&&' | '||' | '!' | '+=' | '-=' | '*=' | '/='

<punctuation>    ::= '(' | ')' | ',' | '.' | '[' | ']' | '=' | '#'
```

### 22.2 语句文法

```bnf
<program>        ::= {<statement>}

<statement>      ::= <assignment> | <compound-assignment> | <return-stmt>
                   | <break-stmt> | <continue-stmt> | <throw-stmt>
                   | <func-def> | <if-stmt> | <for-stmt> | <while-stmt>
                   | <try-stmt> | <class-def> | <module-def> | <import-stmt>
                   | <thread-block> | <expression-statement>
                   | <input-stmt> | <print-stmt> | <box-stmt> | <const-def>
                   | <getpack-stmt> | <getcpack-stmt>

<assignment>     ::= <lvalue> '=' <expression>
<lvalue>         ::= <identifier> | <identifier> '[' <expression> ']'
                   | <identifier> '.' <identifier> | 'this' '.' <identifier>

<compound-assignment> ::= <identifier> ('+=' | '-=' | '*=' | '/=') <expression>

<return-stmt>    ::= 'return' [<expression>]
<break-stmt>     ::= 'break'
<continue-stmt>  ::= 'continue'
<throw-stmt>     ::= 'throw' [<expression>]

<func-def>       ::= 'Func' '(' <identifier> {',' <identifier>} ')'
                     {<statement>}
                     'EndFunc'

<if-stmt>        ::= 'if' '(' <expression> ')'
                     {<statement>}
                     {'elseIf' '(' <expression> ')' {<statement>}}
                     ['else' {<statement>}]
                     'endif'

<for-stmt>       ::= 'For' '(' <identifier> ',' <expression> ',' <expression>
                     [',' <expression>] ')'
                     {<statement>}
                     'endfor'

<while-stmt>     ::= 'while' '(' <expression> ')'
                     {<statement>}
                     'endwhile'

<try-stmt>       ::= 'try'
                     {<statement>}
                     'IfErrorToDo'
                     {<statement>}
                     'endTry'

<class-def>      ::= 'Class' '(' <identifier> {',' <identifier>} ')'
                     {<method-def>}
                     'EndClass'
<method-def>     ::= 'Func' '(' <identifier> {',' <identifier>} ')'
                     {<statement>}
                     'EndFunc'

<module-def>     ::= 'Module' '(' <identifier> ')'
                     {<statement>}
                     'EndModule'

<import-stmt>    ::= 'import' <identifier>

<thread-block>   ::= 'Thread'
                     {<statement>}
                     'endThread'

<const-def>      ::= 'CboxS' '(' <identifier> ',' <expression> ')'

<input-stmt>     ::= ('Input' | 'InputInt') '(' <expression> [',' <identifier>] ')'
<print-stmt>     ::= 'PrintLog' '(' <expression> {',' <expression>} ')'
<box-stmt>       ::= 'box' '(' <identifier> ',' <expression> ')'
                   | 'boxS' '(' <expression> ',' <identifier> ',' <expression> ')'

<getpack-stmt>   ::= 'GetPack' '(' <expression> ')'
<getcpack-stmt>  ::= 'GetCPack' '(' <expression> ')'

<expression-statement> ::= <call>
<call>           ::= <identifier> '(' [<arg-list>] ')'
<arg-list>       ::= <expression> {',' <expression>}
```

### 22.3 表达式文法

```bnf
<expression>     ::= <logical-or>
<logical-or>     ::= <logical-and> {'||' <logical-and>}
<logical-and>    ::= <logical-not> {'&&' <logical-not>}
<logical-not>    ::= '!' <logical-not> | <comparison>
<comparison>     ::= <additive> [<comp-op> <additive>]
<comp-op>        ::= '==' | '!=' | '>' | '<' | '>=' | '<='
<additive>       ::= <multiplicative> {('+' | '-') <multiplicative>}
<multiplicative>::= <unary> {('*' | '/') <unary>}
<unary>          ::= ('+' | '-') <unary> | <primary>
<primary>        ::= <literal> | <identifier> | <call> | <indexing>
                   | <field-access> | '(' <expression> ')' | <lambda>
<indexing>       ::= <identifier> '[' <expression> ']'
<field-access>   ::= (<identifier> | 'this') '.' <identifier>
<lambda>         ::= 'lambda' '(' <expression> ',' <expression> ')'
<literal>        ::= <number> | <string>
```

---

## 附录 A: 完整程序示例

### A.1 闭包计数器

```python
Func(makeCounter, start)
  count = start
  return lambda("", "count = count + 1\nreturn count")
EndFunc

counter = makeCounter(10)
PrintLog(counter())    # 11
PrintLog(counter())    # 12
PrintLog(counter())    # 13
```

### A.2 模块化数学库

```python
Module(Math)
  Func(factorial, n)
    if(n <= 1)
      return 1
    endif
    return n * factorial(n - 1)
  EndFunc
  Func(isPrime, n)
    if(n < 2)
      return false
    endif
    i = 2
    while(i * i <= n)
      if(mod(n, i) == 0)
        return false
      endif
      i += 1
    endwhile
    return true
  EndFunc
EndModule

PrintLog(Math.factorial(10))    # 3628800
PrintLog(Math.isPrime(97))      # true
```

### A.3 向量类与运算符重载

```python
Class(Vec3, x, y, z)
  Func(__add__, other)
    r = new(Vec3)
    r.x = this.x + other.x
    r.y = this.y + other.y
    r.z = this.z + other.z
    return r
  EndFunc
  Func(__mul__, other)
    r = new(Vec3)
    r.x = this.x * other.x
    r.y = this.y * other.y
    r.z = this.z * other.z
    return r
  EndFunc
  Func(length)
    return sqrt(this.x*this.x + this.y*this.y + this.z*this.z)
  EndFunc
EndClass

a = new(Vec3)
a.x = 1
a.y = 2
a.z = 2
b = new(Vec3)
b.x = 3
b.y = 4
b.z = 12

c = a + b
PrintLog(c.x, c.y, c.z)    # 4 6 14
PrintLog(c.length())       # sqrt(16+36+196) = sqrt(248) ≈ 15.7480

d = a * b
PrintLog(d.x, d.y, d.z)    # 3 8 24
```

### A.4 迭代器遍历

```python
# 列表求和
nums = list(10, 20, 30, 40, 50)
it = iter(nums)
total = 0
while(hasNext(it))
  total = total + next(it)
endwhile
PrintLog("Sum:", total)    # 150

# 字典遍历
scores = dict()
dictSet(scores, "Alice", "95")
dictSet(scores, "Bob", "87")
dictSet(scores, "Carol", "92")

it = iter(scores)
while(hasNext(it))
  name = next(it)
  score = iterValue(it)
  PrintLog(name, ":", score)
endwhile
```

### A.5 错误处理

```python
Func(divide, a, b)
  try
    if(b == 0)
      throw "division by zero"
    endif
    return a / b
  IfErrorToDo
    PrintLog("Error:", lastError())
    return 0
  endTry
EndFunc

PrintLog(divide(10, 2))    # 5
PrintLog(divide(10, 0))    # Error: throw: division by zero \n 0
```

---

## 附录 B: 变更日志

| 版本 | 日期 | 变更 |
|------|------|------|
| SLS-1.0 | 2025 | 初始版本。规定: 词法、类型、表达式、语句、函数、闭包、模块、类、运算符重载、迭代器、错误处理、线程、标准库。 |

---

## 附录 C: 参考文献

- [ECMA-334] C# Language Specification
- [ISO/IEC 14882] C++ Standard
- [ISO/IEC 9899] C Standard
- [Python] The Python Language Reference
- [ECMA-262] ECMAScript Language Specification

---

*End of SimpleIDE Language Standard (SLS) v1.0*
