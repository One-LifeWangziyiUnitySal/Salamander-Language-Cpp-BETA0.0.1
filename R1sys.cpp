#include <windows.h>
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
HWND g_hMainWnd, g_hMultiEdit, g_hRunBtn, g_hStepBtn, g_hClearBtn, g_hSaveBtn, g_hOpenBtn;
// 输出窗口控件 (独立弹出窗口)
HWND g_hOutputWnd, g_hOutput, g_hInput, g_hSendBtn;

bool g_outputWndCreated = false;
std::vector<std::string> g_multiLines;
int g_currentLine = 0;
bool g_waitingInput = false;
std::string g_inputPrompt, g_inputVarName;
int g_inputType = 0; // 0 = string, 1 = int
bool g_returning = false;

bool g_breakLoop = false;

// ---- 列表系统 ----
// 列表存储: 变量名 -> 元素列表
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
// 判断变量名是否为列表引用语法: listName[i]
bool isListIndexExpr(const std::string& s, std::string& listName, std::string& idxExpr);

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
// ---- ±?á?1üàí ----

std::map<std::string, std::string> g_vars;
void setVar(const std::string& n, const std::string& v) { g_vars[n] = v; }
std::string getVar(const std::string& n) {
    auto it = g_vars.find(n); return it != g_vars.end() ? it->second : "";
}
bool hasVar(const std::string& n) { return g_vars.find(n) != g_vars.end(); }
bool eraseVar(const std::string& n) { return g_vars.erase(n) > 0; }

// ---- è￥??°× ----
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

// ---- isIdentifier: ?§3?ê××?????????￡?oóD??éo?êy×? ----
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
    size_t rb = s.find_last_of(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return false;
    if (rb != s.size() - 1) return false;
    listName = s.substr(0, lb);
    idxExpr = s.substr(lb + 1, rb - lb - 1);
    trim(listName);
    trim(idxExpr);
    return isIdentifier(listName);
}

// ê?·?ê?′?êy×?￡¨°üà¨D?êy?￠?oo??￠???§??êy·¨￡?
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

// ---- oˉêy?¨ò? ----
struct Function { std::string name; std::vector<std::string> params; std::vector<std::string> body; };
std::map<std::string, Function> g_funcs;

// ---- 前向声明 ----
void runCode(const std::string& input);
void runLines(const std::vector<std::string>& lines);
std::string evalExpr(const std::string& expr);
std::string callFunc(const std::string& name, const std::vector<std::string>& args);
bool builtinCall(const std::string& name, const std::vector<std::string>& args, std::string& result);

// ---- calc: íêè???D′￡??§3?à¨o??￠ò??a?oo??￠???§??êy·¨ ----
// ê1ó???ê?????￡?×?è·′|àí????·?ó??è??oíà¨o?

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

// ×ó±í′?ê??Dê?·?o?×?·?′?áD￡¨±ü?a calc °?×?·?′?×a3é 0 ?ì3é×?·?′?±è???ó?D￡?
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

    // è?o?×?·?′?áD￡??μ?÷2?ê?′?êy?μ±í′?ê?￡?·μ?? 0
    // ￡¨×?·?′?±è??ó|?ú cond ?Dμ￥?à′|àí￡?
    if (hasStringLiteral(s)) return 0;

    // è￥3yía2?à¨o?
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

    // ′?êy×?
    if (isPureNumber(s)) return atof(s.c_str());

    // ±?á?
    if (hasVar(s)) return atof(getVar(s).c_str());

    // 2é?ò + / - ￡¨×?μíó??è??￡?
    size_t pos;
    if (findTopLevelOp(s, '+', '-', pos)) {
        char op = s[pos];
        double l = calc(s.substr(0, pos));
        double r = calc(s.substr(pos + 1));
        return op == '+' ? l + r : l - r;
    }

    // 2é?ò * / ￡¨?D??ó??è??￡?
    if (findTopLevelMulDiv(s, '*', pos)) {
        return calc(s.substr(0, pos)) * calc(s.substr(pos + 1));
    }
    if (findTopLevelMulDiv(s, '/', pos)) {
        double r = calc(s.substr(pos + 1));
        if (r == 0) return 0;
        return calc(s.substr(0, pos)) / r;
    }

    // ò??a?oo?
    if (s.size() >= 2 && s[0] == '-') {
        return -calc(s.substr(1));
    }
    if (s.size() >= 2 && s[0] == '+') {
        return calc(s.substr(1));
    }

    // ±?á?
    if (hasVar(s)) return atof(getVar(s).c_str());
    return atof(s.c_str());
}

