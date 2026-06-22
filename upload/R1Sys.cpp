#include <windows.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <ctime>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <climits>
#include <cmath>
#include <algorithm>
#include <cstring>

// 主窗口控件
HWND g_hMainWnd, g_hMultiEdit;
// 输出窗口控件 (独立弹出窗口)
HWND g_hOutputWnd, g_hOutput, g_hInput, g_hSendBtn;

// 当前字体
HFONT g_hEditorFont = NULL;
int g_fontSize = 15;
char g_fontName[64] = "Consolas";

// 菜单命令 ID
#define IDM_RUN         2001
#define IDM_STEP        2002
#define IDM_CLEAR       2003
#define IDM_SAVE        2004
#define IDM_OPEN        2005
#define IDM_HELP        2006
#define IDM_FONT        2007
#define IDM_FONT_INC    2008
#define IDM_FONT_DEC    2009
#define IDM_FONT_RESET  2010
#define IDM_EXIT        2011

// 语法高亮颜色 (RGB)
#define COLOR_COMMENT RGB(0,128,0)      // 绿色 - 注释
#define COLOR_KEYWORD  RGB(0,0,200)     // 蓝色 - 关键字
#define COLOR_STRING   RGB(200,0,0)     // 红色 - 字符串
#define COLOR_NUMBER   RGB(128,0,128)   // 紫色 - 数字
#define COLOR_FUNC     RGB(0,100,100)   // 青色 - 函数名
#define COLOR_DEFAULT  RGB(0,0,0)       // 黑色 - 默认
bool g_highlightEnabled = true;

bool g_outputWndCreated = false;
std::vector<std::string> g_multiLines;
int g_currentLine = 0;
bool g_waitingInput = false;
std::string g_inputPrompt, g_inputVarName;
int g_inputType = 0; // 0 = string, 1 = int
// 表达式模式 Input: 当 Input() 作为函数参数在表达式中使用时
bool g_inputPending = false;   // 是否有待处理的表达式输入
std::string g_inputResult;     // 表达式输入的结果值
bool g_returning = false;

bool g_breakLoop = false;
bool g_inTryBlock = false;  // 是否在 try 块中

// ---- 列表系统 ----
std::map<std::string, std::vector<std::string> > g_lists;

bool isListVar(const std::string& name) {
    return g_lists.find(name) != g_lists.end();
}
std::vector<std::string>& getList(const std::string& name) {
    static std::vector<std::string> empty;
    auto it = g_lists.find(name);
    return it != g_lists.end() ? it->second : empty;
}
void setList(const std::string& name, const std::vector<std::string>& vals) {
    g_lists[name] = vals;
}
bool eraseList(const std::string& name) {
    return g_lists.erase(name) > 0;
}
bool isListIndexExpr(const std::string& s, std::string& listName, std::string& idxExpr);

// ---- 常量系统 (CboxS) ----
// 常量存储: 名字 -> 值, 一旦定义不可修改
std::map<std::string, std::string> g_consts;

bool isConstVar(const std::string& name) {
    return g_consts.find(name) != g_consts.end();
}
std::string getConst(const std::string& name) {
    auto it = g_consts.find(name);
    return it != g_consts.end() ? it->second : "";
}
void setConst(const std::string& name, const std::string& v) {
    g_consts[name] = v;
}

// ---- 字典系统 (Dict) ----
// 字典存储: 变量名 -> 键值对映射
std::map<std::string, std::map<std::string, std::string> > g_dicts;

bool isDictVar(const std::string& name) {
    return g_dicts.find(name) != g_dicts.end();
}
std::map<std::string, std::string>& getDict(const std::string& name) {
    static std::map<std::string, std::string> empty;
    auto it = g_dicts.find(name);
    return it != g_dicts.end() ? it->second : empty;
}
void setDict(const std::string& name, const std::map<std::string, std::string>& vals) {
    g_dicts[name] = vals;
}
bool eraseDict(const std::string& name) {
    return g_dicts.erase(name) > 0;
}

// ---- 库导入系统 (GetPack) ----
// 库文件存放在可执行文件同目录的 libs/ 子文件夹下
// 库文件就是普通的 .sal 代码文件,用我们的语言编写
// 用 GetPack("库名") 导入,自动加载 libs/库名.sal 并执行
// 库中定义的 Func/全局变量在导入后即可使用
void runLines(const std::vector<std::string>& lines);  // 前向声明
std::vector<std::string> g_loadedPacks;  // 已加载的库名列表

bool isPackLoaded(const std::string& name) {
    for (auto& p : g_loadedPacks) if (p == name) return true;
    return false;
}

// 获取库文件路径: 可执行文件目录/libs/名称.sal
std::string getPackPath(const std::string& name) {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    else dir = ".";
    return dir + "\\libs\\" + name + ".sal";
}

// 从文件读取所有行
std::vector<std::string> readLinesFromFile(const std::string& path) {
    std::vector<std::string> lines;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return lines;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); return lines; }
    if (fsize > 1024 * 1024) fsize = 1024 * 1024;
    std::vector<char> buf(fsize + 1, 0);
    long readLen = fread(buf.data(), 1, fsize, fp);
    buf[readLen] = 0;
    fclose(fp);
    // 跳过 SALF1 头
    char* start = buf.data();
    if (readLen >= 5 && strncmp(start, "SALF1", 5) == 0) start += 5;
    std::string t(start);
    std::istringstream iss(t);
    std::string l;
    while (std::getline(iss, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        lines.push_back(l);
    }
    return lines;
}

// loadPack 在错误系统定义之后实现
bool loadPack(const std::string& name);

// ---- 恢复机制：当 Input 中断执行时保存断点状态 ----
// ResumeFrame 保存一个执行层级的断点信息
struct ResumeFrame {
    int type;  // 0=SEQ(顺序行), 1=FOR(For循环), 2=WHILE(while循环)
    // SEQ: lines 中从 nextIndex 开始的行需要执行
    std::vector<std::string> lines;
    size_t nextIndex;
    // FOR: 循环状态
    std::string forVar;
    int forVal;
    int forEnd;
    int forStep;
    bool isExprStep;
    std::string forExprStepArg;
    std::vector<std::string> forBody;
    // WHILE: 循环状态
    std::string whileCond;
    std::vector<std::string> whileBody;
};
// g_pendingFrames: 当前中断期间收集的帧（从内到外）
// g_resumeStack: 待恢复的帧队列（从外到内处理）
std::vector<ResumeFrame> g_pendingFrames;
std::deque<ResumeFrame> g_resumeStack;

// ---- 辅助函数 ----

// 自定义消息：向输出窗口追加文本
#define WM_APP_APPENDTEXT (WM_APP + 1)
// 自定义消息：输入完成后通知
#define WM_APP_INPUTDONE (WM_APP + 2)

void guiOutput(const std::string& text) {
    if (!g_hOutputWnd) return;
    if (g_hOutput) {
        // 同步追加到输出框，确保顺序正确
        int len = GetWindowTextLength(g_hOutput);
        SendMessage(g_hOutput, EM_SETSEL, len, len);
        SendMessage(g_hOutput, EM_REPLACESEL, 0, (LPARAM)text.c_str());
    }
}

std::string getTime() {
    time_t t = time(0);
    struct tm lt;
    if (localtime_s(&lt, &t) != 0) return "00:00:00";
    char b[64];
    strftime(b, sizeof(b), "%H:%M:%S", &lt);
    return b;
}
void Log(const std::string& s) { guiOutput("[" + getTime() + "] " + s + "\r\n"); }

// ---- 错误系统 ----
struct ErrorInfo {
    int code;
    std::string message;
    int line;
    std::string context;
};
std::vector<ErrorInfo> g_errors;
int g_currentLineNo = 0;

enum ErrorCode {
    ERR_NONE = 0,
    ERR_SYNTAX = 1001,
    ERR_UNDEFINED_VAR = 1002,
    ERR_UNDEFINED_FUNC = 1003,
    ERR_INVALID_ARG = 1004,
    ERR_ARG_COUNT = 1005,
    ERR_INVALID_NUM = 1006,
    ERR_INVALID_STR = 1007,
    ERR_INVALID_LIST = 1008,
    ERR_DIV_ZERO = 1009,
    ERR_OUT_OF_RANGE = 1010,
    ERR_INVALID_TYPE = 1011,
    ERR_INVALID_NAME = 1012,
    ERR_FILE_IO = 1013,
    ERR_STACK_OVERFLOW = 1014,
    ERR_INTERNAL = 1099
};

void clearErrors() { g_errors.clear(); }
void reportError(int code, const std::string& msg, const std::string& context = "") {
    ErrorInfo e;
    e.code = code;
    e.message = msg;
    e.line = g_currentLineNo;
    e.context = context;
    g_errors.push_back(e);
    char prefix[128];
    sprintf(prefix, "[Error E%d L%d] ", code, g_currentLineNo);
    Log(prefix + msg + (context.empty() ? "" : " (" + context + ")"));
    // 在 try 块中出错,设置中断标志
    if (g_inTryBlock) g_breakLoop = true;
}
bool hasErrors() { return !g_errors.empty(); }
void printAllErrors() {
    if (g_errors.empty()) return;
    Log("=== Error Summary (" + std::to_string(g_errors.size()) + " errors) ===");
    for (auto& e : g_errors) {
        char buf[256];
        sprintf(buf, "  E%d L%d: ", e.code, e.line);
        Log(buf + e.message + (e.context.empty() ? "" : " | " + e.context));
    }
}

// loadPack 实现 (在错误系统之后)
bool loadPack(const std::string& name) {
    if (isPackLoaded(name)) return true;  // 已加载,跳过
    std::string path = getPackPath(name);
    std::vector<std::string> lines = readLinesFromFile(path);
    if (lines.empty()) {
        reportError(ERR_FILE_IO, "cannot load pack: " + name, path);
        return false;
    }
    g_loadedPacks.push_back(name);
    // 执行库代码 (定义函数/变量/常量等)
    runLines(lines);
    Log("[Pack] loaded: " + name + " (" + std::to_string(lines.size()) + " lines)");
    return true;
}

// ---- C++ 库扩展系统 (GetCPack) ----
// C++ 库编译为 DLL,放在可执行文件同目录的 cpacks/ 文件夹下
// 用 GetCPack("库名") 加载 cpacks/库名.dll
// DLL 必须导出函数: extern "C" __declspec(dllexport) void registerPack()
// registerPack 中调用 registerNativeFunc("函数名", nativeFunc) 注册自定义函数
// 原生函数类型: typedef const char* (*NativeFunc)(const char** args, int argc);
typedef const char* (*NativeFunc)(const char** args, int argc);

struct NativeFuncEntry {
    std::string name;
    NativeFunc func;
};
std::vector<NativeFuncEntry> g_nativeFuncs;
std::vector<HMODULE> g_loadedDlls;

// 注册C++原生函数 (DLL调用)
extern "C" __declspec(dllexport) void registerNativeFunc(const char* name, NativeFunc func) {
    g_nativeFuncs.push_back({std::string(name), func});
}

// 查找并调用C++原生函数
bool callNativeFunc(const std::string& name, const std::vector<std::string>& args, std::string& result) {
    for (auto& e : g_nativeFuncs) {
        if (e.name == name && e.func) {
            std::vector<const char*> cargs;
            for (auto& a : args) cargs.push_back(a.c_str());
            const char* r = e.func(cargs.data(), (int)cargs.size());
            result = r ? std::string(r) : "";
            return true;
        }
    }
    return false;
}

// 导入C++库
// 检查文件是否存在
static bool fileExists(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp) { fclose(fp); return true; }
    return false;
}

bool loadCPack(const std::string& name) {
    // 检查是否已加载
    for (auto& e : g_nativeFuncs) {
        if (e.name == "__cpack_" + name + "_loaded") return true;
    }
    // 获取可执行文件目录
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    else dir = ".";

    std::string dllPath = dir + "\\cpacks\\" + name + ".dll";
    std::string cppPath = dir + "\\cpacks\\" + name + ".cpp";

    // 1. 优先尝试加载已编译的 DLL
    HMODULE hDll = NULL;
    if (fileExists(dllPath)) {
        hDll = LoadLibraryA(dllPath.c_str());
    }

    // 2. DLL 不存在或加载失败，检查 .cpp 源码并自动编译
    if (!hDll && fileExists(cppPath)) {
        Log("[CPack] compiling: " + name + ".cpp");
        // 编译命令: g++ -shared -std=c++11 -o cpacks/名.dll cpacks/名.cpp
        // 不需要链接 exe，registerNativeFunc 通过函数指针传递
        std::string cmd = "g++ -shared -std=c++11 -o \"" + dllPath + "\" \"" + cppPath + "\"";
        // 静默编译，输出重定向到临时文件
        cmd += " 2>\"" + dir + "\\cpacks\\__compile_err.txt\"";

        int ret = system(cmd.c_str());
        if (ret != 0) {
            // 编译失败，读取错误信息
            FILE* ferr = fopen((dir + "\\cpacks\\__compile_err.txt").c_str(), "r");
            if (ferr) {
                char errbuf[1024] = {0};
                fread(errbuf, 1, sizeof(errbuf) - 1, ferr);
                fclose(ferr);
                std::string err(errbuf);
                // 只取第一行
                size_t nl = err.find('\n');
                if (nl != std::string::npos) err = err.substr(0, nl);
                reportError(ERR_FILE_IO, "compile failed: " + name, err);
            } else {
                reportError(ERR_FILE_IO, "compile failed: " + name, "g++ not found or error");
            }
            return false;
        }
        // 编译成功，加载 DLL
        hDll = LoadLibraryA(dllPath.c_str());
        if (!hDll) {
            reportError(ERR_FILE_IO, "compiled but cannot load: " + name, dllPath);
            return false;
        }
        Log("[CPack] compiled OK: " + name);
    }

    // 3. DLL 和 CPP 都不存在
    if (!hDll) {
        reportError(ERR_FILE_IO, "cannot load cpack: " + name, "no .dll or .cpp found in cpacks/");
        return false;
    }

    // 查找注册函数 — 支持两种写法和多种符号名
    // 新写法: registerPack(RegisterFunc reg) — 接收函数指针
    // 旧写法: registerPack() — 无参数,DLL自己声明 dllimport
    typedef void (*RegisterPackNewFunc)(void(*)(const char*, NativeFunc));
    typedef void (*RegisterPackOldFunc)();

    RegisterPackNewFunc registerPackNew = NULL;
    RegisterPackOldFunc registerPackOld = NULL;

    // 尝试多种符号名 (不同编译器可能加下划线前缀)
    const char* symNames[] = {"registerPack", "_registerPack", NULL};
    for (int si = 0; symNames[si]; si++) {
        FARPROC p = GetProcAddress(hDll, symNames[si]);
        if (p) {
            // 先尝试当新写法调用 (带参数)
            registerPackNew = (RegisterPackNewFunc)p;
            break;
        }
    }

    if (registerPackNew) {
        // 新写法: 传 registerNativeFunc 函数指针
        Log("[CPack] calling registerPack...");
        registerPackNew(registerNativeFunc);
        Log("[CPack] registered " + std::to_string(g_nativeFuncs.size()) + " functions");
    } else {
        // 都没找到
        reportError(ERR_FILE_IO, "cpack missing registerPack: " + name, "DLL does not export registerPack");
        FreeLibrary(hDll);
        return false;
    }
    g_loadedDlls.push_back(hDll);
    g_nativeFuncs.push_back({"__cpack_" + name + "_loaded", NULL});
    Log("[CPack] loaded: " + name);
    return true;
}

// ---- ±äÁ¿¹ÜÀí ----

std::map<std::string, std::string> g_vars;
void setVar(const std::string& n, const std::string& v) {
    // 常量不可修改
    if (isConstVar(n)) {
        reportError(ERR_INVALID_ARG, "cannot modify constant: " + n);
        return;
    }
    g_vars[n] = v;
}
std::string getVar(const std::string& n) {
    // 优先返回常量
    if (isConstVar(n)) return getConst(n);
    auto it = g_vars.find(n); return it != g_vars.end() ? it->second : "";
}
bool hasVar(const std::string& n) { return g_vars.find(n) != g_vars.end() || isConstVar(n); }
bool eraseVar(const std::string& n) {
    if (isConstVar(n)) {
        reportError(ERR_INVALID_ARG, "cannot delete constant: " + n);
        return false;
    }
    return g_vars.erase(n) > 0;
}

// ---- È¥¿Õ°× ----
static inline void trimLeft(std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i > 0) s.erase(0, i);
}
static inline void trimRight(std::string& s) {
    size_t i = s.size();
    while (i > 0 && (s[i-1] == ' ' || s[i-1] == '\t')) i--;
    if (i < s.size()) s.erase(i);
}
static inline void trim(std::string& s) { trimLeft(s); trimRight(s); }

// ---- isIdentifier: Ö§³ÖÊ××ÖÄ¸ÏÂ»®Ïß£¬ºóÐø¿Éº¬Êý×Ö ----
bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    unsigned char c0 = (unsigned char)s[0];
    if (!isalpha(c0) && s[0] != '_') return false;
    for (size_t i = 1; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && s[i] != '_') return false;
    }
    return true;
}

// 判断变量名是否为列表引用语法: listName[i]
bool isListIndexExpr(const std::string& s, std::string& listName, std::string& idxExpr) {
    size_t lb = s.find('[');
    if (lb == std::string::npos) return false;
    // 找匹配的 ] (考虑嵌套)
    int depth = 1;
    size_t rb = std::string::npos;
    for (size_t i = lb + 1; i < s.size(); i++) {
        if (s[i] == '[') depth++;
        else if (s[i] == ']') { depth--; if (depth == 0) { rb = i; break; } }
    }
    if (rb == std::string::npos) return false;
    // 匹配的 ] 必须是最后一个字符(纯下标表达式)
    if (rb != s.size() - 1) return false;
    listName = s.substr(0, lb);
    idxExpr = s.substr(lb + 1, rb - lb - 1);
    trim(listName);
    trim(idxExpr);
    return isIdentifier(listName);
}

// ÊÇ·ñÊÇ´¿Êý×Ö£¨°üÀ¨Ð¡Êý¡¢¸ººÅ¡¢¿ÆÑ§¼ÆÊý·¨£©
bool isPureNumber(const std::string& s) {
    if (s.empty()) return false;
    bool hasDigit = false;
    bool hasDot = false;
    bool hasE = false;
    size_t start = 0;
    if (s[0] == '+' || s[0] == '-') start = 1;
    for (size_t i = start; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { hasDigit = true; }
        else if (c == '.' && !hasDot && !hasE) { hasDot = true; }
        else if ((c == 'e' || c == 'E') && hasDigit && !hasE) {
            hasE = true;
            if (i + 1 < s.size() && (s[i+1] == '+' || s[i+1] == '-')) i++;
        }
        else return false;
    }
    // 指数后必须有数字
    if (hasE) {
        size_t ePos = std::string::npos;
        for (size_t i = 0; i < s.size(); i++) { if (s[i] == 'e' || s[i] == 'E') { ePos = i; break; } }
        if (ePos != std::string::npos) {
            size_t j = ePos + 1;
            if (j < s.size() && (s[j] == '+' || s[j] == '-')) j++;
            if (j >= s.size()) return false;
            for (; j < s.size(); j++) { if (s[j] < '0' || s[j] > '9') return false; }
        }
    }
    return hasDigit;
}

// ---- º¯Êý¶¨Òå ----
struct Function { std::string name; std::vector<std::string> params; std::vector<std::string> body; };
std::map<std::string, Function> g_funcs;

// ---- 前向声明 ----
void runCode(const std::string& input);
void runLines(const std::vector<std::string>& lines);
std::string evalExpr(const std::string& expr);
std::string callFunc(const std::string& name, const std::vector<std::string>& args);
bool builtinCall(const std::string& name, const std::vector<std::string>& args, std::string& result);

// ---- calc: ÍêÈ«ÖØÐ´£¬Ö§³ÖÀ¨ºÅ¡¢Ò»Ôª¸ººÅ¡¢¿ÆÑ§¼ÆÊý·¨ ----
// Ê¹ÓÃÕ»Ê½¼ÆËã£¬×¼È·´¦ÀíÔËËã·ûÓÅÏÈ¼¶ºÍÀ¨ºÅ

static bool findTopLevelOp(const std::string& s, char op1, char op2, size_t& outPos) {
    int depth = 0;
    bool inS = false, inD = false;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        char c = s[i];
        if (inS) { if (c == '\'') inS = false; continue; }
        if (inD) { if (c == '"') inD = false; continue; }
        if (c == '\'') { inS = true; continue; }
        if (c == '"') { inD = true; continue; }
        if (c == ')') depth++;
        else if (c == '(') { depth--; continue; }
        if (depth != 0) continue;
        if (c == op1 || c == op2) {
            if (c == '-' || c == '+') {
                if (i == 0) continue;
                int p = i - 1;
                while (p >= 0 && (s[p] == ' ' || s[p] == '\t')) p--;
                if (p < 0) continue;
                char prev = s[p];
                if (prev == '(' || prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '^') continue;
            }
            outPos = (size_t)i;
            return true;
        }
    }
    return false;
}

static bool findTopLevelMulDiv(const std::string& s, char op, size_t& outPos) {
    int depth = 0;
    bool inS = false, inD = false;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        char c = s[i];
        if (inS) { if (c == '\'') inS = false; continue; }
        if (inD) { if (c == '"') inD = false; continue; }
        if (c == '\'') { inS = true; continue; }
        if (c == '"') { inD = true; continue; }
        if (c == ')') depth++;
        else if (c == '(') { depth--; continue; }
        if (depth != 0) continue;
        if (c == op) { outPos = (size_t)i; return true; }
    }
    return false;
}

// ×Ó±í´ïÊ½ÖÐÊÇ·ñº¬×Ö·û´®ÁÐ£¨±ÜÃâ calc °Ñ×Ö·û´®×ª³É 0 Ôì³É×Ö·û´®±È½ÏÎóÅÐ£©
static bool hasStringLiteral(const std::string& s) {
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"' || s[i] == '\'') return true;
    }
    return false;
}

double calc(const std::string& e) {
    std::string s = e;
    trim(s);
    if (s.empty()) return 0;

    // Èôº¬×Ö·û´®ÁÐ£¬ËµÃ÷²»ÊÇ´¿ÊýÖµ±í´ïÊ½£¬·µ»Ø 0
    // £¨×Ö·û´®±È½ÏÓ¦ÔÚ cond ÖÐµ¥¶À´¦Àí£©
    if (hasStringLiteral(s)) return 0;

    // È¥³ýÍâ²ãÀ¨ºÅ
    while (s.size() >= 2 && s[0] == '(' && s.back() == ')') {
        int depth = 0; bool isOuter = true;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') { depth--; if (depth == 0 && i < s.size() - 1) { isOuter = false; break; } }
        }
        if (isOuter) s = s.substr(1, s.size() - 2);
        else break;
    }
    if (s.empty()) return 0;

    // ´¿Êý×Ö
    if (isPureNumber(s)) return atof(s.c_str());

    // ±äÁ¿
    if (hasVar(s)) return atof(getVar(s).c_str());

    // ²éÕÒ + / - £¨×îµÍÓÅÏÈ¼¶£©
    size_t pos;
    if (findTopLevelOp(s, '+', '-', pos)) {
        char op = s[pos];
        double l = calc(s.substr(0, pos));
        double r = calc(s.substr(pos + 1));
        return op == '+' ? l + r : l - r;
    }

    // ²éÕÒ * / £¨ÖÐ¼äÓÅÏÈ¼¶£©
    if (findTopLevelMulDiv(s, '*', pos)) {
        return calc(s.substr(0, pos)) * calc(s.substr(pos + 1));
    }
    if (findTopLevelMulDiv(s, '/', pos)) {
        double r = calc(s.substr(pos + 1));
        if (r == 0) return 0;
        return calc(s.substr(0, pos)) / r;
    }

    // Ò»Ôª¸ººÅ
    if (s.size() >= 2 && s[0] == '-') {
        return -calc(s.substr(1));
    }
    if (s.size() >= 2 && s[0] == '+') {
        return calc(s.substr(1));
    }

    // ±äÁ¿
    if (hasVar(s)) return atof(getVar(s).c_str());
    return atof(s.c_str());
}

// ---- cond: ÖØÐ´£¬Ö§³Ö×Ö·û´®±È½Ï¡¢ÕýÈ·Ìø¹ýÒýºÅ ----
// ·µ»ØÖµ£º0 = false£¬1 = true£¬-1 = ÎÞ±È½Ï·ûºÅ
static int tryCompare(const std::string& s, std::string& lhs, std::string& rhs, std::string& op) {
    std::string ops[] = {"==","!=",">=","<=",">","<"};
    for (int i = 0; i < 6; i++) {
        const std::string& o = ops[i];
        size_t p = std::string::npos;
        // É¨ÃèÊ±Ìø¹ýÒýºÅÄÚµÄÄÚÈÝ
        bool inS = false, inD = false;
        int depth = 0;
        for (size_t k = 0; k + o.size() <= s.size(); k++) {
            char c = s[k];
            if (inS) { if (c == '\'') inS = false; continue; }
            if (inD) { if (c == '"') inD = false; continue; }
            if (c == '\'') { inS = true; continue; }
            if (c == '"') { inD = true; continue; }
            if (c == '(') depth++;
            else if (c == ')') { if (depth > 0) depth--; }

            // ´¦Àí >= <= != == ÐèÒªÌø¹ýµ¥×Ö·ûµÄ > <
            if (o == ">" || o == "<") {
                // ±ÜÃâ°Ñ >= µÄ > µ±³É > ÔËËã·û
                if (c == o[0]) {
                    if (k + 1 < s.size() && s[k+1] == '=') { k++; continue; }
                    if (depth == 0) { p = k; break; }
                }
            } else {
                if (s.compare(k, o.size(), o) == 0) {
                    if (depth == 0) { p = k; break; }
                }
            }
        }
        if (p != std::string::npos) {
            lhs = s.substr(0, p);
            rhs = s.substr(p + o.size());
            op = o;
            return 1;
        }
    }
    return 0;
}

// ÅÐ¶Ï±í´ïÊ½ÊÇ·ñÊÇ×Ö·û´®ÀàÐÍ
static bool isStringExpr(const std::string& s) {
    std::string t = s;
    trim(t);
    if (t.empty()) return false;
    if (t[0] == '"' || t[0] == '\'') return true;
    // °üº¬×Ö·û´®Á¬½Ó
    if (t.find('"') != std::string::npos || t.find('\'') != std::string::npos) return true;
    // ÒÑÖª×Ö·û´®±äÁ¿
    if (hasVar(t)) {
        const std::string& v = getVar(t);
        if (!isPureNumber(v)) return true;
    }
    return false;
}

bool cond(const std::string& c) {
    std::string s = c;
    trim(s);
    if (s.empty()) return false;

    std::string lhs, rhs, op;
    if (tryCompare(s, lhs, rhs, op)) {
        trim(lhs); trim(rhs);
        // Èç¹ûÁ½±ß¶¼ÊÇ×Ö·û´®ÀàÐÍ£¬×÷×Ö·û´®±È½Ï
        bool lIsStr = isStringExpr(lhs);
        bool rIsStr = isStringExpr(rhs);
        if (lIsStr || rIsStr) {
            std::string lv = evalExpr(lhs);
            std::string rv = evalExpr(rhs);
            if (op == "==") return lv == rv;
            if (op == "!=") return lv != rv;
            if (op == ">")  return lv > rv;
            if (op == "<")  return lv < rv;
            if (op == ">=") return lv >= rv;
            if (op == "<=") return lv <= rv;
        } else {
            double lv = calc(lhs);
            double rv = calc(rhs);
            if (op == "==") return lv == rv;
            if (op == "!=") return lv != rv;
            if (op == ">=") return lv >= rv;
            if (op == "<=") return lv <= rv;
            if (op == ">")  return lv > rv;
            if (op == "<")  return lv < rv;
        }
    }

    if (s == "true") return true;
    if (s == "false") return false;
    if (hasVar(s)) {
        const std::string& v = getVar(s);
        if (v == "true") return true;
        if (v == "false") return false;
        return atof(v.c_str()) != 0;
    }
    if (isPureNumber(s)) return atof(s.c_str()) != 0;
    return calc(s) != 0;
}

// ---- getArg: ÖØÐ´£¬ÕýÈ·´¦ÀíÒýºÅ×Ö·û´®¡¢ÔËËã·û¡¢×Ó²ÎÊý ----
std::string getArg(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos >= s.size()) return "";
    std::string r;
    char c = s[pos];
    if (c == '\'' || c == '"') {
        // 保留外层引号，让下游 evalExpr/resolveFuncArg 正确识别为字符串字面量
        char q = c;
        r += c; pos++;
        while (pos < s.size() && s[pos] != q) {
            if (s[pos] == '\\' && pos + 1 < s.size()) {
                r += s[pos]; r += s[pos+1];
                pos += 2;
            } else {
                r += s[pos]; pos++;
            }
        }
        if (pos < s.size()) { r += s[pos]; pos++; }
        return r;
    }
    // 非引号参数：根据运算符低级界定，而不是自动截断运算符
    int depth = 0;
    bool inS = false, inD = false;
    while (pos < s.size()) {
        char ch = s[pos];
        if (inS) { r += ch; pos++; if (ch == '\'') inS = false; continue; }
        if (inD) { r += ch; pos++; if (ch == '"') inD = false; continue; }
        if (ch == '\'' || ch == '"') { r += ch; pos++; if (ch == '\'') inS = true; else inD = true; continue; }
        if (ch == '(') { depth++; r += ch; pos++; continue; }
        if (ch == ')') { if (depth == 0) break; depth--; r += ch; pos++; continue; }
        if (ch == ',' && depth == 0) break;
        r += ch; pos++;
    }
    std::string rs = r;
    trim(rs);
    return rs;
}

// ---- callFunc: ÍêÈ«ÖØÐ´£¬ÍêÉÆ×÷ÓÃÓò¸ôÀë ----
std::string callFunc(const std::string& name, const std::vector<std::string>& args) {
    auto it = g_funcs.find(name);
    if (it == g_funcs.end()) return "";
    Function& f = it->second;

    // 保存调用前的完整全局变量状态，实现作用域隔离
    std::map<std::string, std::string> savedGlobalVars = g_vars;

    // 绑定参数（覆盖/新建到当前 g_vars）
    for (size_t i = 0; i < f.params.size(); i++) {
        setVar(f.params[i], i < args.size() ? args[i] : "");
    }

    // 重置 __ret__ 与控制标志
    setVar("__ret__", "");
    bool savedReturning = g_returning;
    g_returning = false;
    bool savedBreak = g_breakLoop;
    g_breakLoop = false;

    runLines(f.body);

    std::string ret = getVar("__ret__");

    // 恢复全局变量状态（清除所有局部变量和参数，恢复被覆盖的全局变量）
    g_returning = savedReturning;
    g_breakLoop = savedBreak;
    g_vars = savedGlobalVars;

    return ret;
}

// ---- evalExpr: ÍêÈ«ÖØÐ´ ----
// Ö§³Ö£º×Ö·û´®×ÖÃæÁ¿¡¢±äÁ¿Ãû¡¢ÊýÖµ¼ÆËã¡¢×Ö·û´®Æ´½Ó¡¢º¯Êýµ÷ÓÃ¡¢À¨ºÅ

// ½âÎöÒýºÅ×Ö·û´®£¨È¥³ýÒýºÅ¡¢´¦Àí×ªÒå£©
static std::string parseStringLiteral(const std::string& s) {
    std::string r;
    for (size_t i = 1; i < s.size() - 1; i++) {
        if (s[i] == '\\' && i + 1 < s.size() - 1) {
            char nx = s[i+1];
            if (nx == 'n') r += '\n';
            else if (nx == 't') r += '\t';
            else if (nx == 'r') r += '\r';
            else if (nx == '\\') r += '\\';
            else if (nx == '"') r += '"';
            else if (nx == '\'') r += '\'';
            else r += nx;
            i++;
        } else {
            r += s[i];
        }
    }
    return r;
}

static bool isQuoted(const std::string& s) {
    if (s.size() < 2) return false;
    if (s[0] != '"' && s[0] != '\'') return false;
    char q = s[0];
    for (size_t i = 1; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) { i++; continue; }
        if (s[i] == q) return i == s.size() - 1;
    }
    return false;
}

// Ñ°ÕÒ×îÍâ²ãÎ´Æ¥ÅäµÄ + £¨×Ö·û´®Æ´½Ó£©£¬Ìø¹ýÒýºÅÄÚµÄ +
static bool findConcatPlus(const std::string& s, size_t& outPos) {
    int depth = 0;
    bool inS = false, inD = false;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        char c = s[i];
        if (inS) { if (c == '\'') inS = false; continue; }
        if (inD) { if (c == '"') inD = false; continue; }
        if (c == '\'') { inS = true; continue; }
        if (c == '"') { inD = true; continue; }
        if (c == ')') depth++;
        else if (c == '(') { depth--; continue; }
        if (depth != 0) continue;
        if (c == '+') {
            // ·ÇÒ»ÔªºÅ£ºÇ°Ãæ·ÇÔËËã·û/×óÀ¨ºÅ
            if (i == 0) continue;
            char prev = s[i-1];
            if (prev == '(' || prev == '+' || prev == '-' || prev == '*' || prev == '/') continue;
            outPos = (size_t)i;
            return true;
        }
    }
    return false;
}

// ½«±í´ïÊ½µÄÖµ×ªÎª×Ö·û´®ÐÎÊ½£¨Êý×Ö×ÔÈ»¸ñÊ½»¯£©
static std::string numToStr(double val) {
    if (val == std::floor(val) && std::fabs(val) < 2e18) {
        char buf[64];
        sprintf(buf, "%.0f", val);
        return buf;
    }
    char buf[64];
    sprintf(buf, "%g", val);
    return buf;
}