// ---- cond: ??D′￡??§3?×?·?′?±è???￠?yè·ì?1yòyo? ----
// ·μ???μ￡o0 = false￡?1 = true￡?-1 = ?T±è??·?o?
static int tryCompare(const std::string& s, std::string& lhs, std::string& rhs, std::string& op) {
    std::string ops[] = {"==","!=",">=","<=",">","<"};
    for (int i = 0; i < 6; i++) {
        const std::string& o = ops[i];
        size_t p = std::string::npos;
        // é¨?èê±ì?1yòyo??úμ??úèY
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

            // ′|àí >= <= != == Dèòaì?1yμ￥×?·?μ? > <
            if (o == ">" || o == "<") {
                // ±ü?a°? >= μ? > μ±3é > ????·?
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

// ?D??±í′?ê?ê?·?ê?×?·?′?ààDí
static bool isStringExpr(const std::string& s) {
    std::string t = s;
    trim(t);
    if (t.empty()) return false;
    if (t[0] == '"' || t[0] == '\'') return true;
    // °üo?×?·?′?á??ó
    if (t.find('"') != std::string::npos || t.find('\'') != std::string::npos) return true;
    // ò??a×?·?′?±?á?
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
        // è?1?á?±???ê?×?·?′?ààDí￡?×÷×?·?′?±è??
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

// ---- getArg: ??D′￡??yè·′|àíòyo?×?·?′??￠????·??￠×ó2?êy ----
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

// ---- callFunc: íêè???D′￡?íêé?×÷ó?óò??à? ----
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

// ---- evalExpr: íêè???D′ ----
// ?§3?￡o×?·?′?×???á??￠±?á????￠êy?μ?????￠×?·?′??′?ó?￠oˉêyμ÷ó??￠à¨o?

// ?a??òyo?×?·?′?￡¨è￥3yòyo??￠′|àí×aò?￡?
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

// ?°?ò×?ía2??′?￥??μ? + ￡¨×?·?′??′?ó￡?￡?ì?1yòyo??úμ? +
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
            // ·?ò??ao?￡o?°??·?????·?/×óà¨o?
            if (i == 0) continue;
            char prev = s[i-1];
            if (prev == '(' || prev == '+' || prev == '-' || prev == '*' || prev == '/') continue;
            outPos = (size_t)i;
            return true;
        }
    }
    return false;
}

// ??±í′?ê?μ??μ×a?a×?·?′?D?ê?￡¨êy×?×?è???ê??ˉ￡?
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

    // òyo?×?·?′?×???á?
    if (isQuoted(s)) return parseStringLiteral(s);

    // ′?±?á???￡¨?T????·??￠?Tà¨o??￠?Tòyo?￡?
    if (isIdentifier(s) && hasVar(s)) return getVar(s);

    // ′?êy×?
    if (isPureNumber(s)) return s;

    // ê×?èì???oˉêyμ÷ó?￡¨μY1éμ?￡?
    // ?ú?ùóD????′|àí???°￡??è??oˉêyμ÷ó?ì????a??·μ???μ
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
                    if (ap < argsStr.size() && argsStr[ap] == ',') ap++;
                    else break;
                }
                std::string fr = callFunc(func.first, cargs);
                s.replace(pos, rp - pos + 1, fr);
                replaced = true;
                break; // ??D??aê?ì????-?·
            }
            if (replaced) break;
        }
        // 也尝试内置函数 (str*/list*)
        if (!replaced) {
            // 检查所有内置函数名
            static const char* builtins[] = {
                "strLen","strUpper","strLower","strSub","strCat","strRep","strFind","strRFind",
                "strReplace","strTrim","strSplit","strJoin","strChar","strAscii","strChr",
                "strReverse","strStartsWith","strEndsWith","strContains","strCount",
                "strLeft","strRight","strToInt","strToFloat","numToStr",
                "listNew","listLen","listGet","listSet","listAppend","listInsert","listRemove",
                "listPop","listClear","listSort","listReverse","listFind","listContains",
                "listCopy","listSum","listJoin","listPrint", NULL
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
                    while (ap <= argsStr.size()) {
                        std::string a = getArg(argsStr, ap);
                        cargs.push_back(evalExpr(a));
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
    }

    // ì???oóè?1?±?3é′?×?·?′??òêy×?￡??±?ó·μ??
    trim(s);
    if (s.empty()) return "";
    if (isQuoted(s)) return parseStringLiteral(s);
    if (isPureNumber(s)) return s;
    // 如果是单个标识符且是已定义变量,返回变量值
    if (isIdentifier(s) && hasVar(s)) return getVar(s);
    // 如果不含运算符(+,-,*,/,=)且不是引号/数字,当作字符串字面量返回(可能是内置函数返回值)
    if (s.find_first_of("+-*/=") == std::string::npos) return s;

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

    // êy?μ????
    double val = calc(s);
    return numToStr(val);
}

// ---- resolveFuncArg: 2?êy?a?? ----
std::string resolveFuncArg(const std::string& raw) {
    std::string s = raw;
    trim(s);
    if (s.empty()) return "";
    if (isQuoted(s)) return parseStringLiteral(s);
    if (isIdentifier(s) && !hasVar(s)) return s; // ?′?¨ò?μ?±êê?·?×÷?a×?·?′?
    return evalExpr(s);
}

// ---- runCode: ??D′ ----
// ′|àíμ￥DD??á?￡oreturn?￠?3?μ?￠?′o??3?μ?￠oˉêyμ÷ó??￠?μ?÷ó???

// ?D?? s ê?·?ò?í???μ??3????á??aí·
static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// ?D??ê?·??a?òμ￥?3?μ￡¨·??′o??3?μ?￠·?±è??????·?￡?
// ·μ?? -1 ±íê?·?￡?·??ò·μ?? = μ?????
static size_t findAssignEq(const std::string& s) {
    bool inS = false, inD = false;
    int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (inS) { if (c == '\'') inS = false; continue; }
        if (inD) { if (c == '"') inD = false; continue; }
        if (c == '\'') { inS = true; continue; }
        if (c == '"') { inD = true; continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; }
        else if (depth == 0 && c == '=') {
            // ??3y == != >= <= += -= *= /= ?° <=
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

    return false;
}

void runCode(const std::string& input) {
    std::string s = input;
    trim(s);
    if (s.empty() || s[0] == '#') return;

    // return ó???
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

    // ?ì2a?3?μ￡¨?úoˉêyμ÷ó????°￡?
    size_t eqPos = findAssignEq(s);
    size_t parenPos = s.find('(');

    if (eqPos != std::string::npos && eqPos > 0 &&
        (parenPos == std::string::npos || eqPos < parenPos)) {
        std::string vn = s.substr(0, eqPos);
        std::string vl = s.substr(eqPos + 1);
        trim(vn); trim(vl);
        if (vn.empty() || vl.empty()) return;
        if (!isIdentifier(vn)) {
            reportError(ERR_INVALID_NAME, "invalid variable name: " + vn);
            return;
        }
        setVar(vn, resolveFuncArg(vl));
        return;
    }

    // oˉêyμ÷ó?ó??μ?÷ó???￡¨ò? ( ?á?2￡?
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

        // ó??§oˉêy
        if (g_funcs.find(fn) != g_funcs.end()) {
            std::vector<std::string> args;
            for (size_t i = 0; i < rawArgs.size(); i++)
                args.push_back(resolveFuncArg(rawArgs[i]));
            callFunc(fn, args);
            return;
        }

        // 内置返回值函数 (字符串/列表函数)
        {
            std::vector<std::string> args;
            for (size_t i = 0; i < rawArgs.size(); i++)
                args.push_back(resolveFuncArg(rawArgs[i]));
            std::string result;
            if (builtinCall(fn, args, result)) {
                // 内置函数已处理, 如果是赋值语句的右侧则已被处理
                // 这里函数作为语句调用, 返回值被丢弃(但日志已输出)
                return;
            }
        }

        // Input / InputInt ￡oμúò?2?êy?aìáê?￡?μú?t2?êy?a±?á???
        if (fn == "Input" || fn == "InputInt") {
            g_waitingInput = true;
            g_inputType = (fn == "InputInt") ? 1 : 0;
            std::string prompt, varName;
            if (rawArgs.size() > 0) prompt = resolveFuncArg(rawArgs[0]);
            // 变量名必须用原始标识符，不能用 resolveFuncArg（否则已定义变量会返回值而非名称）
            if (rawArgs.size() > 1) { varName = rawArgs[1]; trim(varName); }
            g_inputPrompt = prompt;
            g_inputVarName = varName;
            if (!prompt.empty()) Log(prompt);
            return;
        }

        // box(x, value) / boxS(prompt, x, value)
        if (fn == "box" && rawArgs.size() >= 2) {
            std::string varName = rawArgs[0];
            std::string val = rawArgs[1];
            trim(val);
            // ?ì2a val ê?·?ê? Input()/InputInt() D?ê?
            if (startsWith(val, "Input(") && val.back() == ')') {
                g_waitingInput = true;
                g_inputType = 0;
                g_inputVarName = varName;
                // ìáè? Input μ?2?êy×÷?aìáê?
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
            return;
        }

        if (fn == "PrintLog") {
            // ?§3??à2?êy￡o·?±e?ó?μoóó?????á??ó
            std::string out;
            for (size_t i = 0; i < rawArgs.size(); i++) {
                if (i > 0) out += " ";
                out += evalExpr(rawArgs[i]);
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

        reportError(ERR_UNDEFINED_FUNC, "unknown function: " + fn);
        return;
    }

    // ?′o??3?μ += -= *= /=
    std::string ops[] = {"+=","-=","*=","/="};
    for (int j = 0; j < 4; j++) {
        size_t op = s.find(ops[j]);
        if (op != std::string::npos && op > 0) {
            std::string vn = s.substr(0, op);
            std::string rv = s.substr(op + 2);
            trim(vn); trim(rv);
            if (vn.empty() || rv.empty()) return;
            if (!hasVar(vn)) {
                // ?′?¨ò?±?á?￡?3?ê??ˉ?a 0 ?ù????
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

// ---- runLines: ??D′￡??yè·′|àí?ì?·ì?3?ó? return ----
void runLines(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); i++) {
        // Input 中断：保存剩余行为 SEQ 帧，然后返回
        if (g_waitingInput) {
            ResumeFrame frame;
            frame.type = 0;  // SEQ
            frame.lines = lines;
            frame.nextIndex = i;
            g_pendingFrames.push_back(frame);
            return;
        }
        if (g_breakLoop) break;

        std::string s = lines[i];
        trim(s);
        // 记录当前行号 (1-based)
        g_currentLineNo = (int)i + 1;
        if (s.empty() || s[0] == '#') continue;

        // Func ?¨ò?
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
                // ??óDμ±???yê?±í′?ê?2?μ±×÷±í′?ê?￡?·??òê? step
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
                    // Input ?D??￡o±￡′? For ?-?·?ìD???
                    ResumeFrame frame;
                    frame.type = 1;  // FOR
                    frame.forVar = var;
                    frame.isExprStep = isExprStep;
                    frame.forExprStepArg = isExprStep ? fa[3] : "";
                    frame.forEnd = end;
                    frame.forStep = step;
                    frame.forBody = fb;
                    // ??????ò???μü′ú?μ
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
                    // Input ?D??￡o±￡′? while ?-?·?ìD???
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

        // break ó???
        if (s == "break") {
            g_breakLoop = true;
            return;
        }

        // ??í¨??á?
        runCode(s);
        if (g_returning) return;
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
            // 代码编辑区 (多行，占满大部分)
            g_hMultiEdit = CreateWindow("EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_BORDER,
                10, 10, 660, 380, hWnd, (HMENU)1, GetModuleHandle(NULL), NULL);
            // 运行按钮
            g_hRunBtn = CreateWindow("BUTTON", "Run", WS_CHILD | WS_VISIBLE,
                10, 400, 70, 30, hWnd, (HMENU)3, GetModuleHandle(NULL), NULL);
            // 逐行按钮
            g_hStepBtn = CreateWindow("BUTTON", "Step", WS_CHILD | WS_VISIBLE,
                85, 400, 70, 30, hWnd, (HMENU)6, GetModuleHandle(NULL), NULL);
            // 清除按钮
            g_hClearBtn = CreateWindow("BUTTON", "Clear", WS_CHILD | WS_VISIBLE,
                160, 400, 70, 30, hWnd, (HMENU)4, GetModuleHandle(NULL), NULL);
            // 保存按钮
            g_hSaveBtn = CreateWindow("BUTTON", "Save", WS_CHILD | WS_VISIBLE,
                245, 400, 70, 30, hWnd, (HMENU)8, GetModuleHandle(NULL), NULL);
            // 打开按钮
            g_hOpenBtn = CreateWindow("BUTTON", "Open", WS_CHILD | WS_VISIBLE,
                320, 400, 70, 30, hWnd, (HMENU)9, GetModuleHandle(NULL), NULL);
            // 默认提示
            SetWindowText(g_hMultiEdit,
                "# SimpleIDE v2.0\r\n"
                "# Edit code here, click Run.\r\n"
                "# Save/Open uses .sal files.\r\n"
                "# String funcs: strLen, strUpper, strSub, strFind, strReplace...\r\n"
                "# List funcs: listNew, listGet, listAppend, listSort, listSum...\r\n"
                "\r\n"
                "PrintLog(\"Hello, World!\")\r\n"
                "listNew(nums, 3, 1, 4, 1, 5, 9, 2, 6)\r\n"
                "listSort(nums)\r\n"
                "listPrint(nums)\r\n"
                "PrintLog(\"Sum =\", listSum(nums))\r\n"
                "PrintLog(\"Upper:\", strUpper(\"hello\"))\r\n");
            break;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hWnd, &rc);
            int w = rc.right, h = rc.bottom;
            int btnH = 30, gap = 10;
            // 编辑区
            SetWindowPos(g_hMultiEdit, NULL, gap, gap,
                w - 2*gap > 1 ? w - 2*gap : 1,
                h - btnH - 3*gap > 1 ? h - btnH - 3*gap : 1,
                SWP_NOZORDER);
            // 按钮行
            SetWindowPos(g_hRunBtn, NULL, gap, h - btnH - gap, 70, btnH, SWP_NOZORDER);
            SetWindowPos(g_hStepBtn, NULL, gap + 75, h - btnH - gap, 70, btnH, SWP_NOZORDER);
            SetWindowPos(g_hClearBtn, NULL, gap + 150, h - btnH - gap, 70, btnH, SWP_NOZORDER);
            SetWindowPos(g_hSaveBtn, NULL, gap + 225, h - btnH - gap, 70, btnH, SWP_NOZORDER);
            SetWindowPos(g_hOpenBtn, NULL, gap + 300, h - btnH - gap, 70, btnH, SWP_NOZORDER);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 3) handleRun();
            else if (LOWORD(wParam) == 6) handleStep();
            else if (LOWORD(wParam) == 4) handleClear();
            else if (LOWORD(wParam) == 8) handleSave();
            else if (LOWORD(wParam) == 9) handleOpen();
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
    int len = GetWindowTextLength(g_hMultiEdit);
    if (len <= 0) return ls;
    if (len > 1024 * 1024) len = 1024 * 1024;
    std::vector<char> buf(len + 1, 0);
    GetWindowText(g_hMultiEdit, buf.data(), len + 1);
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
    int len = GetWindowTextLength(g_hMultiEdit);
    if (len < 0) len = 0;
    std::vector<char> buf(len + 1, 0);
    GetWindowText(g_hMultiEdit, buf.data(), len + 1);
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
    ensureOutputWindow();
    Log("Loaded: " + path);
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
            Log("[Error] invalid integer: " + input);
            if (!g_inputVarName.empty()) setVar(g_inputVarName, "0");
        } else {
            Log(g_inputPrompt + input);
            if (!g_inputVarName.empty()) setVar(g_inputVarName, input);
        }
    } else {
        Log(g_inputPrompt + input);
        if (!g_inputVarName.empty()) setVar(g_inputVarName, input);
    }
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
        // 让回车键触发按钮
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
            HWND focus = GetFocus();
            if (focus == g_hInput) {
                // 在输入框按回车 = 点 Send
                handleSendInput();
                continue;
            }
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