std::string evalExpr(const std::string& expr) {
    std::string s = expr;
    trim(s);
    if (s.empty()) return "0";

    // 引号字符串字面量
    if (isQuoted(s)) return parseStringLiteral(s);

    // 纯变量名（无运算符、无括号、无引号）
    if (isIdentifier(s) && hasVar(s)) return getVar(s);

    // 纯数字
    if (isPureNumber(s)) return s;

    // list[index] / dict[key] 下标访问
    {
        std::string name, idx;
        if (isListIndexExpr(s, name, idx)) {
            // 先求值索引
            std::string idxVal = evalExpr(idx);
            if (g_waitingInput) return "";
            if (isListVar(name)) {
                int i = atoi(idxVal.c_str());
                auto& lst = getList(name);
                if (i < 0 || i >= (int)lst.size()) {
                    reportError(ERR_OUT_OF_RANGE, "list index out of range: " + name + "[" + idxVal + "]");
                    return "";
                }
                return lst[i];
            }
            if (isDictVar(name)) {
                auto& d = getDict(name);
                auto it = d.find(idxVal);
                return (it != d.end()) ? it->second : "";
            }
            // 未定义的列表/字典
            reportError(ERR_UNDEFINED_VAR, "undefined list/dict: " + name);
            return "";
        }
    }

    // 首先替换函数调用（递归地）
    bool replaced = true;
    int safetyCount = 0;
    while (replaced && safetyCount++ < 1000) {
        replaced = false;
        for (auto& func : g_funcs) {
            std::string pattern = func.first + "(";
            size_t pos = s.find(pattern);
            while (pos != std::string::npos) {
                if (pos > 0 && (isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_')) {
                    pos = s.find(pattern, pos + 1);
                    continue;
                }
                size_t lp = pos + pattern.length();
                int depth = 1; size_t rp = lp;
                bool inS = false, inD = false;
                while (rp < s.size() && depth > 0) {
                    char c = s[rp];
                    if (inS) { if (c == '\'') inS = false; }
                    else if (inD) { if (c == '"') inD = false; }
                    else if (c == '\'') inS = true;
                    else if (c == '"') inD = true;
                    else if (c == '(') depth++;
                    else if (c == ')') { depth--; if (depth == 0) break; }
                    rp++;
                }
                if (depth != 0) break;
                std::string argsStr = s.substr(lp, rp - lp);
                std::vector<std::string> cargs; size_t ap = 0;
                while (ap <= argsStr.size()) {
                    std::string a = getArg(argsStr, ap);
                    cargs.push_back(evalExpr(a));
                    if (g_waitingInput) return "";  // Input 中断,立即返回
                    if (ap < argsStr.size() && argsStr[ap] == ',') ap++;
                    else break;
                }
                std::string fr = callFunc(func.first, cargs);
                if (g_waitingInput) return "";  // callFunc 内部可能触发 Input
                s.replace(pos, rp - pos + 1, fr);
                replaced = true;
                break; // ÖØÐÂ¿ªÊ¼Ìæ»»Ñ­»·
            }
            if (replaced) break;
        }
        // 也尝试内置函数 (str*/list*)
        if (!replaced) {
            // 先检测 Input/InputInt (表达式模式: 返回输入值)
            static const char* inputFuncs[] = {"InputInt", "Input", NULL};
            for (int bi = 0; inputFuncs[bi]; bi++) {
                std::string pattern = std::string(inputFuncs[bi]) + "(";
                size_t pos = s.find(pattern);
                while (pos != std::string::npos) {
                    if (pos > 0 && (isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_')) {
                        pos = s.find(pattern, pos + 1);
                        continue;
                    }
                    size_t lp = pos + pattern.length();
                    int depth = 1; size_t rp = lp;
                    bool inS = false, inD = false;
                    while (rp < s.size() && depth > 0) {
                        char c = s[rp];
                        if (inS) { if (c == '\'') inS = false; }
                        else if (inD) { if (c == '"') inD = false; }
                        else if (c == '\'') inS = true;
                        else if (c == '"') inD = true;
                        else if (c == '(') depth++;
                        else if (c == ')') { depth--; if (depth == 0) break; }
                        rp++;
                    }
                    if (depth != 0) break;
                    std::string argsStr = s.substr(lp, rp - lp);
                    // 解析参数(提示), InputInt 优先匹配
                    std::vector<std::string> iargs; size_t iap = 0;
                    while (iap <= argsStr.size()) {
                        std::string a = getArg(argsStr, iap);
                        iargs.push_back(evalExpr(a));
                        if (g_waitingInput) return "";
                        if (iap < argsStr.size() && argsStr[iap] == ',') iap++;
                        else break;
                    }
                    if (!g_inputPending) {
                        // 第一次: 触发输入等待
                        g_waitingInput = true;
                        g_inputType = (inputFuncs[bi] == "InputInt") ? 1 : 0;
                        g_inputPrompt = iargs.size() > 0 ? iargs[0] : "";
                        g_inputVarName = "";  // 表达式模式,不存变量
                        g_inputPending = true;
                        return "";  // 中断 evalExpr
                    } else {
                        // 恢复: 返回输入结果
                        g_inputPending = false;
                        std::string result = g_inputResult;
                        s.replace(pos, rp - pos + 1, result);
                        replaced = true;
                        break;
                    }
                }
                if (replaced) break;
            }
        }
        if (!replaced) {
            // 检查所有内置函数名
            static const char* builtins[] = {
                "strLen","strUpper","strLower","strSub","strCat","strRep","strFind","strRFind",
                "strReplace","strTrim","strSplit","strJoin","strChar","strAscii","strChr",
                "strReverse","strStartsWith","strEndsWith","strContains","strCount",
                "strLeft","strRight","strToInt","strToFloat","numToStr",
                "listNew","listLen","listGet","listSet","listAppend","listInsert","listRemove",
                "listPop","listClear","listSort","listReverse","listFind","listContains",
                "listCopy","listSum","listJoin","listPrint",
                "abs","sqrt","pow","max","min","floor","ceil","round","random","mod","sin","cos","log",
                "CboxS","getCbox","isCbox","showAllCboxS",
                "dictNew","dictSet","dictGet","dictHas","dictRemove","dictLen",
                "dictClear","dictKeys","dictValues","dictCopy","dictPrint","dictMerge", NULL
            };
            for (int bi = 0; builtins[bi]; bi++) {
                std::string pattern = std::string(builtins[bi]) + "(";
                size_t pos = s.find(pattern);
                while (pos != std::string::npos) {
                    if (pos > 0 && (isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_')) {
                        pos = s.find(pattern, pos + 1);
                        continue;
                    }
                    size_t lp = pos + pattern.length();
                    int depth = 1; size_t rp = lp;
                    bool inS = false, inD = false;
                    while (rp < s.size() && depth > 0) {
                        char c = s[rp];
                        if (inS) { if (c == '\'') inS = false; }
                        else if (inD) { if (c == '"') inD = false; }
                        else if (c == '\'') inS = true;
                        else if (c == '"') inD = true;
                        else if (c == '(') depth++;
                        else if (c == ')') { depth--; if (depth == 0) break; }
                        rp++;
                    }
                    if (depth != 0) break;
                    std::string argsStr = s.substr(lp, rp - lp);
                    std::vector<std::string> cargs; size_t ap = 0;
                    // getCbox/isCbox 参数不求值(需要常量名)
                    bool noEval = (strcmp(builtins[bi], "getCbox") == 0 || strcmp(builtins[bi], "isCbox") == 0);
                    while (ap <= argsStr.size()) {
                        std::string a = getArg(argsStr, ap);
                        cargs.push_back(noEval ? a : evalExpr(a));
                        if (g_waitingInput) return "";  // Input 中断
                        if (ap < argsStr.size() && argsStr[ap] == ',') ap++;
                        else break;
                    }
                    std::string fr;
                    builtinCall(builtins[bi], cargs, fr);
                    s.replace(pos, rp - pos + 1, fr);
                    replaced = true;
                    break;
                }
                if (replaced) break;
            }
        }
        // 尝试 C++ 原生函数 (动态注册的)
        if (!replaced) {
            for (auto& e : g_nativeFuncs) {
                if (!e.func) continue;
                std::string pattern = e.name + "(";
                size_t pos = s.find(pattern);
                while (pos != std::string::npos) {
                    if (pos > 0 && (isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_')) { pos = s.find(pattern, pos+1); continue; }
                    size_t lp = pos + pattern.length(); int depth = 1; size_t rp = lp; bool inS=false,inD=false;
                    while (rp < s.size() && depth > 0) { char c = s[rp];
                        if (inS) { if (c=='\'') inS=false; } else if (inD) { if (c=='"') inD=false; }
                        else if (c=='\'') inS=true; else if (c=='"') inD=true;
                        else if (c=='(') depth++; else if (c==')') { depth--; if (depth==0) break; }
                        rp++;
                    }
                    if (depth != 0) break;
                    std::string argsStr = s.substr(lp, rp-lp); std::vector<std::string> cargs; size_t ap = 0;
                    while (ap <= argsStr.size()) { std::string a = getArg(argsStr, ap); cargs.push_back(evalExpr(a));
                        if (g_waitingInput) return ""; if (ap < argsStr.size() && argsStr[ap]==',') ap++; else break; }
                    std::string fr; builtinCall(e.name, cargs, fr);
                    s.replace(pos, rp-pos+1, fr); replaced = true; break;
                }
                if (replaced) break;
            }
        }
    }

    // 替换后如果变成纯字符串或数字，直接返回
    trim(s);
    if (s.empty()) return "";
    if (isQuoted(s)) return parseStringLiteral(s);
    if (isPureNumber(s)) return s;
    // 如果是单个标识符且是已定义变量,返回变量值
    if (isIdentifier(s) && hasVar(s)) return getVar(s);
    // 如果不含运算符(+,-,*,/,=)且不是引号/数字,当作字符串字面量返回(可能是内置函数返回值)
    if (s.find_first_of("+-*/=") == std::string::npos) return s;

    // 处理含下标的算术表达式: 把所有 list[i]/dict[k] 替换为值
    if (s.find('[') != std::string::npos) {
        bool didReplace = true;
        int safety = 0;
        while (didReplace && safety++ < 1000) {
            didReplace = false;
            for (size_t p = 0; p < s.size(); p++) {
                if (s[p] == '[' && p > 0) {
                    size_t nameStart = p;
                    while (nameStart > 0 && (isalnum((unsigned char)s[nameStart-1]) || s[nameStart-1] == '_')) nameStart--;
                    if (nameStart >= p) continue;
                    std::string name = s.substr(nameStart, p - nameStart);
                    if (!isIdentifier(name)) continue;
                    int depth = 1; size_t rb = std::string::npos;
                    for (size_t k = p + 1; k < s.size(); k++) {
                        if (s[k] == '[') depth++;
                        else if (s[k] == ']') { depth--; if (depth == 0) { rb = k; break; } }
                    }
                    if (rb == std::string::npos) continue;
                    std::string idxExpr = s.substr(p + 1, rb - p - 1);
                    trim(idxExpr);
                    std::string idxVal = evalExpr(idxExpr);
                    if (g_waitingInput) return "";
                    std::string val;
                    if (isListVar(name)) {
                        int i = atoi(idxVal.c_str());
                        auto& lst = getList(name);
                        if (i >= 0 && i < (int)lst.size()) val = lst[i];
                    } else if (isDictVar(name)) {
                        auto& d = getDict(name);
                        auto it = d.find(idxVal);
                        if (it != d.end()) val = it->second;
                    } else continue;
                    // 如果值是字符串,用引号包裹避免再次解析
                    if (!isPureNumber(val)) val = "\"" + val + "\"";
                    s.replace(nameStart, rb - nameStart + 1, val);
                    didReplace = true;
                    break;
                }
            }
        }
        trim(s);
        if (s.empty()) return "";
        if (isQuoted(s)) return parseStringLiteral(s);
        if (isPureNumber(s)) return s;
        if (isIdentifier(s) && hasVar(s)) return getVar(s);
    }

    // 如果含 + 运算符，判断是字符串拼接还是算术加法
    bool hasPlus = (s.find('+') != std::string::npos);
    if (hasPlus) {
        size_t pp;
        if (findConcatPlus(s, pp)) {
            std::string l = s.substr(0, pp);
            std::string r = s.substr(pp + 1);
            std::string lt = l, rt = r; trim(lt); trim(rt);
            bool lIsStr = false, rIsStr = false;
            if (isQuoted(lt)) lIsStr = true;
            else if (hasVar(lt) && !isPureNumber(getVar(lt))) lIsStr = true;
            else if (lt.find('"') != std::string::npos || lt.find('\'') != std::string::npos) lIsStr = true;
            if (isQuoted(rt)) rIsStr = true;
            else if (hasVar(rt) && !isPureNumber(getVar(rt))) rIsStr = true;
            else if (rt.find('"') != std::string::npos || rt.find('\'') != std::string::npos) rIsStr = true;
            if (lIsStr || rIsStr) {
                return evalExpr(l) + evalExpr(r);
            }
        }
    }

    // ÊýÖµ¼ÆËã
    double val = calc(s);
    return numToStr(val);
}

// ---- resolveFuncArg: ²ÎÊý½âÎö ----
std::string resolveFuncArg(const std::string& raw) {
    std::string s = raw;
    trim(s);
    if (s.empty()) return "";
    if (isQuoted(s)) return parseStringLiteral(s);
    if (isIdentifier(s) && !hasVar(s)) return s; // Î´¶¨ÒåµÄ±êÊ¶·û×÷Îª×Ö·û´®
    return evalExpr(s);
}

// ---- runCode: ÖØÐ´ ----
// ´¦Àíµ¥ÐÐÖ¸Áî£ºreturn¡¢¸³Öµ¡¢¸´ºÏ¸³Öµ¡¢º¯Êýµ÷ÓÃ¡¢ËµÃ÷Óï¾ä

// ÅÐ¶Ï s ÊÇ·ñÒÔÍÆ¼öµÄÄ³ÖÖÖ¸Áî¿ªÍ·
static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// ÅÐ¶ÏÊÇ·ñÎª¼òµ¥¸³Öµ£¨·Ç¸´ºÏ¸³Öµ¡¢·Ç±È½ÏÔËËã·û£©
// ·µ»Ø -1 ±íÊ¾·ñ£»·ñÔò·µ»Ø = µÄÎ»ÖÃ
static size_t findAssignEq(const std::string& s) {
    bool inS = false, inD = false;
    int depth = 0;       // () 深度
    int bracketDepth = 0; // [] 深度
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (inS) { if (c == '\'') inS = false; continue; }
        if (inD) { if (c == '"') inD = false; continue; }
        if (c == '\'') { inS = true; continue; }
        if (c == '"') { inD = true; continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        else if (c == '[') bracketDepth++;
        else if (c == ']') { if (bracketDepth > 0) bracketDepth--; }
        else if (depth == 0 && bracketDepth == 0 && c == '=') {
            // 排除 == != >= <= += -= *= /= 及 <=
            if (i + 1 < s.size() && s[i+1] == '=') { i++; continue; }
            if (i > 0 && (s[i-1] == '=' || s[i-1] == '!' || s[i-1] == '<' || s[i-1] == '>' ||
                          s[i-1] == '+' || s[i-1] == '-' || s[i-1] == '*' || s[i-1] == '/')) continue;
            return i;
        }
    }
    return std::string::npos;
}

// ---- 内置函数: 返回字符串值的函数 (用于表达式求值) ----
// 返回 true 表示是内置函数并已处理, false 表示不是
// args 是已解析的参数值列表
bool builtinCall(const std::string& name, const std::vector<std::string>& args, std::string& result) {
    // 先检查 C++ 原生函数 (GetCPack 注册的)
    if (callNativeFunc(name, args, result)) return true;

    // ============ 字符串处理函数 (20+) ============

    // strLen(s) - 字符串长度
    if (name == "strLen") {
        if (args.empty()) { reportError(ERR_ARG_COUNT, "strLen needs 1 arg", name); result = "0"; return true; }
        result = std::to_string(args[0].size());
        return true;
    }
    // strUpper(s) - 转大写
    if (name == "strUpper") {
        if (args.empty()) { result = ""; return true; }
        std::string r = args[0];
        for (auto& c : r) c = (char)toupper((unsigned char)c);
        result = r;
        return true;
    }
    // strLower(s) - 转小写
    if (name == "strLower") {
        if (args.empty()) { result = ""; return true; }
        std::string r = args[0];
        for (auto& c : r) c = (char)tolower((unsigned char)c);
        result = r;
        return true;
    }
    // strSub(s, start, len) - 子串
    if (name == "strSub") {
        if (args.size() < 2) { reportError(ERR_ARG_COUNT, "strSub needs 2-3 args", name); result = ""; return true; }
        int start = (int)atoi(args[1].c_str());
        int len = args.size() >= 3 ? (int)atoi(args[2].c_str()) : (int)args[0].size();
        if (start < 0) start = 0;
        if (start > (int)args[0].size()) start = (int)args[0].size();
        if (len < 0) len = 0;
        if (start + len > (int)args[0].size()) len = (int)args[0].size() - start;
        result = args[0].substr(start, len);
        return true;
    }
    // strCat(s1, s2, ...) - 字符串连接
    if (name == "strCat") {
        std::string r;
        for (auto& a : args) r += a;
        result = r;
        return true;
    }
    // strRep(s, n) - 重复n次
    if (name == "strRep") {
        if (args.size() < 2) { result = ""; return true; }
        int n = atoi(args[1].c_str());
        if (n < 0) n = 0;
        std::string r;
        for (int i = 0; i < n; i++) r += args[0];
        result = r;
        return true;
    }
    // strFind(s, sub) - 查找子串,返回位置(0-based),-1未找到
    if (name == "strFind") {
        if (args.size() < 2) { result = "-1"; return true; }
        size_t p = args[0].find(args[1]);
        result = (p == std::string::npos) ? "-1" : std::to_string((int)p);
        return true;
    }
    // strRFind(s, sub) - 从后查找
    if (name == "strRFind") {
        if (args.size() < 2) { result = "-1"; return true; }
        size_t p = args[0].rfind(args[1]);
        result = (p == std::string::npos) ? "-1" : std::to_string((int)p);
        return true;
    }
    // strReplace(s, old, new) - 替换所有
    if (name == "strReplace") {
        if (args.size() < 3) { result = args.empty() ? "" : args[0]; return true; }
        std::string r = args[0];
        const std::string& from = args[1];
        const std::string& to = args[2];
        if (from.empty()) { result = r; return true; }
        size_t pos = 0;
        while ((pos = r.find(from, pos)) != std::string::npos) {
            r.replace(pos, from.size(), to);
            pos += to.size();
        }
        result = r;
        return true;
    }
    // strTrim(s) - 去首尾空白
    if (name == "strTrim") {
        if (args.empty()) { result = ""; return true; }
        std::string r = args[0];
        trim(r);
        result = r;
        return true;
    }
    // strSplit(s, delim) - 分割字符串为列表
    if (name == "strSplit") {
        if (args.size() < 2) { result = ""; return true; }
        // 结果存入临时列表,返回列表名
        // 但这里返回字符串值,所以返回元素个数,列表存到 g_lists["__split__"]
        std::vector<std::string> parts;
        std::string s = args[0], delim = args[1];
        if (delim.empty()) {
            for (auto& c : s) parts.push_back(std::string(1, c));
        } else {
            size_t start = 0, pos;
            while ((pos = s.find(delim, start)) != std::string::npos) {
                parts.push_back(s.substr(start, pos - start));
                start = pos + delim.size();
            }
            parts.push_back(s.substr(start));
        }
        static int splitCounter = 0;
        splitCounter++;
        std::string listName = "__split_" + std::to_string(splitCounter) + "__";
        setList(listName, parts);
        result = listName;
        return true;
    }
    // strJoin(listName, delim) - 列表连接为字符串
    if (name == "strJoin") {
        if (args.size() < 2) { result = ""; return true; }
        std::string r;
        if (isListVar(args[0])) {
            auto& lst = getList(args[0]);
            for (size_t i = 0; i < lst.size(); i++) {
                if (i > 0) r += args[1];
                r += lst[i];
            }
        }
        result = r;
        return true;
    }
    // strChar(s, i) - 取第i个字符
    if (name == "strChar") {
        if (args.size() < 2) { result = ""; return true; }
        int i = atoi(args[1].c_str());
        if (i < 0 || i >= (int)args[0].size()) { reportError(ERR_OUT_OF_RANGE, "strChar index out of range", args[1]); result = ""; return true; }
        result = std::string(1, args[0][i]);
        return true;
    }
    // strAscii(c) - 字符转ASCII码
    if (name == "strAscii") {
        if (args.empty() || args[0].empty()) { result = "0"; return true; }
        result = std::to_string((int)(unsigned char)args[0][0]);
        return true;
    }
    // strChr(n) - ASCII码转字符
    if (name == "strChr") {
        if (args.empty()) { result = ""; return true; }
        int n = atoi(args[0].c_str());
        if (n < 0 || n > 255) { result = ""; return true; }
        result = std::string(1, (char)n);
        return true;
    }
    // strReverse(s) - 反转字符串
    if (name == "strReverse") {
        if (args.empty()) { result = ""; return true; }
        std::string r = args[0];
        std::reverse(r.begin(), r.end());
        result = r;
        return true;
    }
    // strStartsWith(s, prefix) - 是否以prefix开头
    if (name == "strStartsWith") {
        if (args.size() < 2) { result = "false"; return true; }
        result = startsWith(args[0], args[1]) ? "true" : "false";
        return true;
    }
    // strEndsWith(s, suffix) - 是否以suffix结尾
    if (name == "strEndsWith") {
        if (args.size() < 2) { result = "false"; return true; }
        if (args[1].size() > args[0].size()) { result = "false"; return true; }
        result = (args[0].compare(args[0].size() - args[1].size(), args[1].size(), args[1]) == 0) ? "true" : "false";
        return true;
    }
    // strContains(s, sub) - 是否包含子串
    if (name == "strContains") {
        if (args.size() < 2) { result = "false"; return true; }
        result = (args[0].find(args[1]) != std::string::npos) ? "true" : "false";
        return true;
    }
    // strCount(s, sub) - 子串出现次数
    if (name == "strCount") {
        if (args.size() < 2 || args[1].empty()) { result = "0"; return true; }
        int cnt = 0;
        size_t pos = 0;
        while ((pos = args[0].find(args[1], pos)) != std::string::npos) { cnt++; pos += args[1].size(); }
        result = std::to_string(cnt);
        return true;
    }
    // strLeft(s, n) - 取左边n个字符
    if (name == "strLeft") {
        if (args.size() < 2) { result = args.empty() ? "" : args[0]; return true; }
        int n = atoi(args[1].c_str());
        if (n < 0) n = 0;
        if (n > (int)args[0].size()) n = (int)args[0].size();
        result = args[0].substr(0, n);
        return true;
    }
    // strRight(s, n) - 取右边n个字符
    if (name == "strRight") {
        if (args.size() < 2) { result = args.empty() ? "" : args[0]; return true; }
        int n = atoi(args[1].c_str());
        if (n < 0) n = 0;
        if (n > (int)args[0].size()) n = (int)args[0].size();
        result = args[0].substr(args[0].size() - n);
        return true;
    }
    // strToInt(s) - 字符串转整数
    if (name == "strToInt") {
        if (args.empty()) { result = "0"; return true; }
        result = std::to_string(atoi(args[0].c_str()));
        return true;
    }
    // strToFloat(s) - 字符串转浮点
    if (name == "strToFloat") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%g", atof(args[0].c_str()));
        result = buf;
        return true;
    }
    // numToStr(n) - 数字转字符串 (已在别处实现,这里兼容)
    if (name == "numToStr") {
        if (args.empty()) { result = "0"; return true; }
        result = args[0];
        return true;
    }

    // ============ 列表处理函数 (10+) ============

    // listNew(name, e1, e2, ...) - 创建列表
    if (name == "listNew") {
        if (args.empty()) { reportError(ERR_ARG_COUNT, "listNew needs list name", name); result = ""; return true; }
        std::string lname = args[0];
        if (!isIdentifier(lname)) { reportError(ERR_INVALID_NAME, "invalid list name", lname); result = ""; return true; }
        std::vector<std::string> elems;
        for (size_t i = 1; i < args.size(); i++) elems.push_back(args[i]);
        setList(lname, elems);
        result = lname;
        return true;
    }
    // listLen(name) - 列表长度
    if (name == "listLen") {
        if (args.empty()) { result = "0"; return true; }
        result = std::to_string(getList(args[0]).size());
        return true;
    }
    // listGet(name, i) - 取第i个元素
    if (name == "listGet") {
        if (args.size() < 2) { result = ""; return true; }
        int i = atoi(args[1].c_str());
        if (!isListVar(args[0])) { reportError(ERR_INVALID_LIST, "undefined list", args[0]); result = ""; return true; }
        auto& lst = getList(args[0]);
        if (i < 0 || i >= (int)lst.size()) { reportError(ERR_OUT_OF_RANGE, "list index out of range", args[1]); result = ""; return true; }
        result = lst[i];
        return true;
    }
    // listSet(name, i, val) - 设置第i个元素
    if (name == "listSet") {
        if (args.size() < 3) { result = ""; return true; }
        int i = atoi(args[1].c_str());
        if (!isListVar(args[0])) { reportError(ERR_INVALID_LIST, "undefined list", args[0]); result = ""; return true; }
        auto& lst = getList(args[0]);
        if (i < 0 || i >= (int)lst.size()) { reportError(ERR_OUT_OF_RANGE, "list index out of range", args[1]); result = ""; return true; }
        lst[i] = args[2];
        result = args[2];
        return true;
    }
    // listAppend(name, val) - 追加元素
    if (name == "listAppend") {
        if (args.size() < 2) { result = ""; return true; }
        if (!isListVar(args[0])) setList(args[0], std::vector<std::string>());
        getList(args[0]).push_back(args[1]);
        result = args[1];
        return true;
    }
    // listInsert(name, i, val) - 在位置i插入
    if (name == "listInsert") {
        if (args.size() < 3) { result = ""; return true; }
        int i = atoi(args[1].c_str());
        if (!isListVar(args[0])) setList(args[0], std::vector<std::string>());
        auto& lst = getList(args[0]);
        if (i < 0) i = 0;
        if (i > (int)lst.size()) i = (int)lst.size();
        lst.insert(lst.begin() + i, args[2]);
        result = args[2];
        return true;
    }
    // listRemove(name, i) - 删除位置i的元素
    if (name == "listRemove") {
        if (args.size() < 2) { result = ""; return true; }
        int i = atoi(args[1].c_str());
        if (!isListVar(args[0])) { result = ""; return true; }
        auto& lst = getList(args[0]);
        if (i < 0 || i >= (int)lst.size()) { reportError(ERR_OUT_OF_RANGE, "list index out of range", args[1]); result = ""; return true; }
        lst.erase(lst.begin() + i);
        result = "true";
        return true;
    }
    // listPop(name) - 弹出最后一个元素
    if (name == "listPop") {
        if (args.empty()) { result = ""; return true; }
        if (!isListVar(args[0])) { result = ""; return true; }
        auto& lst = getList(args[0]);
        if (lst.empty()) { result = ""; return true; }
        result = lst.back();
        lst.pop_back();
        return true;
    }
    // listClear(name) - 清空列表
    if (name == "listClear") {
        if (args.empty()) { result = ""; return true; }
        setList(args[0], std::vector<std::string>());
        result = "true";
        return true;
    }
    // listSort(name) - 排序(原地)
    if (name == "listSort") {
        if (args.empty()) { result = ""; return true; }
        if (!isListVar(args[0])) { result = ""; return true; }
        auto& lst = getList(args[0]);
        std::sort(lst.begin(), lst.end());
        result = "true";
        return true;
    }
    // listReverse(name) - 反转列表(原地)
    if (name == "listReverse") {
        if (args.empty()) { result = ""; return true; }
        if (!isListVar(args[0])) { result = ""; return true; }
        auto& lst = getList(args[0]);
        std::reverse(lst.begin(), lst.end());
        result = "true";
        return true;
    }
    // listFind(name, val) - 查找元素位置,-1未找到
    if (name == "listFind") {
        if (args.size() < 2) { result = "-1"; return true; }
        if (!isListVar(args[0])) { result = "-1"; return true; }
        auto& lst = getList(args[0]);
        for (size_t i = 0; i < lst.size(); i++) {
            if (lst[i] == args[1]) { result = std::to_string((int)i); return true; }
        }
        result = "-1";
        return true;
    }
    // listContains(name, val) - 是否包含
    if (name == "listContains") {
        if (args.size() < 2) { result = "false"; return true; }
        auto& lst = getList(args[0]);
        for (auto& v : lst) if (v == args[1]) { result = "true"; return true; }
        result = "false";
        return true;
    }
    // listCopy(srcName, dstName) - 复制列表
    if (name == "listCopy") {
        if (args.size() < 2) { result = ""; return true; }
        setList(args[1], getList(args[0]));
        result = args[1];
        return true;
    }
    // listSum(name) - 求和(数字列表)
    if (name == "listSum") {
        if (args.empty()) { result = "0"; return true; }
        double sum = 0;
        auto& lst = getList(args[0]);
        for (auto& v : lst) sum += atof(v.c_str());
        char buf[64]; sprintf(buf, "%g", sum);
        result = buf;
        return true;
    }
    // listJoin(name, delim) - 连接为字符串
    if (name == "listJoin") {
        if (args.size() < 2) { result = ""; return true; }
        std::string r;
        auto& lst = getList(args[0]);
        for (size_t i = 0; i < lst.size(); i++) {
            if (i > 0) r += args[1];
            r += lst[i];
        }
        result = r;
        return true;
    }
    // listPrint(name) - 打印列表
    if (name == "listPrint") {
        if (args.empty()) { result = ""; return true; }
        auto& lst = getList(args[0]);
        std::string r = "[";
        for (size_t i = 0; i < lst.size(); i++) {
            if (i > 0) r += ", ";
            r += lst[i];
        }
        r += "]";
        Log(r);
        result = r;
        return true;
    }

    // ============ 数学函数 (10+) ============

    // abs(n) - 绝对值
    if (name == "abs") {
        if (args.empty()) { result = "0"; return true; }
        double v = atof(args[0].c_str());
        char buf[64]; sprintf(buf, "%g", fabs(v));
        result = buf; return true;
    }
    // sqrt(n) - 平方根
    if (name == "sqrt") {
        if (args.empty()) { result = "0"; return true; }
        double v = atof(args[0].c_str());
        if (v < 0) { reportError(ERR_INVALID_NUM, "sqrt of negative number"); result = "0"; return true; }
        char buf[64]; sprintf(buf, "%g", sqrt(v));
        result = buf; return true;
    }
    // pow(base, exp) - 幂运算
    if (name == "pow") {
        if (args.size() < 2) { result = "0"; return true; }
        double b = atof(args[0].c_str()), e = atof(args[1].c_str());
        char buf[64]; sprintf(buf, "%g", pow(b, e));
        result = buf; return true;
    }
    // max(a, b) - 最大值
    if (name == "max") {
        if (args.size() < 2) { result = args.empty() ? "0" : args[0]; return true; }
        double a = atof(args[0].c_str()), b = atof(args[1].c_str());
        char buf[64]; sprintf(buf, "%g", a > b ? a : b);
        result = buf; return true;
    }
    // min(a, b) - 最小值
    if (name == "min") {
        if (args.size() < 2) { result = args.empty() ? "0" : args[0]; return true; }
        double a = atof(args[0].c_str()), b = atof(args[1].c_str());
        char buf[64]; sprintf(buf, "%g", a < b ? a : b);
        result = buf; return true;
    }
    // floor(n) - 向下取整
    if (name == "floor") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%.0f", floor(atof(args[0].c_str())));
        result = buf; return true;
    }
    // ceil(n) - 向上取整
    if (name == "ceil") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%.0f", ceil(atof(args[0].c_str())));
        result = buf; return true;
    }
    // round(n) - 四舍五入
    if (name == "round") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%.0f", floor(atof(args[0].c_str()) + 0.5));
        result = buf; return true;
    }
    // random(min, max) - 随机整数 [min, max]
    if (name == "random") {
        static bool seeded = false;
        if (!seeded) { srand((unsigned)time(NULL)); seeded = true; }
        if (args.size() < 2) { result = "0"; return true; }
        int lo = atoi(args[0].c_str()), hi = atoi(args[1].c_str());
        if (hi < lo) { int t = lo; lo = hi; hi = t; }
        char buf[64]; sprintf(buf, "%d", lo + rand() % (hi - lo + 1));
        result = buf; return true;
    }
    // mod(a, b) - 取模
    if (name == "mod") {
        if (args.size() < 2) { result = "0"; return true; }
        int a = atoi(args[0].c_str()), b = atoi(args[1].c_str());
        if (b == 0) { reportError(ERR_DIV_ZERO, "mod by zero"); result = "0"; return true; }
        char buf[64]; sprintf(buf, "%d", a % b);
        result = buf; return true;
    }
    // sin(n) - 正弦 (弧度)
    if (name == "sin") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%g", sin(atof(args[0].c_str())));
        result = buf; return true;
    }
    // cos(n) - 余弦 (弧度)
    if (name == "cos") {
        if (args.empty()) { result = "0"; return true; }
        char buf[64]; sprintf(buf, "%g", cos(atof(args[0].c_str())));
        result = buf; return true;
    }
    // log(n) - 自然对数
    if (name == "log") {
        if (args.empty()) { result = "0"; return true; }
        double v = atof(args[0].c_str());
        if (v <= 0) { reportError(ERR_INVALID_NUM, "log of non-positive number"); result = "0"; return true; }
        char buf[64]; sprintf(buf, "%g", log(v));
        result = buf; return true;
    }

    // ============ 常量函数 ============

    // CboxS(name, value) - 定义常量 (语句调用,返回常量名)
    if (name == "CboxS") {
        if (args.size() < 2) { reportError(ERR_ARG_COUNT, "CboxS needs 2 args", name); result = ""; return true; }
        std::string cname = args[0];
        if (!isIdentifier(cname)) { reportError(ERR_INVALID_NAME, "invalid constant name", cname); result = ""; return true; }
        if (isConstVar(cname)) { reportError(ERR_INVALID_ARG, "constant already defined: " + cname); result = ""; return true; }
        setConst(cname, args[1]);
        result = cname;
        return true;
    }
    // getCbox(name) - 读取常量值
    if (name == "getCbox") {
        if (args.empty()) { result = ""; return true; }
        if (!isConstVar(args[0])) { reportError(ERR_UNDEFINED_VAR, "undefined constant", args[0]); result = ""; return true; }
        result = getConst(args[0]);
        return true;
    }
    // isCbox(name) - 是否是常量
    if (name == "isCbox") {
        if (args.empty()) { result = "false"; return true; }
        result = isConstVar(args[0]) ? "true" : "false";
        return true;
    }
    // showAllCboxS() - 显示所有常量
    if (name == "showAllCboxS") {
        for (auto& c : g_consts) Log("  " + c.first + " = " + c.second);
        result = std::to_string(g_consts.size());
        return true;
    }

    // ============ 字典处理函数 (10+) ============

    // dictNew(name) - 创建空字典
    if (name == "dictNew") {
        if (args.empty()) { reportError(ERR_ARG_COUNT, "dictNew needs name", name); result = ""; return true; }
        std::string dn = args[0];
        if (!isIdentifier(dn)) { reportError(ERR_INVALID_NAME, "invalid dict name", dn); result = ""; return true; }
        setDict(dn, std::map<std::string, std::string>());
        result = dn;
        return true;
    }
    // dictSet(name, key, value) - 设置键值对
    if (name == "dictSet") {
        if (args.size() < 3) { reportError(ERR_ARG_COUNT, "dictSet needs 3 args", name); result = ""; return true; }
        if (!isDictVar(args[0])) setDict(args[0], std::map<std::string, std::string>());
        getDict(args[0])[args[1]] = args[2];
        result = args[2];
        return true;
    }
    // dictGet(name, key) - 取键对应的值
    if (name == "dictGet") {
        if (args.size() < 2) { result = ""; return true; }
        if (!isDictVar(args[0])) { reportError(ERR_INVALID_TYPE, "undefined dict", args[0]); result = ""; return true; }
        auto& d = getDict(args[0]);
        auto it = d.find(args[1]);
        result = (it != d.end()) ? it->second : "";
        return true;
    }
    // dictHas(name, key) - 是否包含键
    if (name == "dictHas") {
        if (args.size() < 2) { result = "false"; return true; }
        if (!isDictVar(args[0])) { result = "false"; return true; }
        auto& d = getDict(args[0]);
        result = (d.find(args[1]) != d.end()) ? "true" : "false";
        return true;
    }
    // dictRemove(name, key) - 删除键
    if (name == "dictRemove") {
        if (args.size() < 2) { result = ""; return true; }
        if (!isDictVar(args[0])) { result = ""; return true; }
        getDict(args[0]).erase(args[1]);
        result = "true";
        return true;
    }
    // dictLen(name) - 键值对数量
    if (name == "dictLen") {
        if (args.empty()) { result = "0"; return true; }
        result = std::to_string(getDict(args[0]).size());
        return true;
    }
    // dictClear(name) - 清空字典
    if (name == "dictClear") {
        if (args.empty()) { result = ""; return true; }
        setDict(args[0], std::map<std::string, std::string>());
        result = "true";
        return true;
    }
    // dictKeys(name) - 获取所有键,返回列表名
    if (name == "dictKeys") {
        if (args.empty()) { result = ""; return true; }
        std::vector<std::string> keys;
        auto& d = getDict(args[0]);
        for (auto& kv : d) keys.push_back(kv.first);
        static int kc = 0; kc++;
        std::string ln = "__dictkeys_" + std::to_string(kc) + "__";
        setList(ln, keys);
        result = ln;
        return true;
    }
    // dictValues(name) - 获取所有值,返回列表名
    if (name == "dictValues") {
        if (args.empty()) { result = ""; return true; }
        std::vector<std::string> vals;
        auto& d = getDict(args[0]);
        for (auto& kv : d) vals.push_back(kv.second);
        static int vc = 0; vc++;
        std::string ln = "__dictvals_" + std::to_string(vc) + "__";
        setList(ln, vals);
        result = ln;
        return true;
    }
    // dictCopy(src, dst) - 复制字典
    if (name == "dictCopy") {
        if (args.size() < 2) { result = ""; return true; }
        setDict(args[1], getDict(args[0]));
        result = args[1];
        return true;
    }
    // dictPrint(name) - 打印字典内容
    if (name == "dictPrint") {
        if (args.empty()) { result = ""; return true; }
        auto& d = getDict(args[0]);
        std::string r = "{";
        bool first = true;
        for (auto& kv : d) {
            if (!first) r += ", ";
            r += kv.first + ": " + kv.second;
            first = false;
        }
        r += "}";
        Log(r);
        result = r;
        return true;
    }
    // dictMerge(dst, src) - 合并src到dst
    if (name == "dictMerge") {
        if (args.size() < 2) { result = ""; return true; }
        if (!isDictVar(args[0])) setDict(args[0], std::map<std::string, std::string>());
        auto& src = getDict(args[1]);
        auto& dst = getDict(args[0]);
        for (auto& kv : src) dst[kv.first] = kv.second;
        result = "true";
        return true;
    }

    return false;
}

void runCode(const std::string& input) {
    std::string s = input;
    trim(s);
    if (s.empty() || s[0] == '#') return;

    // return Óï¾ä
    if (startsWith(s, "return ")) {
        std::string val = s.substr(7);
        trim(val);
        setVar("__ret__", resolveFuncArg(val));
        g_returning = true;
        return;
    }
    if (s == "return") {
        setVar("__ret__", "");
        g_returning = true;
        return;
    }

    // ¼ì²â¸³Öµ£¨ÔÚº¯Êýµ÷ÓÃÖ®Ç°£©
    size_t eqPos = findAssignEq(s);
    size_t parenPos = s.find('(');

    if (eqPos != std::string::npos && eqPos > 0 &&
        (parenPos == std::string::npos || eqPos < parenPos)) {
        std::string vn = s.substr(0, eqPos);
        std::string vl = s.substr(eqPos + 1);
        trim(vn); trim(vl);
        if (vn.empty() || vl.empty()) return;
        // 检查是否是 list[index]=val 或 dict[key]=val 赋值
        {
            std::string lname, idx;
            if (isListIndexExpr(vn, lname, idx)) {
                std::string idxVal = evalExpr(idx);
                if (g_waitingInput) return;
                std::string val = resolveFuncArg(vl);
                if (g_waitingInput) return;
                if (isListVar(lname)) {
                    int i = atoi(idxVal.c_str());
                    auto& lst = getList(lname);
                    if (i < 0 || i >= (int)lst.size()) {
                        reportError(ERR_OUT_OF_RANGE, "list index out of range: " + lname + "[" + idxVal + "]");
                        return;
                    }
                    lst[i] = val;
                } else if (isDictVar(lname)) {
                    getDict(lname)[idxVal] = val;
                } else {
                    reportError(ERR_UNDEFINED_VAR, "undefined list/dict: " + lname);
                }
                return;
            }
        }
        if (!isIdentifier(vn)) {
            reportError(ERR_INVALID_NAME, "invalid variable name: " + vn);
            return;
        }
        {
            std::string val = resolveFuncArg(vl);
            if (g_waitingInput) return;  // Input 中断,等待恢复
            setVar(vn, val);
        }
        return;
    }

    // º¯Êýµ÷ÓÃÓëËµÃ÷Óï¾ä£¨ÒÔ ( ½áÎ²£©
    if (parenPos != std::string::npos && s.back() == ')') {
        std::string fn = s.substr(0, parenPos);
        trim(fn);
        std::vector<std::string> rawArgs;
        size_t pos = parenPos + 1;
        while (pos < s.size() && s[pos] != ')') {
            std::string a = getArg(s, pos);
            rawArgs.push_back(a);
            if (pos < s.size() && s[pos] == ',') pos++;
            else if (pos < s.size() && s[pos] == ')') break;
            else if (pos >= s.size()) break;
        }

        // ÓÃ»§º¯Êý
        if (g_funcs.find(fn) != g_funcs.end()) {
            std::vector<std::string> args;
            for (size_t i = 0; i < rawArgs.size(); i++) {
                args.push_back(resolveFuncArg(rawArgs[i]));
                if (g_waitingInput) return;  // Input 中断,等待恢复
            }
            callFunc(fn, args);
            return;
        }

        // Input / InputInt (语句模式): 必须在内置函数检测之前
        if (fn == "Input" || fn == "InputInt") {
            g_waitingInput = true;
            g_inputType = (fn == "InputInt") ? 1 : 0;
            std::string prompt, varName;
            if (rawArgs.size() > 0) prompt = resolveFuncArg(rawArgs[0]);
            if (rawArgs.size() > 1) { varName = rawArgs[1]; trim(varName); }
            g_inputPrompt = prompt;
            g_inputVarName = varName;
            if (!prompt.empty()) Log(prompt);
            return;
        }

        // box(x, value) / boxS(prompt, x, value) — 必须在内置函数之前,避免 resolveFuncArg 拦截 Input
        if (fn == "box" && rawArgs.size() >= 2) {
            std::string varName = rawArgs[0];
            std::string val = rawArgs[1];
            trim(val);
            if (startsWith(val, "Input(") && val.back() == ')') {
                g_waitingInput = true;
                g_inputType = 0;
                g_inputVarName = varName;
                std::string inner = val.substr(6, val.size() - 7);
                std::vector<std::string> ia; size_t ip = 0;
                while (ip < inner.size()) {
                    ia.push_back(getArg(inner, ip));
                    if (ip < inner.size() && inner[ip] == ',') ip++;
                }
                g_inputPrompt = ia.size() > 0 ? resolveFuncArg(ia[0]) : "";
                if (!g_inputPrompt.empty()) Log(g_inputPrompt);
                return;
            }
            if (startsWith(val, "InputInt(") && val.back() == ')') {
                g_waitingInput = true;
                g_inputType = 1;
                g_inputVarName = varName;
                std::string inner = val.substr(9, val.size() - 10);
                std::vector<std::string> ia; size_t ip = 0;
                while (ip < inner.size()) {
                    ia.push_back(getArg(inner, ip));
                    if (ip < inner.size() && inner[ip] == ',') ip++;
                }
                g_inputPrompt = ia.size() > 0 ? resolveFuncArg(ia[0]) : "";
                if (!g_inputPrompt.empty()) Log(g_inputPrompt);
                return;
            }
            setVar(varName, evalExpr(val));
            if (g_waitingInput) return;
            return;
        }
        if (fn == "boxS" && rawArgs.size() >= 3) {
            std::string varName = rawArgs[1];
            std::string val = rawArgs[2];
            trim(val);
            if (startsWith(val, "Input(") && val.back() == ')') {
                g_waitingInput = true;
                g_inputType = 0;
                g_inputVarName = varName;
                std::string inner = val.substr(6, val.size() - 7);
                std::vector<std::string> ia; size_t ip = 0;
                while (ip < inner.size()) {
                    ia.push_back(getArg(inner, ip));
                    if (ip < inner.size() && inner[ip] == ',') ip++;
                }
                g_inputPrompt = ia.size() > 0 ? resolveFuncArg(ia[0]) : "";
                if (!g_inputPrompt.empty()) Log(g_inputPrompt);
                return;
            }
            if (startsWith(val, "InputInt(") && val.back() == ')') {
                g_waitingInput = true;
                g_inputType = 1;
                g_inputVarName = varName;
                std::string inner = val.substr(9, val.size() - 10);
                std::vector<std::string> ia; size_t ip = 0;
                while (ip < inner.size()) {
                    ia.push_back(getArg(inner, ip));
                    if (ip < inner.size() && inner[ip] == ',') ip++;
                }
                g_inputPrompt = ia.size() > 0 ? resolveFuncArg(ia[0]) : "";
                if (!g_inputPrompt.empty()) Log(g_inputPrompt);
                return;
            }
            setVar(varName, evalExpr(val));
            if (g_waitingInput) return;
            return;
        }

        // PrintLog 等语句函数 — 必须在内置返回值函数之前,避免参数被拦截
        if (fn == "PrintLog") {
            std::string out;
            for (size_t i = 0; i < rawArgs.size(); i++) {
                if (i > 0) out += " ";
                out += evalExpr(rawArgs[i]);
                if (g_waitingInput) return;  // Input 中断
            }
            Log(out);
            return;
        }

        if (fn == "showAllBoxes") {
            for (auto& v : g_vars) Log("  " + v.first + " = " + v.second);
            return;
        }
        if (fn == "clearAllBoxes") {
            g_vars.clear();
            return;
        }
        if (fn == "showFuncs") {
            for (auto& f : g_funcs) Log("Func: " + f.first);
            return;
        }

        // 内置返回值函数 (字符串/列表函数)
        {
            std::vector<std::string> args;
            for (size_t i = 0; i < rawArgs.size(); i++) {
                args.push_back(resolveFuncArg(rawArgs[i]));
                if (g_waitingInput) return;  // Input 中断
            }
            std::string result;
            if (builtinCall(fn, args, result)) {
                return;
            }
        }

        reportError(ERR_UNDEFINED_FUNC, "unknown function: " + fn);
        return;
    }

    // ¸´ºÏ¸³Öµ += -= *= /=
    std::string ops[] = {"+=","-=","*=","/="};
    for (int j = 0; j < 4; j++) {
        size_t op = s.find(ops[j]);
        if (op != std::string::npos && op > 0) {
            std::string vn = s.substr(0, op);
            std::string rv = s.substr(op + 2);
            trim(vn); trim(rv);
            if (vn.empty() || rv.empty()) return;
            if (!hasVar(vn)) {
                // Î´¶¨Òå±äÁ¿£¬³õÊ¼»¯Îª 0 ÔÙÔËËã
                setVar(vn, "0");
                Log("[Warn] auto-init undefined variable: " + vn);
            }
            double cur = atof(getVar(vn).c_str());
            double val = calc(rv);
            double res;
            if (j == 0) res = cur + val;
            else if (j == 1) res = cur - val;
            else if (j == 2) res = cur * val;
            else res = (val != 0 ? cur / val : 0);
            setVar(vn, numToStr(res));
            return;
        }
    }

    reportError(ERR_SYNTAX, "unrecognized statement: " + s);
}

// ---- 恢复机制：将 pendingFrames 前插到 resumeStack ----
static void flushPendingFrames() {
    if (g_pendingFrames.empty()) return;
    // pendingFrames 从内到外排列，前插时反序，使最内的帧在最前
    for (int k = (int)g_pendingFrames.size() - 1; k >= 0; k--) {
        g_resumeStack.push_front(g_pendingFrames[k]);
    }
    g_pendingFrames.clear();
}

// ---- 恢复执行：从 resumeStack 依次处理帧 ----
void resumeForLoop(ResumeFrame& frame);
void resumeWhileLoop(ResumeFrame& frame);

void resumeExecution() {
    while (!g_resumeStack.empty() && !g_waitingInput) {
        ResumeFrame frame = g_resumeStack.front();
        g_resumeStack.pop_front();

        if (frame.type == 0) {
            // SEQ: 执行剩余行
            if (frame.nextIndex < frame.lines.size()) {
                std::vector<std::string> remaining(
                    frame.lines.begin() + frame.nextIndex, frame.lines.end());
                runLines(remaining);
            }
        } else if (frame.type == 1) {
            // FOR: 继续 For 循环
            resumeForLoop(frame);
        } else if (frame.type == 2) {
            // WHILE: 继续 while 循环
            resumeWhileLoop(frame);
        }

        // 处理过程中产生的新帧前插
        flushPendingFrames();
    }
}

// 恢复 For 循环
void resumeForLoop(ResumeFrame& frame) {
    int val = frame.forVal;
    int iter = 0;
    while (iter < 100000) {
        bool cont;
        if (frame.isExprStep) {
            int curStep = (int)calc(frame.forExprStepArg);
            cont = (curStep >= 0) ? (val <= frame.forEnd) : (val >= frame.forEnd);
        } else {
            cont = (frame.forStep >= 0) ? (val <= frame.forEnd) : (val >= frame.forEnd);
        }
        if (!cont) break;
        char buf[64]; sprintf(buf, "%d", val);
        setVar(frame.forVar, buf);
        bool savedBreak = g_breakLoop;
        g_breakLoop = false;
        runLines(frame.forBody);
        if (g_returning) { g_breakLoop = savedBreak; return; }
        if (g_waitingInput) {
            // Input 再次中断：保存新的 FOR 帧
            ResumeFrame newFrame;
            newFrame.type = 1;
            newFrame.forVar = frame.forVar;
            newFrame.isExprStep = frame.isExprStep;
            newFrame.forExprStepArg = frame.forExprStepArg;
            newFrame.forEnd = frame.forEnd;
            newFrame.forStep = frame.forStep;
            newFrame.forBody = frame.forBody;
            if (frame.isExprStep) newFrame.forVal = (int)calc(frame.forExprStepArg);
            else newFrame.forVal = val + frame.forStep;
            g_pendingFrames.push_back(newFrame);
            g_breakLoop = savedBreak;
            return;
        }
        if (g_breakLoop) { g_breakLoop = false; break; }
        g_breakLoop = savedBreak;
        if (frame.isExprStep) val = (int)calc(frame.forExprStepArg);
        else val += frame.forStep;
        iter++;
    }
}

// 恢复 while 循环
void resumeWhileLoop(ResumeFrame& frame) {
    int iter = 0;
    while (iter < 100000) {
        if (!cond(frame.whileCond)) break;
        bool savedBreak = g_breakLoop;
        g_breakLoop = false;
        runLines(frame.whileBody);
        if (g_returning) { g_breakLoop = savedBreak; return; }
        if (g_waitingInput) {
            // Input 再次中断：保存新的 WHILE 帧
            ResumeFrame newFrame;
            newFrame.type = 2;
            newFrame.whileCond = frame.whileCond;
            newFrame.whileBody = frame.whileBody;
            g_pendingFrames.push_back(newFrame);
            g_breakLoop = savedBreak;
            return;
        }
        if (g_breakLoop) { g_breakLoop = false; break; }
        g_breakLoop = savedBreak;
        iter++;
    }
}

// ---- runLines: ÖØÐ´£¬ÕýÈ·´¦Àí¶ÌÂ·Ìø³öÓë return ----
void runLines(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); i++) {
        // Input 中断：保存剩余行为 SEQ 帧，然后返回
        if (g_waitingInput) {
            ResumeFrame frame;
            frame.type = 0;  // SEQ
            frame.lines = lines;
            // 表达式模式(g_inputPending)需重新执行触发Input的行(i-1),语句模式从当前行继续
            frame.nextIndex = g_inputPending ? (i > 0 ? i - 1 : 0) : i;
            g_pendingFrames.push_back(frame);
            return;
        }
        if (g_breakLoop) break;

        std::string s = lines[i];
        trim(s);
        // 记录当前行号 (1-based)
        g_currentLineNo = (int)i + 1;
        if (s.empty() || s[0] == '#') continue;

        // GetPack("库名") - 导入 SAL 库
        if (startsWith(s, "GetPack(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
                std::string arg = s.substr(p1 + 1, p2 - p1 - 1);
                trim(arg);
                std::string packName = resolveFuncArg(arg);
                if (!packName.empty()) {
                    loadPack(packName);
                }
            }
            continue;
        }

        // GetCPack("库名") - 导入 C++ DLL 库
        if (startsWith(s, "GetCPack(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
                std::string arg = s.substr(p1 + 1, p2 - p1 - 1);
                trim(arg);
                std::string packName = resolveFuncArg(arg);
                if (!packName.empty()) {
                    loadCPack(packName);
                }
            }
            continue;
        }

        // try ... IfErrorToDo ... endTry 错误处理
        // 语法:
        //   try
        //     <可能出错的代码>
        //   IfErrorToDo
        //     <出错时执行的代码>
        //   endTry
        if (s == "try") {
            std::vector<std::string> tryBody, errBody;
            bool inError = false;
            int depth = 0;
            for (size_t j = i + 1; j < lines.size(); j++) {
                std::string l = lines[j]; trim(l);
                if (l == "endTry" && depth == 0) { i = j; break; }
                if (startsWith(l, "Func(") || startsWith(l, "if(") ||
                    startsWith(l, "For(") || startsWith(l, "while(") || startsWith(l, "try")) depth++;
                if (l == "EndFunc" || l == "endif" || l == "endfor" || l == "endwhile" || l == "endTry") depth--;
                if (l == "IfErrorToDo" && depth == 0) { inError = true; continue; }
                if (inError) errBody.push_back(lines[j]);
                else tryBody.push_back(lines[j]);
                if (j == lines.size() - 1) i = j;
            }
            // 记录当前错误数量
            size_t errBefore = g_errors.size();
            // 执行 try 体 (设置标志,出错时中断)
            bool savedInTry = g_inTryBlock;
            g_inTryBlock = true;
            g_breakLoop = false;
            runLines(tryBody);
            g_inTryBlock = savedInTry;
            g_breakLoop = false;
            // 检查是否产生新错误
            if (g_errors.size() > errBefore) {
                // 有错误,执行错误处理体
                runLines(errBody);
            }
            if (g_returning) return;
            continue;
        }

        // Func 定义
        if (startsWith(s, "Func(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            std::string as = s.substr(p1 + 1, p2 - p1 - 1);
            std::vector<std::string> fa; size_t ap = 0;
            while (ap <= as.size()) {
                fa.push_back(getArg(as, ap));
                if (ap < as.size() && as[ap] == ',') ap++;
                else break;
            }
            if (fa.empty()) continue;
            Function func; func.name = fa[0];
            for (size_t k = 1; k < fa.size(); k++) {
                std::string pn = fa[k]; trim(pn);
                if (!pn.empty()) func.params.push_back(pn);
            }
            int depth = 0;
            for (size_t j = i + 1; j < lines.size(); j++) {
                std::string l = lines[j]; trim(l);
                if (l == "EndFunc" && depth == 0) { i = j; break; }
                if (startsWith(l, "Func(") || startsWith(l, "if(") ||
                    startsWith(l, "For(") || startsWith(l, "while(")) depth++;
                if (l == "EndFunc" || l == "endif" || l == "endfor" || l == "endwhile") depth--;
                func.body.push_back(lines[j]);
                if (j == lines.size() - 1) i = j;
            }
            g_funcs[func.name] = func;
            Log("[Func] " + func.name + " (" + std::to_string(func.params.size()) + " params)");
            continue;
        }

        // if / elseIf / else / endif
        if (startsWith(s, "if(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            bool ok = cond(s.substr(p1 + 1, p2 - p1 - 1));
            bool matched = ok;       // 是否已有分支匹配
            bool collecting = ok;    // 当前是否在收集代码
            std::vector<std::string> ib;
            int depth = 0;
            for (size_t j = i + 1; j < lines.size(); j++) {
                std::string l = lines[j]; trim(l);
                if (l == "endif" && depth == 0) { i = j; break; }
                if (startsWith(l, "if(") || startsWith(l, "For(") ||
                    startsWith(l, "while(") || startsWith(l, "Func(")) depth++;
                if (l == "endif" || l == "endfor" || l == "endwhile" || l == "EndFunc") depth--;
                if (l == "else" && depth == 0) { collecting = !matched; continue; }
                if (startsWith(l, "elseIf(") && depth == 0) {
                    if (!matched) {
                        size_t a = l.find('('), b = l.find_last_of(')');
                        if (a != std::string::npos && b != std::string::npos) {
                            ok = cond(l.substr(a + 1, b - a - 1));
                            if (ok) { matched = true; collecting = true; ib.clear(); }
                            else { collecting = false; }
                        }
                    } else collecting = false;
                    continue;
                }
                if (collecting) ib.push_back(lines[j]);
                if (j == lines.size() - 1) i = j;
            }
            if (!ib.empty()) runLines(ib);
            if (g_returning) return;
            continue;
        }

        // For / endfor
        if (startsWith(s, "For(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            std::string as = s.substr(p1 + 1, p2 - p1 - 1);
            std::vector<std::string> fa; size_t ap = 0;
            while (ap <= as.size()) {
                fa.push_back(getArg(as, ap));
                if (ap < as.size() && as[ap] == ',') ap++;
                else break;
            }
            if (fa.size() < 3) continue;
            std::string var = fa[0]; trim(var);
            int start = (int)calc(fa[1]);
            int end = (int)calc(fa[2]);
            int step = 1;
            bool isExprStep = false;
            if (fa.size() >= 4) {
                std::string ss = fa[3]; trim(ss);
                // Ö»ÓÐµ±ÕæÕýÊÇ±í´ïÊ½²Åµ±×÷±í´ïÊ½£¬·ñÔòÊÇ step
                if (isPureNumber(ss)) step = (int)calc(ss);
                else isExprStep = true;
            } else {
                step = (start <= end) ? 1 : -1;
            }
            if (step == 0 && !isExprStep) {
                reportError(ERR_INVALID_ARG, "For step cannot be 0");
                continue;
            }
            std::vector<std::string> fb;
            int depth = 0;
            for (size_t j = i + 1; j < lines.size(); j++) {
                std::string l = lines[j]; trim(l);
                if (l == "endfor" && depth == 0) { i = j; break; }
                if (startsWith(l, "For(") || startsWith(l, "while(") ||
                    startsWith(l, "if(") || startsWith(l, "Func(")) depth++;
                if (l == "endfor" || l == "endwhile" || l == "endif" || l == "EndFunc") depth--;
                fb.push_back(lines[j]);
                if (j == lines.size() - 1) i = j;
            }
            if (fb.empty()) continue;
            int val = start;
            int iter = 0;
            while (iter < 100000) {
                bool cont;
                if (isExprStep) {
                    int curStep = (int)calc(fa[3]);
                    cont = (curStep >= 0) ? (val <= end) : (val >= end);
                } else {
                    cont = (step >= 0) ? (val <= end) : (val >= end);
                }
                if (!cont) break;
                char buf[64]; sprintf(buf, "%d", val);
                setVar(var, buf);
                bool savedBreak = g_breakLoop;
                g_breakLoop = false;
                runLines(fb);
                if (g_returning) { g_breakLoop = savedBreak; return; }
                if (g_waitingInput) {
                    // Input ÖÐ¶Ï£º±£´æ For Ñ­»·¼ÌÐøÖ¡
                    ResumeFrame frame;
                    frame.type = 1;  // FOR
                    frame.forVar = var;
                    frame.isExprStep = isExprStep;
                    frame.forExprStepArg = isExprStep ? fa[3] : "";
                    frame.forEnd = end;
                    frame.forStep = step;
                    frame.forBody = fb;
                    // ¼ÆËãÏÂÒ»¸öµü´úÖµ
                    if (isExprStep) frame.forVal = (int)calc(fa[3]);
                    else frame.forVal = val + step;
                    g_pendingFrames.push_back(frame);
                    g_breakLoop = savedBreak;
                    break;
                }
                if (g_breakLoop) { g_breakLoop = false; break; }
                g_breakLoop = savedBreak;
                if (isExprStep) val = (int)calc(fa[3]);
                else val += step;
                iter++;
            }
            continue;
        }

        // while / endwhile
        if (startsWith(s, "while(")) {
            size_t p1 = s.find('('), p2 = s.find_last_of(')');
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            std::string cs = s.substr(p1 + 1, p2 - p1 - 1);
            std::vector<std::string> wb;
            int depth = 0;
            for (size_t j = i + 1; j < lines.size(); j++) {
                std::string l = lines[j]; trim(l);
                if (l == "endwhile" && depth == 0) { i = j; break; }
                if (startsWith(l, "while(") || startsWith(l, "For(") ||
                    startsWith(l, "if(") || startsWith(l, "Func(")) depth++;
                if (l == "endwhile" || l == "endfor" || l == "endif" || l == "EndFunc") depth--;
                wb.push_back(lines[j]);
                if (j == lines.size() - 1) i = j;
            }
            if (wb.empty()) continue;
            int iter = 0;
            while (iter < 100000) {
                if (!cond(cs)) break;
                bool savedBreak = g_breakLoop;
                g_breakLoop = false;
                runLines(wb);
                if (g_returning) { g_breakLoop = savedBreak; return; }
                if (g_waitingInput) {
                    // Input ÖÐ¶Ï£º±£´æ while Ñ­»·¼ÌÐøÖ¡
                    ResumeFrame frame;
                    frame.type = 2;  // WHILE
                    frame.whileCond = cs;
                    frame.whileBody = wb;
                    g_pendingFrames.push_back(frame);
                    g_breakLoop = savedBreak;
                    break;
                }
                if (g_breakLoop) { g_breakLoop = false; break; }
                g_breakLoop = savedBreak;
                iter++;
            }
            continue;
        }

        // break Óï¾ä
        if (s == "break") {
            g_breakLoop = true;
            return;
        }

        // ÆÕÍ¨Ö¸Áî
        runCode(s);
        if (g_returning) return;
    }
    // 循环结束后,检测表达式模式 Input (最后一行触发,需重新执行)
    if (g_waitingInput && g_inputPending && !lines.empty()) {
        ResumeFrame frame;
        frame.type = 0;
        frame.lines = lines;
        frame.nextIndex = lines.size() - 1;  // 重新执行最后一行
        g_pendingFrames.push_back(frame);
    }
}

// ---- GUI ----

// 前向声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK OutputWndProc(HWND, UINT, WPARAM, LPARAM);
void ensureOutputWindow();
void handleRun();
void handleStep();
void handleClear();
void handleSendInput();
void handleSave();
void handleOpen();
void handleHelp();
void applySyntaxHighlight();
void updateEditorFont();
void chooseFont();
LRESULT CALLBACK FileDialogProc(HWND, UINT, WPARAM, LPARAM);

// 注册输出窗口类
void registerOutputClass(HINSTANCE hI) {
    WNDCLASS w = {0};
    w.lpfnWndProc = OutputWndProc;
    w.hInstance = hI;
    w.hCursor = LoadCursor(NULL, IDC_ARROW);
    w.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    w.lpszClassName = "SimpleIDEOutput";
    RegisterClass(&w);
}

// 确保输出窗口已创建
void ensureOutputWindow() {
    if (g_outputWndCreated) {
        if (IsWindow(g_hOutputWnd)) {
            ShowWindow(g_hOutputWnd, SW_SHOW);
            SetForegroundWindow(g_hOutputWnd);
            return;
        }
    }
    HINSTANCE hI = GetModuleHandle(NULL);
    registerOutputClass(hI);
    g_hOutputWnd = CreateWindow("SimpleIDEOutput", "Output",
        WS_OVERLAPPEDWINDOW, 720, 100, 600, 500,
        g_hMainWnd, NULL, hI, NULL);
    if (g_hOutputWnd) {
        g_outputWndCreated = true;
        ShowWindow(g_hOutputWnd, SW_SHOW);
        UpdateWindow(g_hOutputWnd);
    }
}

// 清空输出
void clearOutput() {
    if (g_hOutput) SetWindowText(g_hOutput, "");
}

// 设置输入框状态（等待输入/禁用）
void setInputMode(bool waiting) {
    if (!g_hInput || !g_hSendBtn) return;
    if (waiting) {
        EnableWindow(g_hInput, TRUE);
        EnableWindow(g_hSendBtn, TRUE);
        SetFocus(g_hInput);
        SetWindowText(g_hInput, "");
    } else {
        EnableWindow(g_hInput, FALSE);
        EnableWindow(g_hSendBtn, FALSE);
        SetWindowText(g_hInput, "");
    }
}

// 输出窗口过程
LRESULT CALLBACK OutputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 输出区 (上方，只读多行)
            g_hOutput = CreateWindow("EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 100, 100, hWnd, (HMENU)101, GetModuleHandle(NULL), NULL);
            // 输入框 (下方)
            g_hInput = CreateWindow("EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                0, 0, 100, 28, hWnd, (HMENU)102, GetModuleHandle(NULL), NULL);
            // 发送按钮 (默认按钮，回车触发)
            g_hSendBtn = CreateWindow("BUTTON", "Send",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                0, 0, 60, 28, hWnd, (HMENU)103, GetModuleHandle(NULL), NULL);
            // 初始禁用输入框（等待 Input 时才启用）
            EnableWindow(g_hInput, FALSE);
            EnableWindow(g_hSendBtn, FALSE);
            break;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hWnd, &rc);
            int w = rc.right, h = rc.bottom;
            int inputH = 30;
            int btnW = 70;
            int gap = 6;
            // 输出区填满上方
            SetWindowPos(g_hOutput, NULL, gap, gap,
                w - 2*gap > 1 ? w - 2*gap : 1,
                h - inputH - 3*gap > 1 ? h - inputH - 3*gap : 1,
                SWP_NOZORDER);
            // 输入框在底部
            SetWindowPos(g_hInput, NULL, gap, h - inputH - gap,
                w - btnW - 3*gap > 1 ? w - btnW - 3*gap : 1,
                inputH, SWP_NOZORDER);
            // 发送按钮在输入框右侧
            SetWindowPos(g_hSendBtn, NULL, w - btnW - gap, h - inputH - gap,
                btnW, inputH, SWP_NOZORDER);
            break;
        }
        case WM_APP_APPENDTEXT: {
            // 接收异步追加文本请求
            std::string* p = (std::string*)lParam;
            if (p) {
                if (g_hOutput) {
                    int len = GetWindowTextLength(g_hOutput);
                    SendMessage(g_hOutput, EM_SETSEL, len, len);
                    SendMessage(g_hOutput, EM_REPLACESEL, 0, (LPARAM)p->c_str());
                }
                delete p;
            }
            break;
        }
        case WM_COMMAND: {
            // Send 按钮点击 (控件ID 103)
            if (LOWORD(wParam) == 103) {
                handleSendInput();
            }
            break;
        }
        case WM_CLOSE: {
            // 不销毁，只是隐藏，避免丢失输出
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        case WM_DESTROY: {
            g_hOutput = NULL;
            g_hInput = NULL;
            g_hSendBtn = NULL;
            g_hOutputWnd = NULL;
            g_outputWndCreated = false;
            break;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 主窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMainWnd = hWnd;
            // 加载 RichEdit 库
            LoadLibraryA("msftedit.dll");

            // 创建菜单栏
            HMENU hMenuBar = CreateMenu();
            HMENU hFileMenu = CreatePopupMenu();
            AppendMenuA(hFileMenu, MF_STRING, IDM_OPEN, "&Open...\tCtrl+O");
            AppendMenuA(hFileMenu, MF_STRING, IDM_SAVE, "&Save...\tCtrl+S");
            AppendMenuA(hFileMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFileMenu, MF_STRING, IDM_EXIT, "E&xit\tCtrl+Q");
            AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, "&File");

            HMENU hRunMenu = CreatePopupMenu();
            AppendMenuA(hRunMenu, MF_STRING, IDM_RUN, "&Run\tF5");
            AppendMenuA(hRunMenu, MF_STRING, IDM_STEP, "&Step\tF6");
            AppendMenuA(hRunMenu, MF_STRING, IDM_CLEAR, "&Clear Code\tCtrl+L");
            AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hRunMenu, "&Run");

            HMENU hFontMenu = CreatePopupMenu();
            AppendMenuA(hFontMenu, MF_STRING, IDM_FONT, "&Choose Font...\tCtrl+F");
            AppendMenuA(hFontMenu, MF_STRING, IDM_FONT_INC, "&Increase Size\tCtrl++");
            AppendMenuA(hFontMenu, MF_STRING, IDM_FONT_DEC, "&Decrease Size\tCtrl+-");
            AppendMenuA(hFontMenu, MF_STRING, IDM_FONT_RESET, "&Reset Font\tCtrl+0");
            AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hFontMenu, "F&ormat");

            HMENU hHelpMenu = CreatePopupMenu();
            AppendMenuA(hHelpMenu, MF_STRING, IDM_HELP, "&Help\tF1");
            AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hHelpMenu, "&Help");

            SetMenu(hWnd, hMenuBar);

            // 代码编辑区 (RichEdit, 多行,支持高亮)
            g_hMultiEdit = CreateWindowExA(0, "RICHEDIT50W", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER,
                0, 0, 100, 100, hWnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            // 设置等宽字体
            updateEditorFont();
            // 默认提示
            SetWindowText(g_hMultiEdit,
                "# SimpleIDE v2.0\r\n"
                "# Menu: File(Open/Save) Run(Run/Step/Clear) Format(Font) Help\r\n"
                "# Shortcut: F5=Run F6=Step Ctrl+O=Open Ctrl+S=Save F1=Help\r\n"
                "# Format: Ctrl+F=Font Ctrl++=Bigger Ctrl+-=Smaller Ctrl+0=Reset\r\n"
                "\r\n"
                "PrintLog(\"Hello, World!\")\r\n"
                "listNew(nums, 3, 1, 4, 1, 5, 9, 2, 6)\r\n"
                "listSort(nums)\r\n"
                "listPrint(nums)\r\n"
                "PrintLog(\"Sum =\", listSum(nums))\r\n"
                "PrintLog(\"Upper:\", strUpper(\"hello\"))\r\n");
            // 应用语法高亮
            applySyntaxHighlight();
            break;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hWnd, &rc);
            int w = rc.right, h = rc.bottom;
            // 编辑区填满整个客户区
            if (g_hMultiEdit) SetWindowPos(g_hMultiEdit, NULL, 0, 0, w, h, SWP_NOZORDER);
            break;
        }
        case WM_COMMAND: {
            int cmd = LOWORD(wParam);
            if (cmd == IDM_RUN) handleRun();
            else if (cmd == IDM_STEP) handleStep();
            else if (cmd == IDM_CLEAR) handleClear();
            else if (cmd == IDM_SAVE) handleSave();
            else if (cmd == IDM_OPEN) handleOpen();
            else if (cmd == IDM_HELP) handleHelp();
            else if (cmd == IDM_FONT) chooseFont();
            else if (cmd == IDM_FONT_INC) { g_fontSize++; updateEditorFont(); }
            else if (cmd == IDM_FONT_DEC) { if (g_fontSize > 6) { g_fontSize--; updateEditorFont(); } }
            else if (cmd == IDM_FONT_RESET) { g_fontSize = 15; strcpy(g_fontName, "Consolas"); updateEditorFont(); }
            else if (cmd == IDM_EXIT) { PostQuitMessage(0); }
            // 编辑区内容变化时触发高亮 (防重入在函数内部)
            if (LOWORD(wParam) == 1 && HIWORD(wParam) == EN_CHANGE) {
                applySyntaxHighlight();
            }
            break;
        }
        case WM_KEYDOWN: {
            // 快捷键在消息循环中处理
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---- 业务逻辑 ----

// 读取编辑区所有行
std::vector<std::string> readEditorLines() {
    std::vector<std::string> ls;
    int len = (int)SendMessage(g_hMultiEdit, WM_GETTEXTLENGTH, 0, 0);
    if (len <= 0) return ls;
    if (len > 1024 * 1024) len = 1024 * 1024;
    std::vector<char> buf(len + 1, 0);
    SendMessage(g_hMultiEdit, WM_GETTEXT, len + 1, (LPARAM)buf.data());
    std::string t(buf.data());
    std::istringstream iss(t);
    std::string l;
    while (std::getline(iss, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        ls.push_back(l);
    }
    return ls;
}

// 处理 Run 按钮
void handleRun() {
    // 如果正在等待输入，交给发送按钮处理
    if (g_waitingInput) {
        handleSendInput();
        return;
    }
    std::vector<std::string> ls = readEditorLines();
    if (ls.empty()) {
        ensureOutputWindow();
        reportError(ERR_NONE, "editor is empty");
        return;
    }
    // 确保输出窗口已创建并清空
    ensureOutputWindow();
    clearOutput();
    Log("=== Run (" + std::to_string(ls.size()) + " lines) ===");
    // 清空恢复状态和错误
    g_pendingFrames.clear();
    g_resumeStack.clear();
    g_returning = false;
    g_breakLoop = false;
    g_vars.clear();
    g_funcs.clear();
    g_lists.clear();
    g_consts.clear();
    g_dicts.clear();
    g_loadedPacks.clear();
    g_nativeFuncs.clear();
    for (auto& h : g_loadedDlls) FreeLibrary(h);
    g_loadedDlls.clear();
    g_inputPending = false;
    g_inputResult.clear();
    clearErrors();
    g_currentLineNo = 0;
    // 执行
    runLines(ls);
    if (g_waitingInput) {
        flushPendingFrames();
        // 启用输入框等待用户输入
        setInputMode(true);
    } else {
        // 打印错误汇总
        printAllErrors();
        Log("=== Done ===");
        setInputMode(false);
    }
}

// 处理 Step 按钮
void handleStep() {
    if (g_waitingInput) return;  // 等待输入时不响应
    ensureOutputWindow();
    if (g_multiLines.empty()) {
        g_multiLines = readEditorLines();
        g_currentLine = 0;
        if (g_multiLines.empty()) {
            reportError(ERR_SYNTAX, "editor is empty");
            return;
        }
        Log("=== Step Start ===");
    }
    // 收集一个完整块并执行
    while (g_currentLine < (int)g_multiLines.size()) {
        std::string cur = g_multiLines[g_currentLine];
        std::string t = cur; trim(t);
        if (t.empty() || t[0] == '#') { g_currentLine++; continue; }
        if (startsWith(t, "Func(")) {
            std::vector<std::string> blk;
            int depth = 0;
            while (g_currentLine < (int)g_multiLines.size()) {
                std::string l = g_multiLines[g_currentLine]; std::string tl = l; trim(tl);
                blk.push_back(l);
                g_currentLine++;
                if (tl == "EndFunc" && depth == 0) break;
                if (startsWith(tl, "Func(") || startsWith(tl, "if(") ||
                    startsWith(tl, "For(") || startsWith(tl, "while(")) depth++;
                if (tl == "EndFunc" || tl == "endif" || tl == "endfor" || tl == "endwhile") depth--;
            }
            runLines(blk);
            break;
        }
        if (startsWith(t, "if(") || startsWith(t, "For(") || startsWith(t, "while(")) {
            std::vector<std::string> blk;
            int depth = 0;
            std::string endTag;
            if (startsWith(t, "if(")) endTag = "endif";
            else if (startsWith(t, "For(")) endTag = "endfor";
            else endTag = "endwhile";
            bool found = false;
            while (g_currentLine < (int)g_multiLines.size()) {
                std::string l = g_multiLines[g_currentLine]; std::string tl = l; trim(tl);
                blk.push_back(l);
                g_currentLine++;
                if (tl == endTag && depth == 0) { found = true; break; }
                if (startsWith(tl, "Func(") || startsWith(tl, "if(") ||
                    startsWith(tl, "For(") || startsWith(tl, "while(")) depth++;
                if (tl == "EndFunc" || tl == "endif" || tl == "endfor" || tl == "endwhile") depth--;
            }
            if (found) runLines(blk);
            break;
        }
        runCode(cur);
        g_currentLine++;
        break;
    }
    if (g_currentLine >= (int)g_multiLines.size()) {
        Log("=== Step Done ===");
        g_multiLines.clear();
        g_currentLine = 0;
    }
}

// 处理 Clear 按钮
void handleClear() {
    if (g_hMultiEdit) SetWindowText(g_hMultiEdit, "");
}

// ---- 简易文件名输入对话框 (不依赖 comdlg32) ----
// 全局状态用于对话框通信
std::string g_fileDialogResult;
bool g_fileDialogDone;
HWND g_hFileDlg, g_hFileInput, g_hFileOK, g_hFileCancel;
std::string g_fileDialogTitle;

LRESULT CALLBACK FileDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 提示标签
            CreateWindow("STATIC", "File path:",
                WS_CHILD | WS_VISIBLE,
                10, 10, 380, 20, hWnd, (HMENU)201, GetModuleHandle(NULL), NULL);
            // 输入框
            g_hFileInput = CreateWindow("EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                10, 35, 380, 24, hWnd, (HMENU)202, GetModuleHandle(NULL), NULL);
            // 确定/取消按钮
            g_hFileOK = CreateWindow("BUTTON", "OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                200, 70, 80, 26, hWnd, (HMENU)203, GetModuleHandle(NULL), NULL);
            g_hFileCancel = CreateWindow("BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE,
                290, 70, 80, 26, hWnd, (HMENU)204, GetModuleHandle(NULL), NULL);
            SetFocus(g_hFileInput);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 203) {
                // OK: 读取输入框内容
                int len = GetWindowTextLength(g_hFileInput);
                std::vector<char> buf(len + 1, 0);
                GetWindowText(g_hFileInput, buf.data(), len + 1);
                g_fileDialogResult = buf.data();
                g_fileDialogDone = true;
                DestroyWindow(hWnd);
            } else if (LOWORD(wParam) == 204) {
                // Cancel
                g_fileDialogResult = "";
                g_fileDialogDone = true;
                DestroyWindow(hWnd);
            }
            break;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_RETURN) {
                SendMessage(hWnd, WM_COMMAND, 203, 0);
            } else if (wParam == VK_ESCAPE) {
                SendMessage(hWnd, WM_COMMAND, 204, 0);
            }
            break;
        }
        case WM_CLOSE: {
            g_fileDialogResult = "";
            g_fileDialogDone = true;
            DestroyWindow(hWnd);
            break;
        }
        case WM_DESTROY: {
            // 重新启用主窗口
            if (g_hMainWnd) EnableWindow(g_hMainWnd, TRUE);
            SetFocus(g_hMainWnd);
            break;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 注册文件对话框类
void registerFileDialogClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASS w = {0};
    w.lpfnWndProc = FileDialogProc;
    w.hInstance = GetModuleHandle(NULL);
    w.hCursor = LoadCursor(NULL, IDC_ARROW);
    w.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    w.lpszClassName = "SimpleIDEFileDialog";
    RegisterClass(&w);
    registered = true;
}

// 弹出文件名输入对话框, 返回用户输入的路径(空串=取消)
std::string promptFilePath(const std::string& title) {
    registerFileDialogClass();
    g_fileDialogResult = "";
    g_fileDialogDone = false;
    g_fileDialogTitle = title;
    // 禁用主窗口(模拟模态)
    if (g_hMainWnd) EnableWindow(g_hMainWnd, FALSE);
    g_hFileDlg = CreateWindow("SimpleIDEFileDialog", title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        200, 200, 410, 140, g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hFileDlg, SW_SHOW);
    UpdateWindow(g_hFileDlg);
    // 消息循环(模态)
    MSG m;
    while (!g_fileDialogDone && GetMessage(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return g_fileDialogResult;
}

// 处理保存 (.sal 文件)
void handleSave() {
    std::string path = promptFilePath("Save to file (enter .sal path)");
    if (path.empty()) return;
    // 读取编辑区内容
    int len = (int)SendMessage(g_hMultiEdit, WM_GETTEXTLENGTH, 0, 0);
    if (len < 0) len = 0;
    std::vector<char> buf(len + 1, 0);
    SendMessage(g_hMultiEdit, WM_GETTEXT, len + 1, (LPARAM)buf.data());
    // 写入文件
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        MessageBox(g_hMainWnd, "Cannot save file", "Error", MB_OK | MB_ICONERROR);
        reportError(ERR_FILE_IO, "cannot save file", path);
        return;
    }
    // 写入 BOM 标识和版本号
    fwrite("SALF1", 5, 1, fp);
    fwrite(buf.data(), 1, len, fp);
    fclose(fp);
    ensureOutputWindow();
    Log("Saved: " + path);
}

// 处理打开 (.sal 文件)
void handleOpen() {
    std::string path = promptFilePath("Open file (enter .sal path)");
    if (path.empty()) return;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        MessageBox(g_hMainWnd, "Cannot open file", "Error", MB_OK | MB_ICONERROR);
        reportError(ERR_FILE_IO, "cannot open file", path);
        return;
    }
    // 读取文件
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 5) {
        fclose(fp);
        MessageBox(g_hMainWnd, "Invalid file format", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    char header[6] = {0};
    fread(header, 5, 1, fp);
    if (strncmp(header, "SALF1", 5) != 0) {
        // 非标准格式,尝试作为纯文本读取
        fseek(fp, 0, SEEK_SET);
    }
    std::vector<char> content(fsize + 1, 0);
    long readLen = fread(content.data(), 1, fsize, fp);
    content[readLen] = 0;
    fclose(fp);
    SetWindowText(g_hMultiEdit, content.data());
    applySyntaxHighlight();
    ensureOutputWindow();
    Log("Loaded: " + path);
}

// ---- 帮助窗口 ----
HWND g_hHelpWnd, g_hHelpEdit;
const char* g_helpText =
"================ SimpleIDE v2.0 使用说明 ================\r\n"
"\r\n"
"【界面介绍】\r\n"
"  主窗口: 代码编辑区(上方) + 按钮栏(下方)\r\n"
"    Run   - 运行全部代码\r\n"
"    Step  - 逐块执行(函数/if/For/while 作为一个块)\r\n"
"    Clear - 清空编辑区\r\n"
"    Save  - 保存代码到 .sal 文件\r\n"
"    Open  - 从 .sal 文件加载代码\r\n"
"    Help  - 显示本帮助\r\n"
"  输出窗口: 运行后弹出, 显示 PrintLog 输出\r\n"
"    底部输入框: Input 时在此输入, 回车或点 Send 提交\r\n"
"\r\n"
"【基本语法】\r\n"
"  # 注释行 (以#开头)\r\n"
"  变量赋值: x = 42\r\n"
"  字符串赋值: s = \"hello\"\r\n"
"  表达式赋值: y = (3 + 4) * 2\r\n"
"  输出: PrintLog(\"Hello, World!\")\r\n"
"  输入: Input(\"提示:\", varName)\r\n"
"  整数输入: InputInt(\"提示:\", varName)\r\n"
"\r\n"
"【控制结构】\r\n"
"  if(条件)\r\n"
"    ...\r\n"
"  elseIf(条件)\r\n"
"    ...\r\n"
"  else\r\n"
"    ...\r\n"
"  endif\r\n"
"\r\n"
"  For(i, 1, 10)\r\n"
"    ...\r\n"
"  endfor\r\n"
"  For(i, 10, 1, -1)  ' 带步长\r\n"
"  endfor\r\n"
"\r\n"
"  while(条件)\r\n"
"    ...\r\n"
"  endwhile\r\n"
"\r\n"
"  break  ' 跳出循环\r\n"
"\r\n"
"【函数定义】\r\n"
"  Func(函数名, 参数1, 参数2)\r\n"
"    return 表达式\r\n"
"  EndFunc\r\n"
"  示例:\r\n"
"    Func(add, a, b)\r\n"
"      return a + b\r\n"
"    EndFunc\r\n"
"    PrintLog(add(3, 4))  ' 输出 7\r\n"
"\r\n"
"【常量 CboxS】\r\n"
"  CboxS(NAME, value)  ' 定义常量, 不可修改\r\n"
"  getCbox(NAME)       ' 读取常量值\r\n"
"  isCbox(NAME)        ' 是否是常量\r\n"
"  showAllCboxS()      ' 显示所有常量\r\n"
"  示例:\r\n"
"    CboxS(PI, 3.14)\r\n"
"    CboxS(GREETING, \"Hello\")\r\n"
"    PrintLog(PI * 2)            ' 输出 6.28\r\n"
"    PrintLog(getCbox(GREETING)) ' 输出 Hello\r\n"
"\r\n"
"【列表 List】\r\n"
"  listNew(name, e1, e2, ...)  ' 创建列表\r\n"
"  listLen(name)               ' 长度\r\n"
"  listGet(name, i)            ' 取第i个元素\r\n"
"  listSet(name, i, val)       ' 设置第i个元素\r\n"
"  listAppend(name, val)       ' 追加\r\n"
"  listInsert(name, i, val)    ' 插入\r\n"
"  listRemove(name, i)         ' 删除第i个\r\n"
"  listPop(name)               ' 弹出末尾\r\n"
"  listClear(name)             ' 清空\r\n"
"  listSort(name)              ' 排序(原地)\r\n"
"  listReverse(name)           ' 反转(原地)\r\n"
"  listFind(name, val)         ' 查找位置, -1未找到\r\n"
"  listContains(name, val)     ' 是否包含\r\n"
"  listCopy(src, dst)          ' 复制\r\n"
"  listSum(name)               ' 求和\r\n"
"  listJoin(name, delim)       ' 连接为字符串\r\n"
"  listPrint(name)             ' 打印 [e1, e2, ...]\r\n"
"  示例:\r\n"
"    listNew(nums, 3, 1, 4, 1, 5)\r\n"
"    listSort(nums)\r\n"
"    listPrint(nums)            ' 输出 [1, 1, 3, 4, 5]\r\n"
"    PrintLog(listSum(nums))    ' 输出 14\r\n"
"\r\n"
"【字典 Dict】\r\n"
"  dictNew(name)               ' 创建空字典\r\n"
"  dictSet(name, key, value)   ' 设置键值对\r\n"
"  dictGet(name, key)          ' 取键值\r\n"
"  dictHas(name, key)          ' 是否包含键\r\n"
"  dictRemove(name, key)       ' 删除键\r\n"
"  dictLen(name)               ' 键值对数量\r\n"
"  dictClear(name)             ' 清空\r\n"
"  dictKeys(name)              ' 获取所有键(返回列表名)\r\n"
"  dictValues(name)            ' 获取所有值(返回列表名)\r\n"
"  dictCopy(src, dst)          ' 复制\r\n"
"  dictMerge(dst, src)         ' 合并src到dst\r\n"
"  dictPrint(name)             ' 打印 {k1: v1, k2: v2}\r\n"
"  示例:\r\n"
"    dictNew(scores)\r\n"
"    dictSet(scores, \"Alice\", 95)\r\n"
"    dictSet(scores, \"Bob\", 87)\r\n"
"    dictPrint(scores)          ' 输出 {Alice: 95, Bob: 87}\r\n"
"    PrintLog(dictGet(scores, \"Alice\"))  ' 输出 95\r\n"
"    PrintLog(dictLen(scores))  ' 输出 2\r\n"
"    keys = dictKeys(scores)\r\n"
"    listPrint(keys)            ' 输出 [Alice, Bob]\r\n"
"\r\n"
"【字符串函数 (25+)】\r\n"
"  strLen(s)               ' 长度\r\n"
"  strUpper(s)             ' 转大写\r\n"
"  strLower(s)             ' 转小写\r\n"
"  strSub(s, start, len)   ' 子串\r\n"
"  strCat(s1, s2, ...)     ' 连接\r\n"
"  strRep(s, n)            ' 重复n次\r\n"
"  strFind(s, sub)         ' 查找, -1未找到\r\n"
"  strRFind(s, sub)        ' 反向查找\r\n"
"  strReplace(s, old, new) ' 替换全部\r\n"
"  strTrim(s)              ' 去首尾空白\r\n"
"  strSplit(s, delim)      ' 分割为列表\r\n"
"  strJoin(list, delim)    ' 列表连接为字符串\r\n"
"  strChar(s, i)           ' 取第i个字符\r\n"
"  strAscii(c)             ' 字符转ASCII\r\n"
"  strChr(n)               ' ASCII转字符\r\n"
"  strReverse(s)           ' 反转\r\n"
"  strStartsWith(s, p)     ' 前缀检测\r\n"
"  strEndsWith(s, p)       ' 后缀检测\r\n"
"  strContains(s, sub)     ' 包含检测\r\n"
"  strCount(s, sub)        ' 出现次数\r\n"
"  strLeft(s, n)           ' 取左边n个\r\n"
"  strRight(s, n)          ' 取右边n个\r\n"
"  strToInt(s)             ' 转整数\r\n"
"  strToFloat(s)           ' 转浮点\r\n"
"  示例:\r\n"
"    s = \"Hello World\"\r\n"
"    PrintLog(strLen(s))           ' 11\r\n"
"    PrintLog(strUpper(s))         ' HELLO WORLD\r\n"
"    PrintLog(strSub(s, 0, 5))     ' Hello\r\n"
"    PrintLog(strFind(s, \"World\")) ' 6\r\n"
"    PrintLog(strReplace(s, \"World\", \"There\"))  ' Hello There\r\n"
"\r\n"
"【其他内置函数】\r\n"
"  showAllBoxes()    ' 显示所有变量\r\n"
"  clearAllBoxes()   ' 清空所有变量\r\n"
"  showFuncs()       ' 显示所有函数\r\n"
"  box(x, val)       ' 等价于 x = val\r\n"
"  boxS(label, x, val) ' 带标签的赋值\r\n"
"\r\n"
"【库导入 GetPack】\r\n"
"  GetPack(\"库名\")  ' 导入库,库文件在 libs/库名.sal\r\n"
"  库用我们的语言编写,包含 Func 定义和全局变量\r\n"
"  导入后可直接调用库中的函数\r\n"
"  示例:\r\n"
"    GetPack(\"math\")  ' 导入 libs/math.sal\r\n"
"    PrintLog(sqrt(16))  ' 调用库中定义的 sqrt 函数\r\n"
"  库文件示例 (libs/math.sal):\r\n"
"    Func(sqrt, x)\r\n"
"      return x ^ 0.5\r\n"
"    EndFunc\r\n"
"    Func(square, x)\r\n"
"      return x * x\r\n"
"    EndFunc\r\n"
"\r\n"
"【C++库导入 GetCPack】\r\n"
"  GetCPack(\"库名\")  ' 导入C++库\r\n"
"  支持两种文件: cpacks/库名.dll (已编译) 或 cpacks/库名.cpp (源码)\r\n"
"  .cpp源码会自动编译为DLL (需要系统已安装g++)\r\n"
"  DLL需导出 registerPack() 函数注册原生函数\r\n"
"  示例:\r\n"
"    GetCPack(\"fastmath\")  ' 导入 cpacks/fastmath.cpp 或 .dll\r\n"
"    PrintLog(fastAdd(3, 4))  ' 调用C++函数\r\n"
"  C++库示例 (cpacks/fastmath.cpp):\r\n"
"    typedef const char* (*NativeFunc)(const char**, int);\r\n"
"    extern \"C\" __declspec(dllimport) void registerNativeFunc(const char*, NativeFunc);\r\n"
"    const char* fastAdd(const char** a, int n) {\r\n"
"      static char b[32]; sprintf(b, \"%d\", atoi(a[0])+atoi(a[1]));\r\n"
"      return b;\r\n"
"    }\r\n"
"    extern \"C\" __declspec(dllexport) void registerPack() {\r\n"
"      registerNativeFunc(\"fastAdd\", fastAdd);\r\n"
"    }\r\n"
"  手动编译DLL: g++ -shared -o cpacks/fastmath.dll cpacks/fastmath.cpp\r\n"
"\r\n"
"【文件操作】\r\n"
"  Save - 输入文件路径(如 test.sal), 保存代码\r\n"
"  Open - 输入文件路径, 加载代码\r\n"
"  .sal 文件格式: SALF1 头 + UTF-8 文本\r\n"
"\r\n"
"【运算符】\r\n"
"  算术: + - * /\r\n"
"  比较: == != > < >= <=\r\n"
"  字符串拼接: + (当操作数含字符串时)\r\n"
"  括号: ()\r\n"
"\r\n"
"【完整示例】\r\n"
"  # 计算斐波那契数列前10项\r\n"
"  listNew(fib)\r\n"
"  listAppend(fib, 0)\r\n"
"  listAppend(fib, 1)\r\n"
"  For(i, 2, 9)\r\n"
"    a = listGet(fib, i-1)\r\n"
"    b = listGet(fib, i-2)\r\n"
"    listAppend(fib, a + b)\r\n"
"  endfor\r\n"
"  listPrint(fib)\r\n"
"  PrintLog(\"Sum:\", listSum(fib))\r\n"
"\r\n"
"  # 字典示例: 学生成绩管理\r\n"
"  dictNew(grades)\r\n"
"  dictSet(grades, \"Alice\", 95)\r\n"
"  dictSet(grades, \"Bob\", 87)\r\n"
"  dictSet(grades, \"Carol\", 92)\r\n"
"  keys = dictKeys(grades)\r\n"
"  For(i, 0, dictLen(grades) - 1)\r\n"
"    name = listGet(keys, i)\r\n"
"    score = dictGet(grades, name)\r\n"
"    PrintLog(name, \":\", score)\r\n"
"  endfor\r\n"
"  PrintLog(\"Average:\", listSum(keys))  ' 需自己实现求平均\r\n"
"\r\n"
"【错误处理】\r\n"
"  运行时错误会输出 [Error E代码 L行号] 信息\r\n"
"  错误码:\r\n"
"    E1001 语法错误  E1002 未定义变量  E1003 未定义函数\r\n"
"    E1004 无效参数  E1005 参数数量    E1006 无效数字\r\n"
"    E1008 无效列表  E1009 除零        E1010 越界\r\n"
"    E1011 类型错误  E1012 无效标识符  E1013 文件IO\r\n"
"  程序结束时会汇总所有错误\r\n"
"\r\n"
"========================================================\r\n";

LRESULT CALLBACK HelpWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hHelpEdit = CreateWindow("EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 100, 100, hWnd, (HMENU)301, GetModuleHandle(NULL), NULL);
            SetWindowText(g_hHelpEdit, g_helpText);
            // 设置等宽字体
            HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            if (hFont) SendMessage(g_hHelpEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hWnd, &rc);
            if (g_hHelpEdit) SetWindowPos(g_hHelpEdit, NULL, 0, 0,
                rc.right, rc.bottom, SWP_NOZORDER);
            break;
        }
        case WM_CLOSE: {
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---- 字体管理 ----
void updateEditorFont() {
    if (g_hEditorFont) DeleteObject(g_hEditorFont);
    g_hEditorFont = CreateFont(g_fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, g_fontName);
    if (g_hEditorFont && g_hMultiEdit) {
        SendMessage(g_hMultiEdit, WM_SETFONT, (WPARAM)g_hEditorFont, TRUE);
        applySyntaxHighlight();
    }
}

// 简易字体选择对话框 (自实现,不依赖 comdlg32)
void chooseFont() {
    // 提供常用字体和字号选择
    static const char* fonts[] = {"Consolas", "Courier New", "Lucida Console", "MS Gothic", "Fixedsys", NULL};
    static const int sizes[] = {10, 12, 14, 15, 16, 18, 20, 24, 28, 0};

    // 用一个简单的输入框让用户输入字体名和字号
    // 格式: fontname,size  如 Consolas,16
    std::string prompt = "Current: " + std::string(g_fontName) + "," + std::to_string(g_fontSize);
    prompt += "\nEnter new font (name,size) e.g. Consolas,16\n";
    prompt += "Fonts: Consolas/Courier New/Lucida Console/MS Gothic/Fixedsys\n";
    prompt += "Sizes: 10/12/14/15/16/18/20/24/28";

    // 使用 MessageBox + 简易输入窗口
    // 创建一个字体选择窗口
    static HWND hFontDlg = NULL;
    if (hFontDlg && IsWindow(hFontDlg)) { SetForegroundWindow(hFontDlg); return; }

    // 注册字体对话框类
    static bool registered = false;
    if (!registered) {
        WNDCLASS w = {0};
        w.lpfnWndProc = [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            switch (msg) {
                case WM_CREATE: {
                    CreateWindow("STATIC", "Font name:",
                        WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, hWnd, (HMENU)301, GetModuleHandle(NULL), NULL);
                    HWND hFontName = CreateWindow("EDIT", g_fontName,
                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                        110, 10, 180, 22, hWnd, (HMENU)302, GetModuleHandle(NULL), NULL);
                    CreateWindow("STATIC", "Size:",
                        WS_CHILD | WS_VISIBLE, 10, 40, 100, 20, hWnd, (HMENU)303, GetModuleHandle(NULL), NULL);
                    char szSize[16]; sprintf(szSize, "%d", g_fontSize);
                    CreateWindow("EDIT", szSize,
                        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                        110, 40, 60, 22, hWnd, (HMENU)304, GetModuleHandle(NULL), NULL);
                    CreateWindow("BUTTON", "OK",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        100, 75, 70, 25, hWnd, (HMENU)305, GetModuleHandle(NULL), NULL);
                    CreateWindow("BUTTON", "Cancel",
                        WS_CHILD | WS_VISIBLE,
                        180, 75, 70, 25, hWnd, (HMENU)306, GetModuleHandle(NULL), NULL);
                    SetFocus(hFontName);
                    break;
                }
                case WM_COMMAND: {
                    if (LOWORD(wParam) == 305) {  // OK
                        char name[64] = {0}; char sz[16] = {0};
                        GetDlgItemText(hWnd, 302, name, 64);
                        GetDlgItemText(hWnd, 304, sz, 16);
                        if (name[0] && sz[0]) {
                            strncpy(g_fontName, name, 63);
                            g_fontName[63] = 0;
                            g_fontSize = atoi(sz);
                            if (g_fontSize < 6) g_fontSize = 6;
                            if (g_fontSize > 72) g_fontSize = 72;
                            updateEditorFont();
                        }
                        DestroyWindow(hWnd);
                    } else if (LOWORD(wParam) == 306) {  // Cancel
                        DestroyWindow(hWnd);
                    }
                    break;
                }
                case WM_CLOSE: DestroyWindow(hWnd); break;
                case WM_DESTROY: break;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
        };
        w.hInstance = GetModuleHandle(NULL);
        w.hCursor = LoadCursor(NULL, IDC_ARROW);
        w.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        w.lpszClassName = "SimpleIDEFontDlg";
        RegisterClass(&w);
        registered = true;
    }
    hFontDlg = CreateWindow("SimpleIDEFontDlg", "Choose Font",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        300, 200, 310, 140, g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(hFontDlg, SW_SHOW);
    UpdateWindow(hFontDlg);
}

// ---- 语法高亮 ----
// 关键字列表
static const char* g_keywords[] = {
    "Func","EndFunc","if","elseIf","else","endif",
    "For","endfor","while","endwhile","return","break",
    "try","IfErrorToDo","endTry",
    "Input","InputInt","PrintLog","showAllBoxes","clearAllBoxes","showFuncs",
    "box","boxS","CboxS","getCbox","isCbox","showAllCboxS",
    "GetPack",
    "GetCPack",
    NULL
};

// 判断字符串是否是关键字
static bool isKeyword(const std::string& s) {
    for (int i = 0; g_keywords[i]; i++) {
        if (s == g_keywords[i]) return true;
    }
    return false;
}

// 内置函数列表 (str*/list*/dict*)
static const char* g_builtinFuncs[] = {
    "strLen","strUpper","strLower","strSub","strCat","strRep","strFind","strRFind",
    "strReplace","strTrim","strSplit","strJoin","strChar","strAscii","strChr",
    "strReverse","strStartsWith","strEndsWith","strContains","strCount",
    "strLeft","strRight","strToInt","strToFloat","numToStr",
    "listNew","listLen","listGet","listSet","listAppend","listInsert","listRemove",
    "listPop","listClear","listSort","listReverse","listFind","listContains",
    "listCopy","listSum","listJoin","listPrint",
    "abs","sqrt","pow","max","min","floor","ceil","round","random","mod","sin","cos","log",
    "dictNew","dictSet","dictGet","dictHas","dictRemove","dictLen",
    "dictClear","dictKeys","dictValues","dictCopy","dictPrint","dictMerge",
    NULL
};

static bool isBuiltinFunc(const std::string& s) {
    for (int i = 0; g_builtinFuncs[i]; i++) {
        if (s == g_builtinFuncs[i]) return true;
    }
    return false;
}

// 对 RichEdit 控件应用语法高亮
void applySyntaxHighlight() {
    if (!g_hMultiEdit || !g_highlightEnabled) return;

    // 防重入
    static bool inHighlight = false;
    if (inHighlight) return;
    inHighlight = true;

    // 保存当前光标位置 (选区起止)
    DWORD selStart = 0, selEnd = 0;
    LRESULT sel = SendMessage(g_hMultiEdit, EM_GETSEL, 0, 0);
    selStart = LOWORD(sel);
    selEnd = HIWORD(sel);

    // 保存滚动位置
    int firstVisible = (int)SendMessage(g_hMultiEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    // 隐藏选区高亮，避免闪烁
    SendMessage(g_hMultiEdit, EM_HIDESELECTION, TRUE, 0);
    // 禁用重绘
    SendMessage(g_hMultiEdit, WM_SETREDRAW, FALSE, 0);

    // 用 WM_GETTEXT 获取文本 (比 GetWindowText 更可靠)
    LRESULT len = SendMessage(g_hMultiEdit, WM_GETTEXTLENGTH, 0, 0);
    if (len <= 0) {
        SendMessage(g_hMultiEdit, WM_SETREDRAW, TRUE, 0);
        SendMessage(g_hMultiEdit, EM_HIDESELECTION, FALSE, 0);
        inHighlight = false;
        return;
    }
    std::vector<char> buf(len + 1, 0);
    SendMessage(g_hMultiEdit, WM_GETTEXT, len + 1, (LPARAM)buf.data());
    std::string text(buf.data());

    // 先全部设为默认颜色
    CHARFORMAT cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = COLOR_DEFAULT;
    SendMessage(g_hMultiEdit, EM_SETSEL, 0, -1);  // 全选 (0,-1)
    SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 逐字符扫描
    size_t i = 0;
    while (i < text.size()) {
        // 跳过空白
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
        if (i >= text.size()) break;

        // 注释 #...
        if (text[i] == '#') {
            size_t start = i;
            while (i < text.size() && text[i] != '\n') i++;
            cf.crTextColor = COLOR_COMMENT;
            SendMessage(g_hMultiEdit, EM_SETSEL, (WPARAM)start, (LPARAM)i);
            SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            continue;
        }

        // 字符串 "..." 或 '...'
        if (text[i] == '"' || text[i] == '\'') {
            char q = text[i];
            size_t start = i;
            i++;
            while (i < text.size() && text[i] != q) {
                if (text[i] == '\\' && i + 1 < text.size()) i++;
                i++;
            }
            if (i < text.size()) i++;  // 跳过结束引号
            cf.crTextColor = COLOR_STRING;
            SendMessage(g_hMultiEdit, EM_SETSEL, (WPARAM)start, (LPARAM)i);
            SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            continue;
        }

        // 数字
        if (text[i] >= '0' && text[i] <= '9') {
            size_t start = i;
            while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.')) i++;
            cf.crTextColor = COLOR_NUMBER;
            SendMessage(g_hMultiEdit, EM_SETSEL, (WPARAM)start, (LPARAM)i);
            SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            continue;
        }

        // 标识符 (关键字/函数名/变量名)
        if (isalpha((unsigned char)text[i]) || text[i] == '_') {
            size_t start = i;
            while (i < text.size() && (isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
            std::string word = text.substr(start, i - start);
            if (isKeyword(word)) {
                cf.crTextColor = COLOR_KEYWORD;
                SendMessage(g_hMultiEdit, EM_SETSEL, (WPARAM)start, (LPARAM)i);
                SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            } else if (isBuiltinFunc(word)) {
                cf.crTextColor = COLOR_FUNC;
                SendMessage(g_hMultiEdit, EM_SETSEL, (WPARAM)start, (LPARAM)i);
                SendMessage(g_hMultiEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            }
            continue;
        }

        // 其他字符
        i++;
    }

    // 恢复光标位置
    SendMessage(g_hMultiEdit, EM_SETSEL, selStart, selEnd);
    // 恢复滚动位置
    int curVisible = (int)SendMessage(g_hMultiEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (curVisible != firstVisible) {
        SendMessage(g_hMultiEdit, EM_LINESCROLL, 0, firstVisible - curVisible);
    }
    // 显示选区
    SendMessage(g_hMultiEdit, EM_HIDESELECTION, FALSE, 0);
    // 启用重绘并刷新
    SendMessage(g_hMultiEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hMultiEdit, NULL, FALSE);

    inHighlight = false;
}

void handleHelp() {
    static bool registered = false;
    if (!registered) {
        WNDCLASS w = {0};
        w.lpfnWndProc = HelpWndProc;
        w.hInstance = GetModuleHandle(NULL);
        w.hCursor = LoadCursor(NULL, IDC_ARROW);
        w.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        w.lpszClassName = "SimpleIDEHelp";
        RegisterClass(&w);
        registered = true;
    }
    static HWND hHelp = NULL;
    if (hHelp && IsWindow(hHelp)) {
        ShowWindow(hHelp, SW_SHOW);
        SetForegroundWindow(hHelp);
        return;
    }
    hHelp = CreateWindow("SimpleIDEHelp", "SimpleIDE Help - 使用说明",
        WS_OVERLAPPEDWINDOW, 150, 100, 800, 600,
        g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    if (hHelp) {
        ShowWindow(hHelp, SW_SHOW);
        UpdateWindow(hHelp);
    }
}

// 处理发送输入 (Send 按钮 / 回车)
void handleSendInput() {
    if (!g_waitingInput) return;
    int len = GetWindowTextLength(g_hInput);
    if (len < 0) len = 0;
    std::vector<char> buf(len + 1, 0);
    GetWindowText(g_hInput, buf.data(), len + 1);
    std::string input(buf.data());
    if (input.empty()) return;  // 空输入不允许结束等待

    // InputInt 需要验证整数
    if (g_inputType == 1) {
        bool valid = true;
        size_t chk = 0;
        if (chk < input.size() && (input[chk] == '+' || input[chk] == '-')) chk++;
        if (chk >= input.size()) valid = false;
        for (; chk < input.size(); chk++) {
            if (input[chk] < '0' || input[chk] > '9') { valid = false; break; }
        }
        if (!valid) {
            reportError(ERR_INVALID_NUM, "invalid integer input: " + input);
            input = "0";  // 用0替代
        }
    }
    // 输出提示+输入到日志
    if (!g_inputPrompt.empty()) Log(g_inputPrompt + input);
    else Log(input);
    // 语句模式: 存到变量
    if (!g_inputVarName.empty()) setVar(g_inputVarName, input);
    // 表达式模式: 存到 g_inputResult
    g_inputResult = input;

    g_waitingInput = false;
    g_inputPrompt.clear();
    SetWindowText(g_hInput, "");

    // 暂时禁用输入框（恢复执行期间不接收输入）
    setInputMode(false);

    // 恢复执行
    flushPendingFrames();
    resumeExecution();

    if (g_waitingInput) {
        // 又遇到 Input，继续等待
        setInputMode(true);
    } else {
        Log("=== Done ===");
        setInputMode(false);
    }
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR, int nCS) {
    WNDCLASS w = {0};
    w.lpfnWndProc = WndProc;
    w.hInstance = hI;
    w.hCursor = LoadCursor(NULL, IDC_ARROW);
    w.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    w.lpszClassName = "SimpleIDE";
    if (!RegisterClass(&w)) {
        MessageBox(NULL, "RegisterClass failed", "Error", MB_OK);
        return 1;
    }
    HWND h = CreateWindow("SimpleIDE", "SimpleIDE v2.0", WS_OVERLAPPEDWINDOW,
        100, 100, 700, 500, NULL, NULL, hI, NULL);
    if (!h) {
        MessageBox(NULL, "CreateWindow failed", "Error", MB_OK);
        return 1;
    }
    g_hMainWnd = h;
    ShowWindow(h, nCS);
    UpdateWindow(h);
    MSG m;
    while (GetMessage(&m, NULL, 0, 0) > 0) {
        // 快捷键处理
        if (m.message == WM_KEYDOWN) {
            HWND focus = GetFocus();
            // 在输出窗口输入框中按回车 = Send
            if (m.wParam == VK_RETURN && focus == g_hInput) {
                handleSendInput();
                continue;
            }
            // Ctrl 组合键 (仅在主窗口编辑区聚焦时)
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && focus == g_hMultiEdit) {
                if (m.wParam == 'O') { handleOpen(); continue; }
                if (m.wParam == 'S') { handleSave(); continue; }
                if (m.wParam == 'L') { handleClear(); continue; }
                if (m.wParam == 'F') { chooseFont(); continue; }
                if (m.wParam == 'Q') { PostQuitMessage(0); continue; }
                if (m.wParam == '0') { g_fontSize = 15; strcpy(g_fontName, "Consolas"); updateEditorFont(); continue; }
                if (m.wParam == VK_OEM_PLUS || m.wParam == '=') { g_fontSize++; updateEditorFont(); continue; }
                if (m.wParam == VK_OEM_MINUS || m.wParam == '-') { if (g_fontSize > 6) { g_fontSize--; updateEditorFont(); } continue; }
            }
            // F1-F6 功能键
            if (m.wParam == VK_F1) { handleHelp(); continue; }
            if (m.wParam == VK_F5) { handleRun(); continue; }
            if (m.wParam == VK_F6) { handleStep(); continue; }
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
