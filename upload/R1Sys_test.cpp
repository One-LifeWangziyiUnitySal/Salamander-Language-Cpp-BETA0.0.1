// R1Sys_test.cpp — 完整测试套件
// 用法: g++ -std=c++11 -O2 -o test R1Sys_test.cpp && ./test
// 覆盖每个函数、每个分支、每个边界条件

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
using namespace std;

// ============================================================
// 被测代码 (从 R1Sys.cpp 抽取,去掉 GUI/Windows 依赖)
// ============================================================

// ---- 错误系统 (前置) ----
vector<string> g_logLines;  // 捕获 Log 输出用于测试
struct ErrorInfo { int code; string message; int line; string context; };
vector<ErrorInfo> g_errors;
int g_currentLineNo = 0;
enum ErrorCode {
    ERR_NONE=0, ERR_SYNTAX=1001, ERR_UNDEFINED_VAR=1002, ERR_UNDEFINED_FUNC=1003,
    ERR_INVALID_ARG=1004, ERR_ARG_COUNT=1005, ERR_INVALID_NUM=1006, ERR_INVALID_STR=1007,
    ERR_INVALID_LIST=1008, ERR_DIV_ZERO=1009, ERR_OUT_OF_RANGE=1010, ERR_INVALID_TYPE=1011,
    ERR_INVALID_NAME=1012, ERR_FILE_IO=1013, ERR_STACK_OVERFLOW=1014, ERR_INTERNAL=1099
};
void clearErrors() { g_errors.clear(); }
extern bool g_inTryBlock;
extern bool g_breakLoop;
void reportError(int code, const string& msg, const string& context = "") {
    ErrorInfo e; e.code=code; e.message=msg; e.line=g_currentLineNo; e.context=context;
    g_errors.push_back(e);
    char prefix[128]; sprintf(prefix, "[Error E%d L%d] ", code, g_currentLineNo);
    g_logLines.push_back(prefix + msg + (context.empty() ? "" : " (" + context + ")"));
    if (g_inTryBlock) g_breakLoop = true;
}
bool hasErrors() { return !g_errors.empty(); }

// ---- 常量系统 (CboxS) ----
map<string, string> g_consts;
bool isConstVar(const string& name) { return g_consts.find(name)!=g_consts.end(); }
string getConst(const string& name) { auto it=g_consts.find(name); return it!=g_consts.end()?it->second:""; }
void setConst(const string& name, const string& v) { g_consts[name]=v; }

// ---- 变量管理 (含常量集成) ----
map<string, string> g_vars;
void setVar(const string& n, const string& v) {
    if (isConstVar(n)) { reportError(ERR_INVALID_ARG, "cannot modify constant: " + n); return; }
    g_vars[n] = v;
}
string getVar(const string& n) {
    if (isConstVar(n)) return getConst(n);
    auto it=g_vars.find(n); return it!=g_vars.end()?it->second:"";
}
bool hasVar(const string& n) { return g_vars.find(n)!=g_vars.end() || isConstVar(n); }
bool eraseVar(const string& n) {
    if (isConstVar(n)) { reportError(ERR_INVALID_ARG, "cannot delete constant: " + n); return false; }
    return g_vars.erase(n)>0;
}

bool g_returning=false, g_breakLoop=false, g_waitingInput=false;
int g_inputType=0;
string g_inputPrompt, g_inputVarName;
// 表达式模式 Input
bool g_inputPending = false;
string g_inputResult;
bool g_inTryBlock = false;
void printAllErrors() {
    if (g_errors.empty()) return;
    g_logLines.push_back("=== Error Summary (" + to_string(g_errors.size()) + " errors) ===");
    for (auto& e : g_errors) {
        char buf[256]; sprintf(buf, "  E%d L%d: ", e.code, e.line);
        g_logLines.push_back(buf + e.message + (e.context.empty() ? "" : " | " + e.context));
    }
}

// ---- 列表系统 ----
map<string, vector<string> > g_lists;
bool isListVar(const string& name) { return g_lists.find(name)!=g_lists.end(); }
vector<string>& getList(const string& name) {
    static vector<string> empty;
    auto it=g_lists.find(name);
    return it!=g_lists.end()?it->second:empty;
}
void setList(const string& name, const vector<string>& vals) { g_lists[name]=vals; }
bool eraseList(const string& name) { return g_lists.erase(name)>0; }
bool isListIndexExpr(const string& s, string& listName, string& idxExpr);

// ---- 字典系统 (Dict) ----
map<string, map<string, string> > g_dicts;
bool isDictVar(const string& name) { return g_dicts.find(name)!=g_dicts.end(); }
map<string, string>& getDict(const string& name) {
    static map<string, string> empty;
    auto it=g_dicts.find(name);
    return it!=g_dicts.end()?it->second:empty;
}
void setDict(const string& name, const map<string, string>& vals) { g_dicts[name]=vals; }
bool eraseDict(const string& name) { return g_dicts.erase(name)>0; }

// ---- 库导入系统 (GetPack) ----
void runLines(const vector<string>& lines);
vector<string> g_loadedPacks;
bool isPackLoaded(const string& name) { for(auto&p:g_loadedPacks)if(p==name)return true; return false; }
// 测试用: 库代码直接注入 (不读文件)
map<string, vector<string> > g_packSources;  // 库名 -> 代码行
void setPackSource(const string& name, const vector<string>& lines) { g_packSources[name] = lines; }
bool loadPack(const string& name) {
    if (isPackLoaded(name)) return true;
    auto it = g_packSources.find(name);
    if (it == g_packSources.end()) {
        reportError(ERR_FILE_IO, "cannot load pack: " + name);
        return false;
    }
    g_loadedPacks.push_back(name);
    runLines(it->second);
    g_logLines.push_back("[Pack] loaded: " + name);
    return true;
}

// ---- C++ 库扩展系统 (GetCPack) ----
typedef const char* (*NativeFunc)(const char** args, int argc);
struct NativeFuncEntry { string name; NativeFunc func; };
vector<NativeFuncEntry> g_nativeFuncs;
vector<string> g_loadedCPacks;

void registerNativeFunc(const char* name, NativeFunc func) {
    g_nativeFuncs.push_back({string(name), func});
}
bool callNativeFunc(const string& name, const vector<string>& args, string& result) {
    for (auto& e : g_nativeFuncs) {
        if (e.name == name && e.func) {
            vector<const char*> cargs;
            for (auto& a : args) cargs.push_back(a.c_str());
            const char* r = e.func(cargs.data(), (int)cargs.size());
            result = r ? string(r) : "";
            return true;
        }
    }
    return false;
}
bool loadCPack(const string& name) {
    for (auto& p : g_loadedCPacks) if (p == name) return true;
    g_loadedCPacks.push_back(name);
    g_logLines.push_back("[CPack] loaded: " + name);
    return true;
}

// ---- 恢复机制 ----
struct ResumeFrame {
    int type;  // 0=SEQ, 1=FOR, 2=WHILE
    vector<string> lines;
    size_t nextIndex;
    string forVar; int forVal; int forEnd; int forStep;
    bool isExprStep; string forExprStepArg; vector<string> forBody;
    string whileCond; vector<string> whileBody;
};
vector<ResumeFrame> g_pendingFrames;
deque<ResumeFrame> g_resumeStack;

struct Function { string name; vector<string> params; vector<string> body; };
map<string, Function> g_funcs;

static inline void trimLeft(string& s){size_t i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\t'))i++;if(i>0)s.erase(0,i);}
static inline void trimRight(string& s){size_t i=s.size();while(i>0&&(s[i-1]==' '||s[i-1]=='\t'))i--;if(i<s.size())s.erase(i);}
static inline void trim(string& s){trimLeft(s);trimRight(s);}

bool isIdentifier(const string& s){
    if(s.empty())return false;
    unsigned char c0=(unsigned char)s[0];
    if(!isalpha(c0)&&s[0]!='_')return false;
    for(size_t i=1;i<s.size();i++){unsigned char c=(unsigned char)s[i];if(!isalnum(c)&&s[i]!='_')return false;}
    return true;
}
bool isListIndexExpr(const string& s, string& listName, string& idxExpr){
    size_t lb=s.find('[');
    if(lb==string::npos)return false;
    int depth=1;size_t rb=string::npos;
    for(size_t i=lb+1;i<s.size();i++){
        if(s[i]=='[')depth++;
        else if(s[i]==']'){depth--;if(depth==0){rb=i;break;}}
    }
    if(rb==string::npos)return false;
    if(rb!=s.size()-1)return false;
    listName=s.substr(0,lb);idxExpr=s.substr(lb+1,rb-lb-1);
    trim(listName);trim(idxExpr);
    return isIdentifier(listName);
}
bool isPureNumber(const string& s){
    if(s.empty())return false;
    bool hasDigit=false,hasDot=false,hasE=false;size_t start=0;
    if(s[0]=='+'||s[0]=='-')start=1;
    for(size_t i=start;i<s.size();i++){char c=s[i];
        if(c>='0'&&c<='9')hasDigit=true;
        else if(c=='.'&&!hasDot&&!hasE)hasDot=true;
        else if((c=='e'||c=='E')&&hasDigit&&!hasE){hasE=true;if(i+1<s.size()&&(s[i+1]=='+'||s[i+1]=='-'))i++;}
        else return false;
    }
    // 指数后必须有数字
    if(hasE){
        size_t ePos=string::npos;
        for(size_t i=0;i<s.size();i++){if(s[i]=='e'||s[i]=='E'){ePos=i;break;}}
        if(ePos!=string::npos){
            size_t j=ePos+1;
            if(j<s.size()&&(s[j]=='+'||s[j]=='-'))j++;
            if(j>=s.size())return false;
            for(;j<s.size();j++){if(s[j]<'0'||s[j]>'9')return false;}
        }
    }
    return hasDigit;
}

void runCode(const string& input);
void runLines(const vector<string>& lines);
string evalExpr(const string& expr);
string callFunc(const string& name, const vector<string>& args);
bool builtinCall(const string& name, const vector<string>& args, string& result);
static bool startsWith(const string& s, const string& p);
void Log(const string& s) { g_logLines.push_back(s); }

static bool findTopLevelOp(const string& s,char op1,char op2,size_t& outPos){
    int depth=0;bool inS=false,inD=false;
    for(int i=(int)s.size()-1;i>=0;i--){char c=s[i];
        if(inS){if(c=='\'')inS=false;continue;}
        if(inD){if(c=='"')inD=false;continue;}
        if(c=='\''){inS=true;continue;}
        if(c=='"'){inD=true;continue;}
        if(c==')')depth++;else if(c=='('){depth--;continue;}
        if(depth!=0)continue;
        if(c==op1||c==op2){
            if(c=='-'||c=='+'){if(i==0)continue;int p=i-1;while(p>=0&&(s[p]==' '||s[p]=='\t'))p--;if(p<0)continue;char prev=s[p];if(prev=='('||prev=='+'||prev=='-'||prev=='*'||prev=='/'||prev=='^')continue;}
            outPos=(size_t)i;return true;
        }
    }
    return false;
}
static bool findTopLevelMulDiv(const string& s,char op,size_t& outPos){
    int depth=0;bool inS=false,inD=false;
    for(int i=(int)s.size()-1;i>=0;i--){char c=s[i];
        if(inS){if(c=='\'')inS=false;continue;}
        if(inD){if(c=='"')inD=false;continue;}
        if(c=='\''){inS=true;continue;}
        if(c=='"'){inD=true;continue;}
        if(c==')')depth++;else if(c=='('){depth--;continue;}
        if(depth!=0)continue;
        if(c==op){outPos=(size_t)i;return true;}
    }
    return false;
}
static bool hasStringLiteral(const string& s){
    for(size_t i=0;i<s.size();i++)if(s[i]=='"'||s[i]=='\'')return true;
    return false;
}
double calc(const string& e){
    string s=e;trim(s);if(s.empty())return 0;
    if(hasStringLiteral(s))return 0;
    while(s.size()>=2&&s[0]=='('&&s.back()==')'){
        int depth=0;bool isOuter=true;
        for(size_t i=0;i<s.size();i++){if(s[i]=='(')depth++;else if(s[i]==')'){depth--;if(depth==0&&i<s.size()-1){isOuter=false;break;}}}
        if(isOuter)s=s.substr(1,s.size()-2);else break;
    }
    if(s.empty())return 0;
    if(isPureNumber(s))return atof(s.c_str());
    if(hasVar(s))return atof(getVar(s).c_str());
    size_t pos;
    if(findTopLevelOp(s,'+','-',pos)){char op=s[pos];double l=calc(s.substr(0,pos));double r=calc(s.substr(pos+1));return op=='+'?l+r:l-r;}
    if(findTopLevelMulDiv(s,'*',pos))return calc(s.substr(0,pos))*calc(s.substr(pos+1));
    if(findTopLevelMulDiv(s,'/',pos)){double r=calc(s.substr(pos+1));if(r==0)return 0;return calc(s.substr(0,pos))/r;}
    if(s.size()>=2&&s[0]=='-')return -calc(s.substr(1));
    if(s.size()>=2&&s[0]=='+')return calc(s.substr(1));
    if(hasVar(s))return atof(getVar(s).c_str());
    return atof(s.c_str());
}

static int tryCompare(const string& s,string& lhs,string& rhs,string& op){
    string ops[]={"==","!=",">=","<=",">","<"};
    for(int i=0;i<6;i++){const string& o=ops[i];size_t p=string::npos;
        bool inS=false,inD=false;int depth=0;
        for(size_t k=0;k+o.size()<=s.size();k++){char c=s[k];
            if(inS){if(c=='\'')inS=false;continue;}
            if(inD){if(c=='"')inD=false;continue;}
            if(c=='\''){inS=true;continue;}
            if(c=='"'){inD=true;continue;}
            if(c=='(')depth++;else if(c==')'){if(depth>0)depth--;}
            if(o==">"||o=="<"){if(c==o[0]){if(k+1<s.size()&&s[k+1]=='='){k++;continue;}if(depth==0){p=k;break;}}}
            else{if(s.compare(k,o.size(),o)==0){if(depth==0){p=k;break;}}}
        }
        if(p!=string::npos){lhs=s.substr(0,p);rhs=s.substr(p+o.size());op=o;return 1;}
    }
    return 0;
}
static bool isStringExpr(const string& s){
    string t=s;trim(t);if(t.empty())return false;
    if(t[0]=='"'||t[0]=='\'')return true;
    if(t.find('"')!=string::npos||t.find('\'')!=string::npos)return true;
    if(hasVar(t)){const string& v=getVar(t);if(!isPureNumber(v))return true;}
    return false;
}
bool cond(const string& c){
    string s=c;trim(s);if(s.empty())return false;
    string lhs,rhs,op;
    if(tryCompare(s,lhs,rhs,op)){trim(lhs);trim(rhs);
        bool lIsStr=isStringExpr(lhs),rIsStr=isStringExpr(rhs);
        if(lIsStr||rIsStr){string lv=evalExpr(lhs),rv=evalExpr(rhs);
            if(op=="==")return lv==rv;if(op=="!=")return lv!=rv;
            if(op==">")return lv>rv;if(op=="<")return lv<rv;
            if(op==">=")return lv>=rv;if(op=="<=")return lv<=rv;
        }else{double lv=calc(lhs),rv=calc(rhs);
            if(op=="==")return lv==rv;if(op=="!=")return lv!=rv;
            if(op==">=")return lv>=rv;if(op=="<=")return lv<=rv;
            if(op==">")return lv>rv;if(op=="<")return lv<rv;
        }
    }
    if(s=="true")return true;if(s=="false")return false;
    if(hasVar(s)){const string& v=getVar(s);if(v=="true")return true;if(v=="false")return false;return atof(v.c_str())!=0;}
    if(isPureNumber(s))return atof(s.c_str())!=0;
    return calc(s)!=0;
}

string getArg(const string& s,size_t& pos){
    while(pos<s.size()&&(s[pos]==' '||s[pos]=='\t'))pos++;
    if(pos>=s.size())return"";
    string r;char c=s[pos];
    if(c=='\''||c=='"'){
        // 保留外层引号，让下游 evalExpr/resolveFuncArg 正确识别为字符串字面量
        char q=c;r+=c;pos++;
        while(pos<s.size()&&s[pos]!=q){
            if(s[pos]=='\\'&&pos+1<s.size()){r+=s[pos];r+=s[pos+1];pos+=2;}
            else{r+=s[pos];pos++;}
        }
        if(pos<s.size()){r+=s[pos];pos++;}
        return r;
    }
    int depth=0;bool inS=false,inD=false;
    while(pos<s.size()){char ch=s[pos];
        if(inS){r+=ch;pos++;if(ch=='\'')inS=false;continue;}
        if(inD){r+=ch;pos++;if(ch=='"')inD=false;continue;}
        if(ch=='\''||ch=='"'){r+=ch;pos++;if(ch=='\'')inS=true;else inD=true;continue;}
        if(ch=='('){depth++;r+=ch;pos++;continue;}
        if(ch==')'){if(depth==0)break;depth--;r+=ch;pos++;continue;}
        if(ch==','&&depth==0)break;
        r+=ch;pos++;
    }
    string rs=r;trim(rs);return rs;
}

string callFunc(const string& name,const vector<string>& args){
    auto it=g_funcs.find(name);if(it==g_funcs.end())return"";
    Function& f=it->second;
    map<string,string> savedGlobalVars=g_vars;
    for(size_t i=0;i<f.params.size();i++){setVar(f.params[i],i<args.size()?args[i]:"");}
    setVar("__ret__","");bool savedReturning=g_returning;g_returning=false;
    bool savedBreak=g_breakLoop;g_breakLoop=false;
    runLines(f.body);
    string ret=getVar("__ret__");
    g_returning=savedReturning;g_breakLoop=savedBreak;
    g_vars=savedGlobalVars;
    return ret;
}

static string parseStringLiteral(const string& s){
    string r;
    for(size_t i=1;i<s.size()-1;i++){
        if(s[i]=='\\'&&i+1<s.size()-1){char nx=s[i+1];
            if(nx=='n')r+='\n';else if(nx=='t')r+='\t';else if(nx=='r')r+='\r';
            else if(nx=='\\')r+='\\';else if(nx=='"')r+='"';else if(nx=='\'')r+='\'';else r+=nx;i++;
        }else r+=s[i];
    }
    return r;
}
static bool isQuoted(const string& s){
    if(s.size()<2)return false;
    if(s[0]!='"'&&s[0]!='\'')return false;
    char q=s[0];
    for(size_t i=1;i<s.size();i++){
        if(s[i]=='\\'&&i+1<s.size()){i++;continue;}
        if(s[i]==q)return i==s.size()-1;
    }
    return false;
}
static bool findConcatPlus(const string& s,size_t& outPos){
    int depth=0;bool inS=false,inD=false;
    for(int i=(int)s.size()-1;i>=0;i--){char c=s[i];
        if(inS){if(c=='\'')inS=false;continue;}
        if(inD){if(c=='"')inD=false;continue;}
        if(c=='\''){inS=true;continue;}
        if(c=='"'){inD=true;continue;}
        if(c==')')depth++;else if(c=='('){depth--;continue;}
        if(depth!=0)continue;
        if(c=='+'){if(i==0)continue;char prev=s[i-1];if(prev=='('||prev=='+'||prev=='-'||prev=='*'||prev=='/')continue;outPos=(size_t)i;return true;}
    }
    return false;
}
static string numToStr(double val){
    if(val==floor(val)&&fabs(val)<2e18){char buf[64];sprintf(buf,"%.0f",val);return buf;}
    char buf[64];sprintf(buf,"%g",val);return buf;
}
string evalExpr(const string& expr){
    string s=expr;trim(s);if(s.empty())return"0";
    if(isQuoted(s))return parseStringLiteral(s);
    if(isIdentifier(s)&&hasVar(s))return getVar(s);
    if(isPureNumber(s))return s;
    // list[index] / dict[key] 下标访问
    {
        string name, idx;
        if(isListIndexExpr(s, name, idx)){
            string idxVal = evalExpr(idx);
            if(g_waitingInput)return "";
            if(isListVar(name)){
                int i=atoi(idxVal.c_str());
                auto&lst=getList(name);
                if(i<0||i>=(int)lst.size()){return "";}
                return lst[i];
            }
            if(isDictVar(name)){
                auto&d=getDict(name);
                auto it=d.find(idxVal);
                return (it!=d.end())?it->second:"";
            }
            return "";
        }
    }
    bool replaced=true;int safetyCount=0;
    while(replaced&&safetyCount++<1000){replaced=false;
        for(auto& func:g_funcs){string pattern=func.first+"(";size_t pos=s.find(pattern);
            while(pos!=string::npos){
                if(pos>0&&(isalnum((unsigned char)s[pos-1])||s[pos-1]=='_')){pos=s.find(pattern,pos+1);continue;}
                size_t lp=pos+pattern.length();int depth=1;size_t rp=lp;bool inS=false,inD=false;
                while(rp<s.size()&&depth>0){char c=s[rp];
                    if(inS){if(c=='\'')inS=false;}else if(inD){if(c=='"')inD=false;}
                    else if(c=='\'')inS=true;else if(c=='"')inD=true;
                    else if(c=='(')depth++;else if(c==')'){depth--;if(depth==0)break;}
                    rp++;
                }
                if(depth!=0)break;
                string argsStr=s.substr(lp,rp-lp);vector<string> cargs;size_t ap=0;
                while(ap<=argsStr.size()){string a=getArg(argsStr,ap);cargs.push_back(evalExpr(a));
                    if(g_waitingInput)return "";
                    if(ap<argsStr.size()&&argsStr[ap]==',')ap++;else break;}
                string fr=callFunc(func.first,cargs);
                if(g_waitingInput)return "";
                s.replace(pos,rp-pos+1,fr);replaced=true;break;
            }
            if(replaced)break;
        }
        // Input/InputInt 表达式模式
        if(!replaced){
            static const char* inputFuncs[]={"InputInt","Input",NULL};
            for(int bi=0;inputFuncs[bi];bi++){
                string pattern=string(inputFuncs[bi])+"(";
                size_t pos=s.find(pattern);
                while(pos!=string::npos){
                    if(pos>0&&(isalnum((unsigned char)s[pos-1])||s[pos-1]=='_')){pos=s.find(pattern,pos+1);continue;}
                    size_t lp=pos+pattern.length();int depth=1;size_t rp=lp;bool inS=false,inD=false;
                    while(rp<s.size()&&depth>0){char c=s[rp];
                        if(inS){if(c=='\'')inS=false;}else if(inD){if(c=='"')inD=false;}
                        else if(c=='\'')inS=true;else if(c=='"')inD=true;
                        else if(c=='(')depth++;else if(c==')'){depth--;if(depth==0)break;}
                        rp++;
                    }
                    if(depth!=0)break;
                    string argsStr=s.substr(lp,rp-lp);
                    vector<string> iargs;size_t iap=0;
                    while(iap<=argsStr.size()){string a=getArg(argsStr,iap);iargs.push_back(evalExpr(a));
                        if(g_waitingInput)return "";
                        if(iap<argsStr.size()&&argsStr[iap]==',')iap++;else break;}
                    if(!g_inputPending){
                        g_waitingInput=true;
                        g_inputType=(inputFuncs[bi]=="InputInt")?1:0;
                        g_inputPrompt=iargs.size()>0?iargs[0]:"";
                        g_inputVarName="";
                        g_inputPending=true;
                        return "";
                    }else{
                        g_inputPending=false;
                        string result=g_inputResult;
                        s.replace(pos,rp-pos+1,result);replaced=true;break;
                    }
                }
                if(replaced)break;
            }
        }
        // 也尝试内置函数
        if(!replaced){
            static const char* builtins[]={
                "strLen","strUpper","strLower","strSub","strCat","strRep","strFind","strRFind",
                "strReplace","strTrim","strSplit","strJoin","strChar","strAscii","strChr",
                "strReverse","strStartsWith","strEndsWith","strContains","strCount",
                "strLeft","strRight","strToInt","strToFloat","numToStr",
                "listNew","listLen","listGet","listSet","listAppend","listInsert","listRemove",
                "listPop","listClear","listSort","listReverse","listFind","listContains",
                "listCopy","listSum","listJoin","listPrint",
                "abs","sqrt","pow","max","min","floor","ceil","round","mod","sin","cos","log",
                "CboxS","getCbox","isCbox","showAllCboxS",
                "dictNew","dictSet","dictGet","dictHas","dictRemove","dictLen",
                "dictClear","dictKeys","dictValues","dictCopy","dictPrint","dictMerge", NULL
            };
            for(int bi=0;builtins[bi];bi++){
                string pattern=string(builtins[bi])+"(";
                size_t pos=s.find(pattern);
                while(pos!=string::npos){
                    if(pos>0&&(isalnum((unsigned char)s[pos-1])||s[pos-1]=='_')){pos=s.find(pattern,pos+1);continue;}
                    size_t lp=pos+pattern.length();int depth=1;size_t rp=lp;bool inS=false,inD=false;
                    while(rp<s.size()&&depth>0){char c=s[rp];
                        if(inS){if(c=='\'')inS=false;}else if(inD){if(c=='"')inD=false;}
                        else if(c=='\'')inS=true;else if(c=='"')inD=true;
                        else if(c=='(')depth++;else if(c==')'){depth--;if(depth==0)break;}
                        rp++;
                    }
                    if(depth!=0)break;
                    string argsStr=s.substr(lp,rp-lp);vector<string> cargs;size_t ap=0;
                    // getCbox/isCbox 参数不求值(需要常量名)
                    bool noEval = (strcmp(builtins[bi],"getCbox")==0 || strcmp(builtins[bi],"isCbox")==0);
                    while(ap<=argsStr.size()){string a=getArg(argsStr,ap);
                        cargs.push_back(noEval ? a : evalExpr(a));
                        if(ap<argsStr.size()&&argsStr[ap]==',')ap++;else break;}
                    string fr;builtinCall(builtins[bi],cargs,fr);s.replace(pos,rp-pos+1,fr);replaced=true;break;
                }
                if(replaced)break;
            }
        }
        // 尝试 C++ 原生函数 (动态注册的)
        if (!replaced) {
            for (auto& e : g_nativeFuncs) {
                if (!e.func) continue;
                std::string pattern = e.name + "(";
                size_t pos = s.find(pattern);
                while (pos != string::npos) {
                    if (pos > 0 && (isalnum((unsigned char)s[pos-1]) || s[pos-1] == '_')) { pos = s.find(pattern, pos+1); continue; }
                    size_t lp = pos + pattern.length(); int depth = 1; size_t rp = lp; bool inS=false,inD=false;
                    while (rp < s.size() && depth > 0) { char c = s[rp];
                        if (inS) { if (c=='\'') inS=false; } else if (inD) { if (c=='"') inD=false; }
                        else if (c=='\'') inS=true; else if (c=='"') inD=true;
                        else if (c=='(') depth++; else if (c==')') { depth--; if (depth==0) break; }
                        rp++;
                    }
                    if (depth != 0) break;
                    string argsStr = s.substr(lp, rp-lp); vector<string> cargs; size_t ap = 0;
                    while (ap <= argsStr.size()) { string a = getArg(argsStr, ap); cargs.push_back(evalExpr(a));
                        if (g_waitingInput) return ""; if (ap < argsStr.size() && argsStr[ap]==',') ap++; else break; }
                    string fr; builtinCall(e.name, cargs, fr);
                    s.replace(pos, rp-pos+1, fr); replaced = true; break;
                }
                if (replaced) break;
            }
        }
    }
    trim(s);if(s.empty())return"";if(isQuoted(s))return parseStringLiteral(s);if(isPureNumber(s))return s;
    // 如果是单个标识符且是已定义变量,返回变量值
    if(isIdentifier(s)&&hasVar(s))return getVar(s);
    // 如果不含运算符(+,-,*,/,=)且不是引号/数字,当作字符串字面量返回(可能是内置函数返回值)
    if(s.find_first_of("+-*/=")==string::npos) return s;
    // 处理含下标的算术表达式: 把所有 list[i]/dict[k] 替换为值
    if(s.find('[')!=string::npos){
        bool didReplace=true;int safety=0;
        while(didReplace&&safety++<1000){didReplace=false;
            for(size_t p=0;p<s.size();p++){
                if(s[p]=='['&&p>0){
                    size_t nameStart=p;
                    while(nameStart>0&&(isalnum((unsigned char)s[nameStart-1])||s[nameStart-1]=='_'))nameStart--;
                    if(nameStart>=p)continue;
                    string name=s.substr(nameStart,p-nameStart);
                    if(!isIdentifier(name))continue;
                    int depth=1;size_t rb=string::npos;
                    for(size_t k=p+1;k<s.size();k++){if(s[k]=='[')depth++;else if(s[k]==']'){depth--;if(depth==0){rb=k;break;}}}
                    if(rb==string::npos)continue;
                    string idxExpr=s.substr(p+1,rb-p-1);trim(idxExpr);
                    string idxVal=evalExpr(idxExpr);if(g_waitingInput)return"";
                    string val;
                    if(isListVar(name)){int i=atoi(idxVal.c_str());auto&lst=getList(name);if(i>=0&&i<(int)lst.size())val=lst[i];}
                    else if(isDictVar(name)){auto&d=getDict(name);auto it=d.find(idxVal);if(it!=d.end())val=it->second;}
                    else continue;
                    if(!isPureNumber(val))val="\""+val+"\"";
                    s.replace(nameStart,rb-nameStart+1,val);didReplace=true;break;
                }
            }
        }
        trim(s);if(s.empty())return"";if(isQuoted(s))return parseStringLiteral(s);if(isPureNumber(s))return s;
        if(isIdentifier(s)&&hasVar(s))return getVar(s);
    }
    bool hasPlus=(s.find('+')!=string::npos);
    if(hasPlus){size_t pp;
        if(findConcatPlus(s,pp)){
            string l=s.substr(0,pp),r=s.substr(pp+1);
            string lt=l,rt=r;trim(lt);trim(rt);
            bool lIsStr=false,rIsStr=false;
            if(isQuoted(lt))lIsStr=true;
            else if(hasVar(lt)&&!isPureNumber(getVar(lt)))lIsStr=true;
            else if(lt.find('"')!=string::npos||lt.find('\'')!=string::npos)lIsStr=true;
            if(isQuoted(rt))rIsStr=true;
            else if(hasVar(rt)&&!isPureNumber(getVar(rt)))rIsStr=true;
            else if(rt.find('"')!=string::npos||rt.find('\'')!=string::npos)rIsStr=true;
            if(lIsStr||rIsStr)return evalExpr(l)+evalExpr(r);
        }
    }
    double val=calc(s);return numToStr(val);
}

string resolveFuncArg(const string& raw){
    string s=raw;trim(s);if(s.empty())return"";
    if(isQuoted(s))return parseStringLiteral(s);
    if(isIdentifier(s)&&!hasVar(s))return s;
    return evalExpr(s);
}

// ---- 内置函数 (字符串+列表) ----
bool builtinCall(const string& name, const vector<string>& args, string& result){
    // 先检查 C++ 原生函数
    if (callNativeFunc(name, args, result)) return true;
    if(name=="strLen"){ if(args.empty()){result="0";return true;} result=to_string(args[0].size()); return true; }
    if(name=="strUpper"){ if(args.empty()){result="";return true;} string r=args[0]; for(auto&c:r)c=(char)toupper((unsigned char)c); result=r; return true; }
    if(name=="strLower"){ if(args.empty()){result="";return true;} string r=args[0]; for(auto&c:r)c=(char)tolower((unsigned char)c); result=r; return true; }
    if(name=="strSub"){
        if(args.size()<2){result="";return true;}
        int start=atoi(args[1].c_str());
        int len=args.size()>=3?atoi(args[2].c_str()):(int)args[0].size();
        if(start<0)start=0; if(start>(int)args[0].size())start=(int)args[0].size();
        if(len<0)len=0; if(start+len>(int)args[0].size())len=(int)args[0].size()-start;
        result=args[0].substr(start,len); return true;
    }
    if(name=="strCat"){ string r; for(auto&a:args)r+=a; result=r; return true; }
    if(name=="strRep"){ if(args.size()<2){result="";return true;} int n=atoi(args[1].c_str()); if(n<0)n=0; string r; for(int i=0;i<n;i++)r+=args[0]; result=r; return true; }
    if(name=="strFind"){ if(args.size()<2){result="-1";return true;} size_t p=args[0].find(args[1]); result=(p==string::npos)?"-1":to_string((int)p); return true; }
    if(name=="strRFind"){ if(args.size()<2){result="-1";return true;} size_t p=args[0].rfind(args[1]); result=(p==string::npos)?"-1":to_string((int)p); return true; }
    if(name=="strReplace"){
        if(args.size()<3){result=args.empty()?"":args[0];return true;}
        string r=args[0]; const string&from=args[1]; const string&to=args[2];
        if(from.empty()){result=r;return true;}
        size_t pos=0; while((pos=r.find(from,pos))!=string::npos){r.replace(pos,from.size(),to);pos+=to.size();}
        result=r; return true;
    }
    if(name=="strTrim"){ if(args.empty()){result="";return true;} string r=args[0]; trim(r); result=r; return true; }
    if(name=="strSplit"){ if(args.size()<2){result="";return true;} vector<string> parts; string s=args[0],delim=args[1];
        if(delim.empty()){for(auto&c:s)parts.push_back(string(1,c));}
        else{size_t start=0,pos;while((pos=s.find(delim,start))!=string::npos){parts.push_back(s.substr(start,pos-start));start=pos+delim.size();}parts.push_back(s.substr(start));}
        static int sc=0; sc++; string ln="__split_"+to_string(sc)+"__"; setList(ln,parts); result=ln; return true;
    }
    if(name=="strJoin"){ if(args.size()<2){result="";return true;} string r; if(isListVar(args[0])){auto&lst=getList(args[0]);for(size_t i=0;i<lst.size();i++){if(i>0)r+=args[1];r+=lst[i];}} result=r; return true; }
    if(name=="strChar"){ if(args.size()<2){result="";return true;} int i=atoi(args[1].c_str()); if(i<0||i>=(int)args[0].size()){result="";return true;} result=string(1,args[0][i]); return true; }
    if(name=="strAscii"){ if(args.empty()||args[0].empty()){result="0";return true;} result=to_string((int)(unsigned char)args[0][0]); return true; }
    if(name=="strChr"){ if(args.empty()){result="";return true;} int n=atoi(args[0].c_str()); if(n<0||n>255){result="";return true;} result=string(1,(char)n); return true; }
    if(name=="strReverse"){ if(args.empty()){result="";return true;} string r=args[0]; reverse(r.begin(),r.end()); result=r; return true; }
    if(name=="strStartsWith"){ if(args.size()<2){result="false";return true;} result=startsWith(args[0],args[1])?"true":"false"; return true; }
    if(name=="strEndsWith"){ if(args.size()<2){result="false";return true;} if(args[1].size()>args[0].size()){result="false";return true;} result=(args[0].compare(args[0].size()-args[1].size(),args[1].size(),args[1])==0)?"true":"false"; return true; }
    if(name=="strContains"){ if(args.size()<2){result="false";return true;} result=(args[0].find(args[1])!=string::npos)?"true":"false"; return true; }
    if(name=="strCount"){ if(args.size()<2||args[1].empty()){result="0";return true;} int cnt=0; size_t pos=0; while((pos=args[0].find(args[1],pos))!=string::npos){cnt++;pos+=args[1].size();} result=to_string(cnt); return true; }
    if(name=="strLeft"){ if(args.size()<2){result=args.empty()?"":args[0];return true;} int n=atoi(args[1].c_str()); if(n<0)n=0; if(n>(int)args[0].size())n=(int)args[0].size(); result=args[0].substr(0,n); return true; }
    if(name=="strRight"){ if(args.size()<2){result=args.empty()?"":args[0];return true;} int n=atoi(args[1].c_str()); if(n<0)n=0; if(n>(int)args[0].size())n=(int)args[0].size(); result=args[0].substr(args[0].size()-n); return true; }
    if(name=="strToInt"){ if(args.empty()){result="0";return true;} result=to_string(atoi(args[0].c_str())); return true; }
    if(name=="strToFloat"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%g",atof(args[0].c_str())); result=b; return true; }
    if(name=="numToStr"){ if(args.empty()){result="0";return true;} result=args[0]; return true; }
    // 列表函数
    if(name=="listNew"){ if(args.empty()){result="";return true;} string ln=args[0]; vector<string> elems; for(size_t i=1;i<args.size();i++)elems.push_back(args[i]); setList(ln,elems); result=ln; return true; }
    if(name=="listLen"){ if(args.empty()){result="0";return true;} result=to_string(getList(args[0]).size()); return true; }
    if(name=="listGet"){ if(args.size()<2){result="";return true;} int i=atoi(args[1].c_str()); if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); if(i<0||i>=(int)lst.size()){result="";return true;} result=lst[i]; return true; }
    if(name=="listSet"){ if(args.size()<3){result="";return true;} int i=atoi(args[1].c_str()); if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); if(i<0||i>=(int)lst.size()){result="";return true;} lst[i]=args[2]; result=args[2]; return true; }
    if(name=="listAppend"){ if(args.size()<2){result="";return true;} if(!isListVar(args[0]))setList(args[0],vector<string>()); getList(args[0]).push_back(args[1]); result=args[1]; return true; }
    if(name=="listInsert"){ if(args.size()<3){result="";return true;} int i=atoi(args[1].c_str()); if(!isListVar(args[0]))setList(args[0],vector<string>()); auto&lst=getList(args[0]); if(i<0)i=0; if(i>(int)lst.size())i=(int)lst.size(); lst.insert(lst.begin()+i,args[2]); result=args[2]; return true; }
    if(name=="listRemove"){ if(args.size()<2){result="";return true;} int i=atoi(args[1].c_str()); if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); if(i<0||i>=(int)lst.size()){result="";return true;} lst.erase(lst.begin()+i); result="true"; return true; }
    if(name=="listPop"){ if(args.empty()){result="";return true;} if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); if(lst.empty()){result="";return true;} result=lst.back(); lst.pop_back(); return true; }
    if(name=="listClear"){ if(args.empty()){result="";return true;} setList(args[0],vector<string>()); result="true"; return true; }
    if(name=="listSort"){ if(args.empty()){result="";return true;} if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); sort(lst.begin(),lst.end()); result="true"; return true; }
    if(name=="listReverse"){ if(args.empty()){result="";return true;} if(!isListVar(args[0])){result="";return true;} auto&lst=getList(args[0]); reverse(lst.begin(),lst.end()); result="true"; return true; }
    if(name=="listFind"){ if(args.size()<2){result="-1";return true;} if(!isListVar(args[0])){result="-1";return true;} auto&lst=getList(args[0]); for(size_t i=0;i<lst.size();i++){if(lst[i]==args[1]){result=to_string((int)i);return true;}} result="-1"; return true; }
    if(name=="listContains"){ if(args.size()<2){result="false";return true;} auto&lst=getList(args[0]); for(auto&v:lst)if(v==args[1]){result="true";return true;} result="false"; return true; }
    if(name=="listCopy"){ if(args.size()<2){result="";return true;} setList(args[1],getList(args[0])); result=args[1]; return true; }
    if(name=="listSum"){ if(args.empty()){result="0";return true;} double sum=0; auto&lst=getList(args[0]); for(auto&v:lst)sum+=atof(v.c_str()); char b[64];sprintf(b,"%g",sum); result=b; return true; }
    if(name=="listJoin"){ if(args.size()<2){result="";return true;} string r; auto&lst=getList(args[0]); for(size_t i=0;i<lst.size();i++){if(i>0)r+=args[1];r+=lst[i];} result=r; return true; }
    if(name=="listPrint"){ if(args.empty()){result="";return true;} auto&lst=getList(args[0]); string r="["; for(size_t i=0;i<lst.size();i++){if(i>0)r+=", ";r+=lst[i];} r+="]"; Log(r); result=r; return true; }
    // 常量函数
    if(name=="CboxS"){ if(args.size()<2){reportError(ERR_ARG_COUNT,"CboxS needs 2 args");result="";return true;} string cn=args[0]; if(!isIdentifier(cn)){reportError(ERR_INVALID_NAME,"invalid constant name: "+cn);result="";return true;} if(isConstVar(cn)){reportError(ERR_INVALID_ARG,"constant already defined: "+cn);result="";return true;} setConst(cn,args[1]); result=cn; return true; }
    if(name=="getCbox"){ if(args.empty()){result="";return true;} result=isConstVar(args[0])?getConst(args[0]):""; return true; }
    if(name=="isCbox"){ if(args.empty()){result="false";return true;} result=isConstVar(args[0])?"true":"false"; return true; }
    if(name=="showAllCboxS"){ for(auto&c:g_consts)Log("  "+c.first+" = "+c.second); result=to_string(g_consts.size()); return true; }
    // 字典函数
    if(name=="dictNew"){ if(args.empty()){result="";return true;} setDict(args[0],map<string,string>()); result=args[0]; return true; }
    if(name=="dictSet"){ if(args.size()<3){result="";return true;} if(!isDictVar(args[0]))setDict(args[0],map<string,string>()); getDict(args[0])[args[1]]=args[2]; result=args[2]; return true; }
    if(name=="dictGet"){ if(args.size()<2){result="";return true;} if(!isDictVar(args[0])){result="";return true;} auto&d=getDict(args[0]); auto it=d.find(args[1]); result=(it!=d.end())?it->second:""; return true; }
    if(name=="dictHas"){ if(args.size()<2){result="false";return true;} if(!isDictVar(args[0])){result="false";return true;} auto&d=getDict(args[0]); result=(d.find(args[1])!=d.end())?"true":"false"; return true; }
    if(name=="dictRemove"){ if(args.size()<2){result="";return true;} if(!isDictVar(args[0])){result="";return true;} getDict(args[0]).erase(args[1]); result="true"; return true; }
    if(name=="dictLen"){ if(args.empty()){result="0";return true;} result=to_string(getDict(args[0]).size()); return true; }
    if(name=="dictClear"){ if(args.empty()){result="";return true;} setDict(args[0],map<string,string>()); result="true"; return true; }
    if(name=="dictKeys"){ if(args.empty()){result="";return true;} vector<string> keys; auto&d=getDict(args[0]); for(auto&kv:d)keys.push_back(kv.first); static int kc=0; kc++; string ln="__dk_"+to_string(kc)+"__"; setList(ln,keys); result=ln; return true; }
    if(name=="dictValues"){ if(args.empty()){result="";return true;} vector<string> vals; auto&d=getDict(args[0]); for(auto&kv:d)vals.push_back(kv.second); static int vc=0; vc++; string ln="__dv_"+to_string(vc)+"__"; setList(ln,vals); result=ln; return true; }
    if(name=="dictCopy"){ if(args.size()<2){result="";return true;} setDict(args[1],getDict(args[0])); result=args[1]; return true; }
    if(name=="dictPrint"){ if(args.empty()){result="";return true;} auto&d=getDict(args[0]); string r="{"; bool first=true; for(auto&kv:d){if(!first)r+=", ";r+=kv.first+": "+kv.second;first=false;} r+="}"; Log(r); result=r; return true; }
    if(name=="dictMerge"){ if(args.size()<2){result="";return true;} if(!isDictVar(args[0]))setDict(args[0],map<string,string>()); auto&src=getDict(args[1]); auto&dst=getDict(args[0]); for(auto&kv:src)dst[kv.first]=kv.second; result="true"; return true; }
    // 数学函数
    if(name=="abs"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%g",fabs(atof(args[0].c_str()))); result=b; return true; }
    if(name=="sqrt"){ if(args.empty()){result="0";return true;} double v=atof(args[0].c_str()); if(v<0){reportError(ERR_INVALID_NUM,"sqrt of negative");result="0";return true;} char b[64];sprintf(b,"%g",sqrt(v)); result=b; return true; }
    if(name=="pow"){ if(args.size()<2){result="0";return true;} char b[64];sprintf(b,"%g",pow(atof(args[0].c_str()),atof(args[1].c_str()))); result=b; return true; }
    if(name=="max"){ if(args.size()<2){result="0";return true;} double a=atof(args[0].c_str()),bv=atof(args[1].c_str()); char b[64];sprintf(b,"%g",a>bv?a:bv); result=b; return true; }
    if(name=="min"){ if(args.size()<2){result="0";return true;} double a=atof(args[0].c_str()),bv=atof(args[1].c_str()); char b[64];sprintf(b,"%g",a<bv?a:bv); result=b; return true; }
    if(name=="floor"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%.0f",floor(atof(args[0].c_str()))); result=b; return true; }
    if(name=="ceil"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%.0f",ceil(atof(args[0].c_str()))); result=b; return true; }
    if(name=="round"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%.0f",floor(atof(args[0].c_str())+0.5)); result=b; return true; }
    if(name=="mod"){ if(args.size()<2){result="0";return true;} int a=atoi(args[0].c_str()),bv=atoi(args[1].c_str()); if(bv==0){reportError(ERR_DIV_ZERO,"mod by zero");result="0";return true;} char b[64];sprintf(b,"%d",a%bv); result=b; return true; }
    if(name=="sin"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%g",sin(atof(args[0].c_str()))); result=b; return true; }
    if(name=="cos"){ if(args.empty()){result="0";return true;} char b[64];sprintf(b,"%g",cos(atof(args[0].c_str()))); result=b; return true; }
    if(name=="log"){ if(args.empty()){result="0";return true;} double v=atof(args[0].c_str()); if(v<=0){reportError(ERR_INVALID_NUM,"log of non-positive");result="0";return true;} char b[64];sprintf(b,"%g",log(v)); result=b; return true; }
    return false;
}

static bool startsWith(const string& s,const string& p){return s.size()>=p.size()&&s.compare(0,p.size(),p)==0;}
static size_t findAssignEq(const string& s){
    bool inS=false,inD=false;int depth=0;int bracketDepth=0;
    for(size_t i=0;i<s.size();i++){char c=s[i];
        if(inS){if(c=='\'')inS=false;continue;}
        if(inD){if(c=='"')inD=false;continue;}
        if(c=='\''){inS=true;continue;}
        if(c=='"'){inD=true;continue;}
        if(c=='(')depth++;else if(c==')'){if(depth>0)depth--;}
        else if(c=='[')bracketDepth++;else if(c==']'){if(bracketDepth>0)bracketDepth--;}
        else if(depth==0&&bracketDepth==0&&c=='='){
            if(i+1<s.size()&&s[i+1]=='='){i++;continue;}
            if(i>0&&(s[i-1]=='='||s[i-1]=='!'||s[i-1]=='<'||s[i-1]=='>'||s[i-1]=='+'||s[i-1]=='-'||s[i-1]=='*'||s[i-1]=='/'))continue;
            return i;
        }
    }
    return string::npos;
}

void runCode(const string& input){
    string s=input;trim(s);if(s.empty()||s[0]=='#')return;
    if(startsWith(s,"return ")){string val=s.substr(7);trim(val);setVar("__ret__",resolveFuncArg(val));g_returning=true;return;}
    if(s=="return"){setVar("__ret__","");g_returning=true;return;}
    size_t eqPos=findAssignEq(s);size_t parenPos=s.find('(');
    if(eqPos!=string::npos&&eqPos>0&&(parenPos==string::npos||eqPos<parenPos)){
        string vn=s.substr(0,eqPos),vl=s.substr(eqPos+1);trim(vn);trim(vl);
        if(vn.empty()||vl.empty())return;
        // list[index]=val / dict[key]=val
        {
            string lname, idx;
            if(isListIndexExpr(vn, lname, idx)){
                string idxVal=evalExpr(idx);if(g_waitingInput)return;
                string val=resolveFuncArg(vl);if(g_waitingInput)return;
                if(isListVar(lname)){
                    int i=atoi(idxVal.c_str());auto&lst=getList(lname);
                    if(i<0||i>=(int)lst.size()){reportError(ERR_OUT_OF_RANGE,"list index out of range: "+lname+"["+idxVal+"]");return;}
                    lst[i]=val;
                }else if(isDictVar(lname)){
                    getDict(lname)[idxVal]=val;
                }else{reportError(ERR_UNDEFINED_VAR,"undefined list/dict: "+lname);}
                return;
            }
        }
        if(!isIdentifier(vn)){reportError(ERR_INVALID_NAME,"invalid variable name: "+vn);return;}
        {string val=resolveFuncArg(vl);if(g_waitingInput)return;setVar(vn,val);}return;
    }
    if(parenPos!=string::npos&&s.back()==')'){
        string fn=s.substr(0,parenPos);trim(fn);
        vector<string> rawArgs;size_t pos=parenPos+1;
        while(pos<s.size()&&s[pos]!=')'){string a=getArg(s,pos);rawArgs.push_back(a);
            if(pos<s.size()&&s[pos]==',')pos++;else if(pos<s.size()&&s[pos]==')')break;else if(pos>=s.size())break;}
        if(g_funcs.find(fn)!=g_funcs.end()){
            vector<string> args;
            for(size_t i=0;i<rawArgs.size();i++){
                args.push_back(resolveFuncArg(rawArgs[i]));
                if(g_waitingInput)return;
            }
            callFunc(fn,args);return;
        }
        // Input/InputInt (语句模式) — 必须在内置函数之前
        if(fn=="Input"||fn=="InputInt"){g_waitingInput=true;g_inputType=(fn=="InputInt")?1:0;
            string prompt,varName;if(rawArgs.size()>0)prompt=resolveFuncArg(rawArgs[0]);
            if(rawArgs.size()>1){varName=rawArgs[1];trim(varName);}
            g_inputPrompt=prompt;g_inputVarName=varName;if(!prompt.empty())Log(prompt);return;}
        // box/boxS — 必须在内置函数之前
        if(fn=="box"&&rawArgs.size()>=2){
            string varName=rawArgs[0];string val=rawArgs[1];trim(val);
            if(startsWith(val,"Input(")&&val.back()==')'){
                g_waitingInput=true;g_inputType=0;g_inputVarName=varName;
                string inner=val.substr(6,val.size()-7);vector<string> ia;size_t ip=0;
                while(ip<inner.size()){ia.push_back(getArg(inner,ip));if(ip<inner.size()&&inner[ip]==',')ip++;}
                g_inputPrompt=ia.size()>0?resolveFuncArg(ia[0]):"";if(!g_inputPrompt.empty())Log(g_inputPrompt);return;}
            if(startsWith(val,"InputInt(")&&val.back()==')'){
                g_waitingInput=true;g_inputType=1;g_inputVarName=varName;
                string inner=val.substr(9,val.size()-10);vector<string> ia;size_t ip=0;
                while(ip<inner.size()){ia.push_back(getArg(inner,ip));if(ip<inner.size()&&inner[ip]==',')ip++;}
                g_inputPrompt=ia.size()>0?resolveFuncArg(ia[0]):"";if(!g_inputPrompt.empty())Log(g_inputPrompt);return;}
            setVar(varName,evalExpr(val));return;}
        if(fn=="boxS"&&rawArgs.size()>=3){
            string varName=rawArgs[1];string val=rawArgs[2];trim(val);
            if(startsWith(val,"Input(")&&val.back()==')'){
                g_waitingInput=true;g_inputType=0;g_inputVarName=varName;
                string inner=val.substr(6,val.size()-7);vector<string> ia;size_t ip=0;
                while(ip<inner.size()){ia.push_back(getArg(inner,ip));if(ip<inner.size()&&inner[ip]==',')ip++;}
                g_inputPrompt=ia.size()>0?resolveFuncArg(ia[0]):"";if(!g_inputPrompt.empty())Log(g_inputPrompt);return;}
            if(startsWith(val,"InputInt(")&&val.back()==')'){
                g_waitingInput=true;g_inputType=1;g_inputVarName=varName;
                string inner=val.substr(9,val.size()-10);vector<string> ia;size_t ip=0;
                while(ip<inner.size()){ia.push_back(getArg(inner,ip));if(ip<inner.size()&&inner[ip]==',')ip++;}
                g_inputPrompt=ia.size()>0?resolveFuncArg(ia[0]):"";if(!g_inputPrompt.empty())Log(g_inputPrompt);return;}
            setVar(varName,evalExpr(val));if(g_waitingInput)return;return;}
        // PrintLog 等语句函数 — 必须在内置返回值函数之前,避免参数被拦截
        if(fn=="PrintLog"){string out;for(size_t i=0;i<rawArgs.size();i++){if(i>0)out+=" ";out+=evalExpr(rawArgs[i]);if(g_waitingInput)return;}Log(out);return;}
        if(fn=="showAllBoxes"){for(auto& v:g_vars)Log("  "+v.first+" = "+v.second);return;}
        if(fn=="clearAllBoxes"){g_vars.clear();return;}
        if(fn=="showFuncs"){for(auto& f:g_funcs)Log("Func: "+f.first);return;}
        // 内置返回值函数
        {
            vector<string> args;
            for(size_t i=0;i<rawArgs.size();i++){
                args.push_back(resolveFuncArg(rawArgs[i]));
                if(g_waitingInput)return;
            }
            string result;
            if(builtinCall(fn,args,result)){return;}
        }
        reportError(ERR_UNDEFINED_FUNC,"unknown function: "+fn);return;
    }
    string ops[]={"+=","-=","*=","/="};
    for(int j=0;j<4;j++){size_t op=s.find(ops[j]);if(op!=string::npos&&op>0){
        string vn=s.substr(0,op),rv=s.substr(op+2);trim(vn);trim(rv);
        if(vn.empty()||rv.empty())return;
        if(!hasVar(vn)){setVar(vn,"0");Log("[Warn] auto-init undefined variable: "+vn);}
        double cur=atof(getVar(vn).c_str()),val=calc(rv),res;
        if(j==0)res=cur+val;else if(j==1)res=cur-val;else if(j==2)res=cur*val;else res=(val!=0?cur/val:0);
        setVar(vn,numToStr(res));return;
    }}
    Log("[Error] ? "+s);
}

void runLines(const vector<string>& lines){
    for(size_t i=0;i<lines.size();i++){
        if(g_waitingInput){
            ResumeFrame frame;frame.type=0;frame.lines=lines;frame.nextIndex=g_inputPending?(i>0?i-1:0):i;
            g_pendingFrames.push_back(frame);return;
        }
        if(g_breakLoop)break;
        string s=lines[i];trim(s);if(s.empty()||s[0]=='#')continue;
        // GetPack("库名") - 导入库
        if(startsWith(s,"GetPack(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');
            if(p1!=string::npos&&p2!=string::npos&&p2>p1){
                string arg=s.substr(p1+1,p2-p1-1);trim(arg);
                string packName=resolveFuncArg(arg);
                if(!packName.empty())loadPack(packName);
            }
            continue;
        }
        // GetCPack("库名") - 导入C++库
        if(startsWith(s,"GetCPack(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');
            if(p1!=string::npos&&p2!=string::npos&&p2>p1){
                string arg=s.substr(p1+1,p2-p1-1);trim(arg);
                string packName=resolveFuncArg(arg);
                if(!packName.empty())loadCPack(packName);
            }
            continue;
        }
        // try ... IfErrorToDo ... endTry
        if(s=="try"){
            vector<string> tryBody,errBody;bool inError=false;int depth=0;
            for(size_t j=i+1;j<lines.size();j++){string l=lines[j];trim(l);
                if(l=="endTry"&&depth==0){i=j;break;}
                if(startsWith(l,"Func(")||startsWith(l,"if(")||startsWith(l,"For(")||startsWith(l,"while(")||startsWith(l,"try"))depth++;
                if(l=="EndFunc"||l=="endif"||l=="endfor"||l=="endwhile"||l=="endTry")depth--;
                if(l=="IfErrorToDo"&&depth==0){inError=true;continue;}
                if(inError)errBody.push_back(lines[j]);else tryBody.push_back(lines[j]);
                if(j==lines.size()-1)i=j;
            }
            size_t errBefore=g_errors.size();
            bool savedInTry=g_inTryBlock;g_inTryBlock=true;g_breakLoop=false;
            runLines(tryBody);
            g_inTryBlock=savedInTry;g_breakLoop=false;
            if(g_errors.size()>errBefore)runLines(errBody);
            if(g_returning)return;
            continue;
        }
        if(startsWith(s,"Func(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');if(p1==string::npos||p2==string::npos)continue;
            string as=s.substr(p1+1,p2-p1-1);vector<string> fa;size_t ap=0;
            while(ap<=as.size()){fa.push_back(getArg(as,ap));if(ap<as.size()&&as[ap]==',')ap++;else break;}
            if(fa.empty())continue;
            Function func;func.name=fa[0];
            for(size_t k=1;k<fa.size();k++){string pn=fa[k];trim(pn);if(!pn.empty())func.params.push_back(pn);}
            int depth=0;
            for(size_t j=i+1;j<lines.size();j++){string l=lines[j];trim(l);
                if(l=="EndFunc"&&depth==0){i=j;break;}
                if(startsWith(l,"Func(")||startsWith(l,"if(")||startsWith(l,"For(")||startsWith(l,"while("))depth++;
                if(l=="EndFunc"||l=="endif"||l=="endfor"||l=="endwhile")depth--;
                func.body.push_back(lines[j]);if(j==lines.size()-1)i=j;
            }
            g_funcs[func.name]=func;continue;
        }
        if(startsWith(s,"if(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');if(p1==string::npos||p2==string::npos)continue;
            bool ok=cond(s.substr(p1+1,p2-p1-1));bool matched=ok;bool collecting=ok;
            vector<string> ib;int depth=0;
            for(size_t j=i+1;j<lines.size();j++){string l=lines[j];trim(l);
                if(l=="endif"&&depth==0){i=j;break;}
                if(startsWith(l,"if(")||startsWith(l,"For(")||startsWith(l,"while(")||startsWith(l,"Func("))depth++;
                if(l=="endif"||l=="endfor"||l=="endwhile"||l=="EndFunc")depth--;
                if(l=="else"&&depth==0){collecting=!matched;continue;}
                if(startsWith(l,"elseIf(")&&depth==0){
                    if(!matched){size_t a=l.find('('),b=l.find_last_of(')');
                        if(a!=string::npos&&b!=string::npos){ok=cond(l.substr(a+1,b-a-1));if(ok){matched=true;collecting=true;ib.clear();}else{collecting=false;}}}
                    else collecting=false;
                    continue;
                }
                if(collecting)ib.push_back(lines[j]);
                if(j==lines.size()-1)i=j;
            }
            if(!ib.empty())runLines(ib);
            if(g_returning)return;continue;
        }
        if(startsWith(s,"For(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');if(p1==string::npos||p2==string::npos)continue;
            string as=s.substr(p1+1,p2-p1-1);vector<string> fa;size_t ap=0;
            while(ap<=as.size()){fa.push_back(getArg(as,ap));if(ap<as.size()&&as[ap]==',')ap++;else break;}
            if(fa.size()<3)continue;
            string var=fa[0];trim(var);
            int start=(int)calc(fa[1]),end=(int)calc(fa[2]),step=1;bool isExprStep=false;
            if(fa.size()>=4){string ss=fa[3];trim(ss);if(isPureNumber(ss))step=(int)calc(ss);else isExprStep=true;}
            else step=(start<=end)?1:-1;
            if(step==0&&!isExprStep)continue;
            vector<string> fb;int depth=0;
            for(size_t j=i+1;j<lines.size();j++){string l=lines[j];trim(l);
                if(l=="endfor"&&depth==0){i=j;break;}
                if(startsWith(l,"For(")||startsWith(l,"while(")||startsWith(l,"if(")||startsWith(l,"Func("))depth++;
                if(l=="endfor"||l=="endwhile"||l=="endif"||l=="EndFunc")depth--;
                fb.push_back(lines[j]);if(j==lines.size()-1)i=j;
            }
            if(fb.empty())continue;
            int val=start,iter=0;
            while(iter<100000){
                bool cont;
                if(isExprStep){int curStep=(int)calc(fa[3]);cont=(curStep>=0)?(val<=end):(val>=end);}
                else cont=(step>=0)?(val<=end):(val>=end);
                if(!cont)break;
                char buf[64];sprintf(buf,"%d",val);setVar(var,buf);
                bool savedBreak=g_breakLoop;g_breakLoop=false;runLines(fb);
                if(g_returning){g_breakLoop=savedBreak;return;}
                if(g_waitingInput){
                    ResumeFrame frame;frame.type=1;frame.forVar=var;
                    frame.isExprStep=isExprStep;frame.forExprStepArg=isExprStep?fa[3]:"";
                    frame.forEnd=end;frame.forStep=step;frame.forBody=fb;
                    if(isExprStep)frame.forVal=(int)calc(fa[3]);else frame.forVal=val+step;
                    g_pendingFrames.push_back(frame);g_breakLoop=savedBreak;break;
                }
                if(g_breakLoop){g_breakLoop=false;break;}
                g_breakLoop=savedBreak;
                if(isExprStep)val=(int)calc(fa[3]);else val+=step;iter++;
            }
            continue;
        }
        if(startsWith(s,"while(")){
            size_t p1=s.find('('),p2=s.find_last_of(')');if(p1==string::npos||p2==string::npos)continue;
            string cs=s.substr(p1+1,p2-p1-1);vector<string> wb;int depth=0;
            for(size_t j=i+1;j<lines.size();j++){string l=lines[j];trim(l);
                if(l=="endwhile"&&depth==0){i=j;break;}
                if(startsWith(l,"while(")||startsWith(l,"For(")||startsWith(l,"if(")||startsWith(l,"Func("))depth++;
                if(l=="endwhile"||l=="endfor"||l=="endif"||l=="EndFunc")depth--;
                wb.push_back(lines[j]);if(j==lines.size()-1)i=j;
            }
            if(wb.empty())continue;
            int iter=0;
            while(iter<100000){
                if(!cond(cs))break;
                bool savedBreak=g_breakLoop;g_breakLoop=false;runLines(wb);
                if(g_returning){g_breakLoop=savedBreak;return;}
                if(g_waitingInput){
                    ResumeFrame frame;frame.type=2;frame.whileCond=cs;frame.whileBody=wb;
                    g_pendingFrames.push_back(frame);g_breakLoop=savedBreak;break;
                }
                if(g_breakLoop){g_breakLoop=false;break;}
                g_breakLoop=savedBreak;iter++;
            }
            continue;
        }
        if(s=="break"){g_breakLoop=true;return;}
        runCode(s);if(g_returning)return;
    }
    // 循环结束后,检测表达式模式 Input
    if (g_waitingInput && g_inputPending && !lines.empty()) {
        ResumeFrame frame;
        frame.type = 0;
        frame.lines = lines;
        frame.nextIndex = lines.size() - 1;
        g_pendingFrames.push_back(frame);
    }
}

// ---- 恢复机制辅助函数 ----
static void flushPendingFrames(){
    if(g_pendingFrames.empty())return;
    for(int k=(int)g_pendingFrames.size()-1;k>=0;k--)g_resumeStack.push_front(g_pendingFrames[k]);
    g_pendingFrames.clear();
}

void resumeForLoop(ResumeFrame& frame){
    int val=frame.forVal,iter=0;
    while(iter<100000){
        bool cont;
        if(frame.isExprStep){int curStep=(int)calc(frame.forExprStepArg);cont=(curStep>=0)?(val<=frame.forEnd):(val>=frame.forEnd);}
        else cont=(frame.forStep>=0)?(val<=frame.forEnd):(val>=frame.forEnd);
        if(!cont)break;
        char buf[64];sprintf(buf,"%d",val);setVar(frame.forVar,buf);
        bool savedBreak=g_breakLoop;g_breakLoop=false;runLines(frame.forBody);
        if(g_returning){g_breakLoop=savedBreak;return;}
        if(g_waitingInput){
            ResumeFrame nf;nf.type=1;nf.forVar=frame.forVar;
            nf.isExprStep=frame.isExprStep;nf.forExprStepArg=frame.forExprStepArg;
            nf.forEnd=frame.forEnd;nf.forStep=frame.forStep;nf.forBody=frame.forBody;
            if(frame.isExprStep)nf.forVal=(int)calc(frame.forExprStepArg);else nf.forVal=val+frame.forStep;
            g_pendingFrames.push_back(nf);g_breakLoop=savedBreak;return;
        }
        if(g_breakLoop){g_breakLoop=false;break;}
        g_breakLoop=savedBreak;
        if(frame.isExprStep)val=(int)calc(frame.forExprStepArg);else val+=frame.forStep;
        iter++;
    }
}

void resumeWhileLoop(ResumeFrame& frame){
    int iter=0;
    while(iter<100000){
        if(!cond(frame.whileCond))break;
        bool savedBreak=g_breakLoop;g_breakLoop=false;runLines(frame.whileBody);
        if(g_returning){g_breakLoop=savedBreak;return;}
        if(g_waitingInput){
            ResumeFrame nf;nf.type=2;nf.whileCond=frame.whileCond;nf.whileBody=frame.whileBody;
            g_pendingFrames.push_back(nf);g_breakLoop=savedBreak;return;
        }
        if(g_breakLoop){g_breakLoop=false;break;}
        g_breakLoop=savedBreak;iter++;
    }
}

void resumeExecution(){
    while(!g_resumeStack.empty()&&!g_waitingInput){
        ResumeFrame frame=g_resumeStack.front();g_resumeStack.pop_front();
        if(frame.type==0){
            if(frame.nextIndex<frame.lines.size()){
                vector<string> remaining(frame.lines.begin()+frame.nextIndex,frame.lines.end());
                runLines(remaining);
            }
        }else if(frame.type==1)resumeForLoop(frame);
        else if(frame.type==2)resumeWhileLoop(frame);
        flushPendingFrames();
    }
}

// ============================================================
// 测试框架
// ============================================================
int g_pass=0, g_fail=0, g_total=0;
#define CHECK(c, msg) do{ g_total++; \
    if(c){ g_pass++; cout<<"  \033[32m[PASS]\033[0m "<<msg<<endl; } \
    else{ g_fail++; cout<<"  \033[31m[FAIL]\033[0m "<<msg<<" (line "<<__LINE__<<")"<<endl; } \
}while(0)
#define CHECK_EQ(a, e, msg) do{ g_total++; \
    if((a)==(e)){ g_pass++; cout<<"  \033[32m[PASS]\033[0m "<<msg<<endl; } \
    else{ g_fail++; cout<<"  \033[31m[FAIL]\033[0m "<<msg<<" got='"<<(a)<<"' want='"<<(e)<<"'"<<endl; } \
}while(0)
#define CHECK_DBL(a, e, msg) do{ g_total++; \
    if(fabs((a)-(e))<1e-9){ g_pass++; cout<<"  \033[32m[PASS]\033[0m "<<msg<<endl; } \
    else{ g_fail++; cout<<"  \033[31m[FAIL]\033[0m "<<msg<<" got="<<(a)<<" want="<<(e)<<endl; } \
}while(0)
void reset(){ g_vars.clear(); g_funcs.clear(); g_lists.clear(); g_consts.clear(); g_dicts.clear(); g_loadedPacks.clear(); g_nativeFuncs.clear(); g_loadedCPacks.clear(); g_returning=false; g_breakLoop=false; g_waitingInput=false; g_logLines.clear(); g_pendingFrames.clear(); g_resumeStack.clear(); g_inputType=0; g_inputPrompt.clear(); g_inputVarName.clear(); g_inputPending=false; g_inputResult.clear(); g_errors.clear(); g_currentLineNo=0; }

// ============================================================
// 测试用例
// ============================================================

void test_isIdentifier(){
    cout<<"\n=== [isIdentifier] 标识符检测 ==="<<endl;
    CHECK(isIdentifier("abc"), "纯字母");
    CHECK(isIdentifier("ABC"), "大写字母");
    CHECK(isIdentifier("a"), "单字符");
    CHECK(isIdentifier("_"), "单下划线");
    CHECK(isIdentifier("_abc"), "下划线开头");
    CHECK(isIdentifier("_123"), "下划线+数字");
    CHECK(isIdentifier("abc123"), "字母+数字");
    CHECK(isIdentifier("a_b_c"), "含下划线");
    CHECK(isIdentifier("x1y2z3"), "字母数字混合");
    CHECK(isIdentifier("HelloWorld"), "驼峰命名");
    CHECK(isIdentifier("_"), "单下划线");
    CHECK(!isIdentifier(""), "空串");
    CHECK(!isIdentifier("123abc"), "数字开头");
    CHECK(!isIdentifier("a-b"), "含减号");
    CHECK(!isIdentifier("a b"), "含空格");
    CHECK(!isIdentifier("a.b"), "含点号");
    CHECK(!isIdentifier("a+b"), "含加号");
    CHECK(!isIdentifier("a(b"), "含左括号");
    CHECK(!isIdentifier("@abc"), "特殊符号开头");
    CHECK(!isIdentifier("$var"), "美元符号");
    CHECK(!isIdentifier("a#b"), "含井号");
    CHECK(!isIdentifier("\xff"), "高字节字符不崩溃");
    CHECK(!isIdentifier("中文"), "中文字符不崩溃");
}

void test_isPureNumber(){
    cout<<"\n=== [isPureNumber] 数字检测 ==="<<endl;
    CHECK(isPureNumber("0"), "零");
    CHECK(isPureNumber("42"), "正整数");
    CHECK(isPureNumber("-5"), "负整数");
    CHECK(isPureNumber("+5"), "正号整数");
    CHECK(isPureNumber("3.14"), "浮点数");
    CHECK(isPureNumber("-3.14"), "负浮点");
    CHECK(isPureNumber("+3.14"), "正号浮点");
    CHECK(isPureNumber(".5"), "无整数部分小数");
    CHECK(isPureNumber("5."), "无小数部分");
    CHECK(isPureNumber("1e10"), "科学计数法");
    CHECK(isPureNumber("1e-5"), "负指数");
    CHECK(isPureNumber("1e+5"), "正指数");
    CHECK(isPureNumber("1E10"), "大写E");
    CHECK(isPureNumber("1.5e3"), "完整科学计数法");
    CHECK(isPureNumber("1.5e-3"), "完整负指数");
    CHECK(isPureNumber("1234567890"), "长整数");
    CHECK(!isPureNumber(""), "空串");
    CHECK(!isPureNumber("abc"), "字母");
    CHECK(!isPureNumber("1-2"), "含减号");
    CHECK(!isPureNumber("1.2.3"), "多小数点");
    CHECK(!isPureNumber("--5"), "双负号");
    CHECK(!isPureNumber("++5"), "双正号");
    CHECK(!isPureNumber("1e"), "无指数数字不合法");
    CHECK(!isPureNumber("e5"), "无底数");
    CHECK(!isPureNumber("."), "单点");
    CHECK(!isPureNumber("-"), "单负号");
    CHECK(!isPureNumber("+"), "单正号");
    CHECK(!isPureNumber("1.5e1.5"), "指数含小数");
    CHECK(!isPureNumber("3+4"), "表达式");
    CHECK(!isPureNumber("0x10"), "十六进制");
}

void test_isQuoted(){
    cout<<"\n=== [isQuoted] 引号字符串检测 ==="<<endl;
    CHECK(isQuoted("\"hello\""), "双引号字符串");
    CHECK(isQuoted("'hello'"), "单引号字符串");
    CHECK(isQuoted("\"\""), "空双引号");
    CHECK(isQuoted("''"), "空单引号");
    CHECK(isQuoted("\"a\""), "单字符双引号");
    CHECK(isQuoted("\"a\\\"b\""), "含转义引号");
    CHECK(isQuoted("\"a\\\\b\""), "含转义反斜杠");
    CHECK(!isQuoted(""), "空串");
    CHECK(!isQuoted("\""), "单引号");
    CHECK(!isQuoted("\"abc"), "只有开头引号");
    CHECK(!isQuoted("abc\""), "只有结尾引号");
    CHECK(!isQuoted("abc"), "无引号");
    CHECK(!isQuoted("\"abc\"+\"def\""), "拼接字符串(非单个)");
    CHECK(!isQuoted("\"a\"b\""), "中间有引号");
}

void test_hasStringLiteral(){
    cout<<"\n=== [hasStringLiteral] 字符串字面量检测 ==="<<endl;
    CHECK(hasStringLiteral("\"abc\""), "含双引号");
    CHECK(hasStringLiteral("'abc'"), "含单引号");
    CHECK(hasStringLiteral("a\"b"), "中间含双引号");
    CHECK(hasStringLiteral("a'b"), "中间含单引号");
    CHECK(hasStringLiteral("\""), "单引号字符");
    CHECK(!hasStringLiteral("abc"), "无引号");
    CHECK(!hasStringLiteral(""), "空串");
    CHECK(!hasStringLiteral("123"), "纯数字");
    CHECK(!hasStringLiteral("a+b"), "表达式");
}

void test_parseStringLiteral(){
    cout<<"\n=== [parseStringLiteral] 字符串字面量解析 ==="<<endl;
    CHECK_EQ(parseStringLiteral("\"hello\""), "hello", "双引号");
    CHECK_EQ(parseStringLiteral("'hello'"), "hello", "单引号");
    CHECK_EQ(parseStringLiteral("\"\""), "", "空字符串");
    CHECK_EQ(parseStringLiteral("\"a\\nb\""), "a\nb", "转义\\n");
    CHECK_EQ(parseStringLiteral("\"a\\tb\""), "a\tb", "转义\\t");
    CHECK_EQ(parseStringLiteral("\"a\\rb\""), "a\rb", "转义\\r");
    CHECK_EQ(parseStringLiteral("\"a\\\\b\""), "a\\b", "转义反斜杠");
    CHECK_EQ(parseStringLiteral("\"a\\\"b\""), "a\"b", "转义双引号");
    CHECK_EQ(parseStringLiteral("\"a\\'b\""), "a'b", "转义单引号");
    CHECK_EQ(parseStringLiteral("\"hello world\""), "hello world", "含空格");
    CHECK_EQ(parseStringLiteral("\"123\""), "123", "数字字符串");
}

void test_numToStr(){
    cout<<"\n=== [numToStr] 数字转字符串 ==="<<endl;
    CHECK_EQ(numToStr(0), "0", "零");
    CHECK_EQ(numToStr(42), "42", "整数");
    CHECK_EQ(numToStr(-5), "-5", "负整数");
    CHECK_EQ(numToStr(3.14), "3.14", "浮点数");
    CHECK_EQ(numToStr(-3.14), "-3.14", "负浮点");
    CHECK_EQ(numToStr(100), "100", "百");
    // numToStr 用 %.0f 格式化大数,输出完整数字而非科学计数法
    CHECK_EQ(numToStr(1e18), "1000000000000000000", "大数完整输出");
    CHECK_EQ(numToStr(0.5), "0.5", "小数");
    CHECK_EQ(numToStr(100.0), "100", "整数浮点");
    CHECK_EQ(numToStr(3.0), "3", "整数浮点2");
}

void test_startsWith(){
    cout<<"\n=== [startsWith] 前缀检测 ==="<<endl;
    CHECK(startsWith("hello world", "hello"), "匹配前缀");
    CHECK(startsWith("if(x)", "if("), "if前缀");
    CHECK(startsWith("Func(a,b)", "Func("), "Func前缀");
    CHECK(startsWith("abc", ""), "空前缀总匹配");
    CHECK(startsWith("abc", "abc"), "完全匹配");
    CHECK(!startsWith("abc", "abcd"), "前缀比原串长");
    CHECK(!startsWith("hello", "world"), "不匹配");
    CHECK(!startsWith("", "a"), "空串无前缀");
    CHECK(!startsWith("if(x)", "if "), "if后是空格非左括号");
}

void test_findAssignEq(){
    cout<<"\n=== [findAssignEq] 赋值号定位 ==="<<endl;
    CHECK_EQ(findAssignEq("x=5"), 1u, "简单赋值");
    CHECK_EQ(findAssignEq("x = 5"), 2u, "带空格赋值");
    CHECK_EQ(findAssignEq("x = y"), 2u, "变量赋值");
    CHECK_EQ(findAssignEq("abc = 42"), 4u, "长变量名");
    CHECK(findAssignEq("x==5")==string::npos, "等于号不识别为赋值");
    CHECK(findAssignEq("x!=5")==string::npos, "不等号不识别");
    CHECK(findAssignEq("x>=5")==string::npos, "大于等于不识别");
    CHECK(findAssignEq("x<=5")==string::npos, "小于等于不识别");
    CHECK(findAssignEq("x+=5")==string::npos, "复合赋值+=不识别");
    CHECK(findAssignEq("x-=5")==string::npos, "复合赋值-=不识别");
    CHECK(findAssignEq("x*=5")==string::npos, "复合赋值*=不识别");
    CHECK(findAssignEq("x/=5")==string::npos, "复合赋值/=不识别");
    CHECK_EQ(findAssignEq("\"x=y\"=5"), 5u, "Bug#23: 字符串内=不误判(找第2个=)");
    CHECK_EQ(findAssignEq("foo(\"a=b\")=1"), 10u, "括号内=不误判(找第2个=在位置10)");
    CHECK_EQ(findAssignEq("a=b=c"), 1u, "多重=找第一个");
    CHECK(findAssignEq("abc")==string::npos, "无=返回npos");
    CHECK(findAssignEq("")==string::npos, "空串返回npos");
}

void test_findTopLevelOp(){
    cout<<"\n=== [findTopLevelOp] 顶层+-定位 ==="<<endl;
    size_t pos;
    CHECK(findTopLevelOp("3+4", '+', '-', pos) && pos==1, "简单加法");
    CHECK(findTopLevelOp("3-4", '+', '-', pos) && pos==1, "简单减法");
    CHECK(findTopLevelOp("3+4*5", '+', '-', pos) && pos==1, "加法在乘法前(低优先级)");
    // 括号内的 + 不应被识别为顶层运算符
    CHECK(!findTopLevelOp("(3+4)*2", '+', '-', pos), "括号内+不被识别为顶层");
    CHECK(!findTopLevelOp("5", '+', '-', pos), "无运算符");
    CHECK(findTopLevelOp("5*-3", '+', '-', pos)==false, "一元负号*-不识别为二元");
    CHECK(findTopLevelOp("3 - -5", '+', '-', pos) && pos==2, "空格后双负号识别二元-");
    CHECK(findTopLevelOp("\"a+b\"", '+', '-', pos)==false, "字符串内+不识别");
    CHECK(findTopLevelOp("1+2+3", '+', '-', pos) && pos==3, "多+找最右(左结合)");
}

void test_findTopLevelMulDiv(){
    cout<<"\n=== [findTopLevelMulDiv] 顶层*/定位 ==="<<endl;
    size_t pos;
    CHECK(findTopLevelMulDiv("3*4", '*', pos) && pos==1, "简单乘法");
    CHECK(findTopLevelMulDiv("3/4", '/', pos) && pos==1, "简单除法");
    CHECK(findTopLevelMulDiv("3*4*5", '*', pos) && pos==3, "多*找最右");
    CHECK(findTopLevelMulDiv("(3*4)", '*', pos)==false, "括号内*不识别");
    CHECK(!findTopLevelMulDiv("3+4", '*', pos), "无乘除");
    CHECK(findTopLevelMulDiv("\"a*b\"", '*', pos)==false, "字符串内*不识别");
}

void test_findConcatPlus(){
    cout<<"\n=== [findConcatPlus] 字符串拼接+定位 ==="<<endl;
    size_t pos;
    CHECK(findConcatPlus("\"a\"+\"b\"", pos) && pos==3, "字符串拼接+");
    CHECK(findConcatPlus("\"a\" + \"b\"", pos), "带空格拼接可识别");
    CHECK(findConcatPlus("x+y", pos) && pos==1, "变量拼接");
    CHECK(findConcatPlus("(a+b)+c", pos) && pos==5, "括号外+");
    CHECK(findConcatPlus("3+4", pos)==false || pos==1, "数值+找最右(可能在pos=1)");
    CHECK(findConcatPlus("\"a+b\"", pos)==false, "字符串内+不识别");
}

void test_calc(){
    cout<<"\n=== [calc] 表达式求值 ==="<<endl;
    // 基本算术
    CHECK_DBL(calc("3+4"), 7, "加法");
    CHECK_DBL(calc("10-3"), 7, "减法");
    CHECK_DBL(calc("3*4"), 12, "乘法");
    CHECK_DBL(calc("12/4"), 3, "除法");
    CHECK_DBL(calc("12/0"), 0, "Bug#33: 除零返回0");
    CHECK_DBL(calc("0/5"), 0, "零除以数");
    // 优先级
    CHECK_DBL(calc("2+3*4"), 14, "乘法优先");
    CHECK_DBL(calc("2*3+4"), 10, "乘法优先2");
    CHECK_DBL(calc("2+3+4"), 9, "连续加法");
    CHECK_DBL(calc("10-2-3"), 5, "左结合减法");
    CHECK_DBL(calc("100/4/5"), 5, "左结合除法");
    CHECK_DBL(calc("2*3*4"), 24, "连续乘法");
    // 括号
    CHECK_DBL(calc("(3+4)"), 7, "Bug#1: 单括号");
    CHECK_DBL(calc("(3+4)*2"), 14, "Bug#1: 括号乘法");
    CHECK_DBL(calc("((1+2)*(3+4))"), 21, "多层括号");
    CHECK_DBL(calc("(((5)))"), 5, "三层括号单值");
    CHECK_DBL(calc("(2+3)*(4+5)"), 45, "双括号乘法");
    // 一元负号
    CHECK_DBL(calc("-5"), -5, "一元负号");
    CHECK_DBL(calc("+5"), 5, "一元正号");
    CHECK_DBL(calc("5*-3"), -15, "Bug#3: 5*-3");
    CHECK_DBL(calc("3 - -5"), 8, "Bug#3: 3 - -5");
    CHECK_DBL(calc("3 + -5"), -2, "3 + -5");
    CHECK_DBL(calc("-(-5)"), 5, "双重负号");
    CHECK_DBL(calc("-5+3"), -2, "负号开头加法");
    CHECK_DBL(calc("-(3+4)"), -7, "负号括号");
    // 科学计数法
    CHECK_DBL(calc("1e10"), 1e10, "科学计数法正指数");
    CHECK_DBL(calc("1e-5"), 1e-5, "Bug#11: 科学计数法负指数");
    CHECK_DBL(calc("1.5e3"), 1500, "完整科学计数法");
    CHECK_DBL(calc("1E5"), 1e5, "大写E");
    // 浮点
    CHECK_DBL(calc("3.14"), 3.14, "浮点数");
    CHECK_DBL(calc("3.14*2"), 6.28, "浮点乘法");
    CHECK_DBL(calc("0.5+0.5"), 1.0, "浮点加法");
    CHECK_DBL(calc(".5"), 0.5, "无整数部分小数");
    // 边界
    CHECK_DBL(calc("0"), 0, "零");
    CHECK_DBL(calc(""), 0, "空串");
    CHECK_DBL(calc("   "), 0, "纯空格");
    CHECK_DBL(calc("42"), 42, "纯整数");
    // 字符串应返回0
    CHECK_DBL(calc("\"abc\""), 0, "字符串返回0");
    CHECK_DBL(calc("'abc'"), 0, "单引号字符串返回0");
    // 变量
    setVar("x","10"); CHECK_DBL(calc("x"), 10, "变量");
    setVar("y","3.5"); CHECK_DBL(calc("y"), 3.5, "浮点变量");
    setVar("z","abc"); CHECK_DBL(calc("z"), 0, "非数字变量返回0");
    CHECK_DBL(calc("x+y"), 13.5, "变量加法");
    CHECK_DBL(calc("x*2"), 20, "变量乘法");
    reset();
    // 无法解析的表达式
    CHECK_DBL(calc("abc"), 0, "未定义标识符(atof=0)");
    CHECK_DBL(calc("@#$"), 0, "特殊字符(atof=0)");
}

void test_cond(){
    cout<<"\n=== [cond] 条件判断 ==="<<endl;
    // 数字比较
    CHECK(cond("1==1"), "数字相等");
    CHECK(!cond("1==2"), "数字不等");
    CHECK(cond("1!=2"), "不等为真");
    CHECK(!cond("1!=1"), "相等的不等判断为假");
    CHECK(cond("3>2"), "大于");
    CHECK(!cond("2>3"), "大于假");
    CHECK(cond("2<3"), "小于");
    CHECK(!cond("3<2"), "小于假");
    CHECK(cond("3>=3"), "大于等于(等)");
    CHECK(cond("3>=2"), "大于等于(大)");
    CHECK(!cond("2>=3"), "大于等于假");
    CHECK(cond("3<=3"), "小于等于(等)");
    CHECK(cond("2<=3"), "小于等于(小)");
    CHECK(!cond("3<=2"), "小于等于假");
    // 字面量
    CHECK(cond("true"), "true");
    CHECK(!cond("false"), "false");
    CHECK(cond("1"), "非零为真");
    CHECK(!cond("0"), "零为假");
    CHECK(cond("-1"), "负数非零为真");
    CHECK(cond("3.14"), "浮点非零为真");
    CHECK(!cond("0.0"), "浮点零为假");
    // 字符串比较
    CHECK(cond("\"abc\"==\"abc\""), "Bug#8: 相同字符串相等");
    CHECK(!cond("\"abc\"==\"def\""), "Bug#8: 不同字符串不等");
    CHECK(cond("\"abc\"!=\"def\""), "字符串不等");
    CHECK(!cond("\"abc\"!=\"abc\""), "相同字符串不等为假");
    CHECK(cond("\"b\">\"a\""), "字符串字典序");
    CHECK(cond("\"abc\"<\"abd\""), "字符串字典序2");
    CHECK(cond("\"abc\">=\"abc\""), "字符串大于等于");
    CHECK(cond("\"abc\"<=\"abc\""), "字符串小于等于");
    // 字符串内含运算符
    CHECK(cond("\"a>b\"==\"a>b\""), "Bug#25: 字符串内含>");
    CHECK(!cond("\"a>b\"==\"a<c\""), "Bug#25: 字符串内含运算符不等");
    CHECK(cond("\"x=y\"==\"x=y\""), "字符串内含=");
    CHECK(cond("\"a==b\"!=\"c\""), "字符串内含==");
    // 变量
    setVar("flag","true"); CHECK(cond("flag"), "变量=true");
    setVar("flag","false"); CHECK(!cond("flag"), "变量=false");
    setVar("flag","1"); CHECK(cond("flag"), "变量=1");
    setVar("flag","0"); CHECK(!cond("flag"), "变量=0");
    setVar("flag","3.14"); CHECK(cond("flag"), "变量=3.14");
    // 变量=非数字字符串时, cond 直接走 hasVar 分支
    // "hello" 不是true/false, atof("hello")=0, 返回 false
    setVar("flag","hello"); CHECK(!cond("flag"), "变量=非数字字符串在cond中为假");
    setVar("flag",""); CHECK(!cond("flag"), "变量=空字符串");
    setVar("a","5"); setVar("b","3");
    CHECK(cond("a>b"), "变量比较>");
    CHECK(cond("a==5"), "变量==数字");
    CHECK(cond("a!=b"), "变量不等");
    reset();
    // 复合表达式
    CHECK(cond("3+2==5"), "表达式比较");
    CHECK(cond("(3+4)*2==14"), "Bug#1: 括号表达式比较");
    CHECK(cond("2*3 > 5"), "表达式大于");
    CHECK(!cond("1+1==3"), "表达式不等");
    // 边界
    CHECK(!cond(""), "空串为假");
    CHECK(!cond("   "), "空格为假");
}

void test_getArg(){
    cout<<"\n=== [getArg] 参数解析 ==="<<endl;
    size_t pos;
    // 简单参数
    pos=0; CHECK_EQ(getArg("abc", pos), "abc", "简单标识符");
    pos=0; CHECK_EQ(getArg("123", pos), "123", "数字");
    pos=0; CHECK_EQ(getArg("  abc  ", pos), "abc", "带空格trim");
    pos=0; CHECK_EQ(getArg("", pos), "", "空串");
    pos=0; CHECK_EQ(getArg("   ", pos), "", "纯空格");
    // 引号字符串
    pos=0; CHECK_EQ(getArg("\"hello\"", pos), "\"hello\"", "双引号(保留引号)");
    pos=0; CHECK_EQ(getArg("'hello'", pos), "'hello'", "单引号(保留引号)");
    pos=0; CHECK_EQ(getArg("\"\"", pos), "\"\"", "空双引号");
    pos=0; CHECK_EQ(getArg("''", pos), "''", "空单引号");
    pos=0; CHECK_EQ(getArg("\"hello world\"", pos), "\"hello world\"", "含空格字符串(保留引号)");
    // 转义
    pos=0; CHECK_EQ(getArg("\"a\\nb\"", pos), "\"a\\nb\"", "转义原样保留");
    pos=0; CHECK_EQ(getArg("\"a\\\"b\"", pos), "\"a\\\"b\"", "转义引号原样保留");
    // 多参数
    pos=0;
    string a1=getArg("foo, bar, baz", pos);
    if(pos<16 && "foo, bar, baz"[pos]==',') pos++;
    string a2=getArg("foo, bar, baz", pos);
    if(pos<16 && "foo, bar, baz"[pos]==',') pos++;
    string a3=getArg("foo, bar, baz", pos);
    CHECK_EQ(a1, "foo", "多参数1");
    CHECK_EQ(a2, "bar", "多参数2");
    CHECK_EQ(a3, "baz", "多参数3");
    // 嵌套括号
    pos=0; CHECK_EQ(getArg("func(a, b), x", pos), "func(a, b)", "嵌套括号参数");
    pos=0; CHECK_EQ(getArg("(1+2)*3", pos), "(1+2)*3", "括号表达式");
    // 引号内逗号 (保留引号)
    pos=0; CHECK_EQ(getArg("\"a,b\", c", pos), "\"a,b\"", "引号内逗号不分割(保留引号)");
    pos=0; CHECK_EQ(getArg("'a,b,c'", pos), "'a,b,c'", "单引号内多逗号(保留引号)");
    // 引号内括号
    pos=0; CHECK_EQ(getArg("\"(a,b)\"", pos), "\"(a,b)\"", "引号内括号(保留引号)");
}

void test_evalExpr(){
    cout<<"\n=== [evalExpr] 表达式求值 ==="<<endl;
    // 字符串字面量
    CHECK_EQ(evalExpr("\"hello\""), "hello", "双引号字符串");
    CHECK_EQ(evalExpr("'world'"), "world", "单引号字符串");
    CHECK_EQ(evalExpr("\"\""), "", "空字符串");
    CHECK_EQ(evalExpr("'a\\nb'"), "a\nb", "转义字符串");
    CHECK_EQ(evalExpr("\"Hello World\""), "Hello World", "含空格");
    CHECK_EQ(evalExpr("\"123\""), "123", "数字字符串");
    // 纯数字
    CHECK_EQ(evalExpr("42"), "42", "整数");
    CHECK_EQ(evalExpr("-5"), "-5", "负整数");
    CHECK_EQ(evalExpr("3.14"), "3.14", "浮点");
    CHECK_EQ(evalExpr("1e10"), "1e10", "科学计数法(原样返回)");
    CHECK_EQ(evalExpr("0"), "0", "零");
    // 算术表达式
    CHECK_EQ(evalExpr("3+4"), "7", "加法");
    CHECK_EQ(evalExpr("10-3"), "7", "减法");
    CHECK_EQ(evalExpr("3*4"), "12", "乘法");
    CHECK_EQ(evalExpr("12/4"), "3", "除法");
    CHECK_EQ(evalExpr("(3+4)*2"), "14", "Bug#1: 括号");
    CHECK_EQ(evalExpr("2+3*4"), "14", "优先级");
    CHECK_EQ(evalExpr("1-2"), "-1", "Bug#9: 1-2=-1");
    CHECK_EQ(evalExpr("5*-3"), "-15", "一元负号");
    // 变量
    setVar("x","10");
    CHECK_EQ(evalExpr("x"), "10", "纯变量");
    setVar("name","Alice");
    CHECK_EQ(evalExpr("name"), "Alice", "字符串变量");
    setVar("n","5");
    CHECK_EQ(evalExpr("n+3"), "8", "变量加法");
    CHECK_EQ(evalExpr("n*2"), "10", "变量乘法");
    // 字符串拼接
    CHECK_EQ(evalExpr("\"a\"+\"b\""), "ab", "Bug#12: 字符串拼接");
    CHECK_EQ(evalExpr("\"x\" + \"y\""), "xy", "带空格拼接");
    CHECK_EQ(evalExpr("\"Hello \"+name"), "Hello Alice", "字符串+变量");
    setVar("count","3");
    CHECK_EQ(evalExpr("\"count=\"+count"), "count=3", "字符串+数字变量");
    // 复杂拼接
    setVar("i","1"); setVar("j","1");
    CHECK_EQ(evalExpr("i+\"x\"+j+\" \""), "1x1 ", "Bug: 变量+字符串+变量");
    reset();
    // 函数调用
    vector<string> fdef = {"Func(add, a, b)", "  return a + b", "EndFunc"};
    runLines(fdef);
    CHECK_EQ(evalExpr("add(3, 4)"), "7", "函数调用加法");
    CHECK_EQ(evalExpr("add(10, 20)"), "30", "函数调用2");
    CHECK_EQ(evalExpr("add(add(1,2), 3)"), "6", "嵌套函数调用");
    reset();
    // 边界
    CHECK_EQ(evalExpr(""), "0", "空串返回0");
    CHECK_EQ(evalExpr("   "), "0", "空格返回0");
    // 数字格式化
    // isPureNumber 原样返回,不格式化
    CHECK_EQ(evalExpr("3.0"), "3.0", "浮点原样返回");
    CHECK_EQ(evalExpr("100.0"), "100.0", "大整数浮点原样返回");
}

void test_resolveFuncArg(){
    cout<<"\n=== [resolveFuncArg] 参数解析 ==="<<endl;
    CHECK_EQ(resolveFuncArg("\"hello\""), "hello", "字符串字面量");
    CHECK_EQ(resolveFuncArg("'world'"), "world", "单引号字符串");
    CHECK_EQ(resolveFuncArg("42"), "42", "数字");
    CHECK_EQ(resolveFuncArg("3+4"), "7", "表达式");
    CHECK_EQ(resolveFuncArg(""), "", "空串");
    setVar("x","10");
    CHECK_EQ(resolveFuncArg("x"), "10", "已定义变量");
    reset();
    CHECK_EQ(resolveFuncArg("undefined"), "undefined", "未定义标识符返回自身");
    CHECK_EQ(resolveFuncArg("var1"), "var1", "未定义标识符(含数字)");
    CHECK_EQ(resolveFuncArg("(3+4)*2"), "14", "括号表达式");
}

void test_callFunc(){
    cout<<"\n=== [callFunc] 函数调用与作用域 ==="<<endl;
    // 基本函数
    vector<string> add = {"Func(add, a, b)", "  return a + b", "EndFunc"};
    runLines(add);
    CHECK_EQ(callFunc("add",{"3","4"}), "7", "加法函数");
    CHECK_EQ(callFunc("add",{"10","20"}), "30", "加法函数2");
    CHECK_EQ(callFunc("add",{"-5","3"}), "-2", "负数加法");
    // 无返回值函数
    vector<string> noret = {"Func(setX, x)", "  global = x", "EndFunc"};
    runLines(noret);
    CHECK_EQ(callFunc("setX",{"42"}), "", "无返回值函数返回空");
    CHECK(!hasVar("x"), "Bug#5: 参数x不泄漏");
    CHECK(!hasVar("global"), "Bug#5: 局部变量不泄漏");
    // 作用域隔离
    CHECK(!hasVar("a"), "Bug#5: 调用后参数a不存在");
    CHECK(!hasVar("b"), "Bug#5: 调用后参数b不存在");
    callFunc("add",{"1","2"});
    CHECK(!hasVar("a"), "Bug#5: 调用后参数a仍不存在");
    CHECK(!hasVar("b"), "Bug#5: 调用后参数b仍不存在");
    // __ret__ 恢复
    setVar("__ret__","outer");
    callFunc("add",{"1","2"});
    CHECK_EQ(getVar("__ret__"), "outer", "Bug#4: 调用后__ret__恢复");
    setVar("__ret__","");
    callFunc("add",{"1","2"});
    CHECK(hasVar("__ret__"), "Bug#4: 空__ret__也存在");
    CHECK_EQ(getVar("__ret__"), "", "Bug#4: 空__ret__保持空");
    eraseVar("__ret__");
    callFunc("add",{"1","2"});
    CHECK(!hasVar("__ret__"), "Bug#4: 不存在的__ret__调用后仍不存在");
    // 不存在的函数
    CHECK_EQ(callFunc("nonexistent",{}), "", "不存在的函数返回空");
    // 参数不足
    CHECK_EQ(callFunc("add",{"5"}), "5", "参数不足(默认空)");
    // 无参数时, a/b 被设为空字符串, a+b 走字符串拼接返回空
    CHECK_EQ(callFunc("add",{}), "", "无参数(a/b为空字符串,拼接为空)");
    reset();
    // 递归
    vector<string> fact = {"Func(fact, n)", "  if(n <= 1)", "    return 1", "  endif", "  return n * fact(n - 1)", "EndFunc"};
    runLines(fact);
    CHECK_EQ(callFunc("fact",{"5"}), "120", "递归阶乘5!");
    CHECK_EQ(callFunc("fact",{"10"}), "3628800", "递归阶乘10!");
    CHECK_EQ(callFunc("fact",{"0"}), "1", "0!=1");
    CHECK_EQ(callFunc("fact",{"1"}), "1", "1!=1");
    reset();
    // 斐波那契
    vector<string> fib = {"Func(fib, n)", "  if(n < 2)", "    return n", "  endif", "  return fib(n-1) + fib(n-2)", "EndFunc"};
    runLines(fib);
    CHECK_EQ(callFunc("fib",{"0"}), "0", "fib(0)");
    CHECK_EQ(callFunc("fib",{"1"}), "1", "fib(1)");
    CHECK_EQ(callFunc("fib",{"10"}), "55", "fib(10)");
    CHECK_EQ(callFunc("fib",{"15"}), "610", "fib(15)");
    reset();
    // return在控制结构内
    vector<string> testRet = {"Func(testRet)", "  if(1==1)", "    return 42", "  endif", "  return 99", "EndFunc"};
    runLines(testRet);
    CHECK_EQ(callFunc("testRet",{}), "42", "Bug#2: if内return");
    reset();
    vector<string> testFor = {"Func(testFor)", "  For(i, 1, 10)", "    if(i == 5)", "      return i * 100", "    endif", "  endfor", "  return 0", "EndFunc"};
    runLines(testFor);
    CHECK_EQ(callFunc("testFor",{}), "500", "Bug#2: For+if内return");
    reset();
    vector<string> testWhile = {"Func(testWhile)", "  x = 0", "  while(x < 100)", "    x = x + 1", "    if(x == 7)", "      return x", "    endif", "  endwhile", "  return -1", "EndFunc"};
    runLines(testWhile);
    CHECK_EQ(callFunc("testWhile",{}), "7", "Bug#2: while+if内return");
    reset();
    // 全局变量在函数内修改
    vector<string> modGlobal = {"Func(incr, x)", "  x = x + 1", "  return x", "EndFunc"};
    runLines(modGlobal);
    setVar("g","10");
    callFunc("incr",{"5"});
    // g不应该被修改(因为x是参数,函数内修改x不影响外部g)
    CHECK_EQ(getVar("g"), "10", "函数内修改参数不影响外部同名变量");
    reset();
}

void test_runCode_assignment(){
    cout<<"\n=== [runCode: 赋值] ==="<<endl;
    // 简单赋值
    runCode("x = 42"); CHECK_EQ(getVar("x"), "42", "简单赋值");
    runCode("x = 3+4"); CHECK_EQ(getVar("x"), "7", "表达式赋值");
    runCode("x = \"hello\""); CHECK_EQ(getVar("x"), "hello", "字符串赋值");
    runCode("y = x"); CHECK_EQ(getVar("y"), "hello", "变量赋值");
    runCode("z = (3+4)*2"); CHECK_EQ(getVar("z"), "14", "括号表达式赋值");
    // 带空格
    runCode("a = 1"); CHECK_EQ(getVar("a"), "1", "正常空格");
    runCode("b=2"); CHECK_EQ(getVar("b"), "2", "无空格");
    runCode("c   =   3"); CHECK_EQ(getVar("c"), "3", "多空格");
    reset();
    // 非法变量名
    runCode("123abc = 5"); CHECK(!hasVar("123abc"), "Bug#38: 数字开头变量名拒绝");
    runCode("a-b = 5"); CHECK(!hasVar("a-b"), "含减号变量名拒绝");
    runCode("@x = 5"); CHECK(!hasVar("@x"), "特殊符号变量名拒绝");
    reset();
    // 字符串内=不误判
    runCode("\"x=y\" = 5"); CHECK(!hasVar("x=y"), "Bug#23: 字符串内=不误判");
    reset();
    // 链式赋值(不支持,b=5被当作表达式)
    runCode("a = b = 5");
    CHECK_EQ(getVar("a"), "0", "Bug#22: 链式赋值a得0");
    reset();
    // 注释
    runCode("# 这是注释"); CHECK(g_vars.empty(), "注释行不执行");
    runCode(""); CHECK(g_vars.empty(), "空行不执行");
    runCode("   "); CHECK(g_vars.empty(), "纯空格不执行");
}

void test_runCode_compound(){
    cout<<"\n=== [runCode: 复合赋值] ==="<<endl;
    // += -= *= /=
    runCode("x = 10");
    runCode("x += 5"); CHECK_EQ(getVar("x"), "15", "+=");
    runCode("x -= 3"); CHECK_EQ(getVar("x"), "12", "-=");
    runCode("x *= 2"); CHECK_EQ(getVar("x"), "24", "*=");
    runCode("x /= 4"); CHECK_EQ(getVar("x"), "6", "/=");
    reset();
    // 未定义变量自动初始化
    runCode("y += 5"); CHECK_EQ(getVar("y"), "5", "Bug#10: 未定义变量+=");
    runCode("z -= 3"); CHECK_EQ(getVar("z"), "-3", "未定义变量-=");
    runCode("w *= 5"); CHECK_EQ(getVar("w"), "0", "未定义变量*=");
    runCode("v /= 5"); CHECK_EQ(getVar("v"), "0", "未定义变量/=");
    reset();
    // 复合赋值除零
    runCode("x = 10");
    runCode("x /= 0"); CHECK_EQ(getVar("x"), "0", "Bug#33: 复合赋值除零");
    reset();
    // 复合赋值带表达式
    runCode("x = 5");
    runCode("x += 3*2"); CHECK_EQ(getVar("x"), "11", "+=表达式");
    runCode("x -= 4/2"); CHECK_EQ(getVar("x"), "9", "-=表达式");
    reset();
}

void test_runCode_return(){
    cout<<"\n=== [runCode: return] ==="<<endl;
    runCode("return 42");
    CHECK(g_returning, "return设置g_returning");
    CHECK_EQ(getVar("__ret__"), "42", "return值");
    reset();
    runCode("return \"hello\"");
    CHECK_EQ(getVar("__ret__"), "hello", "return字符串");
    reset();
    runCode("return 3+4");
    CHECK_EQ(getVar("__ret__"), "7", "return表达式");
    reset();
    runCode("return");
    CHECK(g_returning, "空return设置标志");
    CHECK_EQ(getVar("__ret__"), "", "空return值");
    reset();
    runCode("return x");
    setVar("x","99");
    runCode("return x");
    CHECK_EQ(getVar("__ret__"), "99", "return变量");
    reset();
}

void test_runCode_builtins(){
    cout<<"\n=== [runCode: 内置函数] ==="<<endl;
    // PrintLog (用ASCII避免编码问题)
    g_logLines.clear();
    runCode("PrintLog(\"hello\")");
    CHECK_EQ(g_logLines.back(), "hello", "PrintLog单参数");
    g_logLines.clear();
    runCode("PrintLog(\"a\", \"b\", \"c\")");
    CHECK_EQ(g_logLines.back(), "a b c", "PrintLog多参数");
    g_logLines.clear();
    runCode("PrintLog(3+4)");
    CHECK_EQ(g_logLines.back(), "7", "PrintLog表达式");
    g_logLines.clear();
    runCode("PrintLog(\"Count:\", 42)");
    CHECK_EQ(g_logLines.back(), "Count: 42", "PrintLog混合参数");
    reset();
    // showAllBoxes
    g_logLines.clear();
    setVar("x","1"); setVar("y","2");
    runCode("showAllBoxes()");
    CHECK(g_logLines.size()>=2, "showAllBoxes输出多行");
    reset();
    // clearAllBoxes
    setVar("x","1"); setVar("y","2");
    runCode("clearAllBoxes()");
    CHECK(g_vars.empty(), "clearAllBoxes清空所有变量");
    reset();
    // showFuncs
    g_logLines.clear();
    vector<string> fdef = {"Func(foo, a)", "  return a", "EndFunc"};
    runLines(fdef);
    runCode("showFuncs()");
    CHECK(!g_logLines.empty(), "showFuncs输出函数列表");
    reset();
    // 未知函数
    g_logLines.clear();
    runCode("unknownFunc()");
    CHECK(!g_logLines.empty(), "未知函数输出错误");
    CHECK(g_logLines.back().find("unknownFunc")!=string::npos, "错误信息含函数名");
    reset();
    // 无法识别的语句
    g_logLines.clear();
    runCode("???");
    CHECK(!g_logLines.empty(), "无法识别语句输出错误");
}

void test_runCode_box(){
    cout<<"\n=== [runCode: box/boxS] ==="<<endl;
    // box赋值
    runCode("box(x, 42)");
    CHECK_EQ(getVar("x"), "42", "box赋值数字");
    reset();
    runCode("box(x, \"hello\")");
    CHECK_EQ(getVar("x"), "hello", "box赋值字符串");
    reset();
    runCode("box(x, 3+4)");
    CHECK_EQ(getVar("x"), "7", "box赋值表达式");
    reset();
    // boxS赋值
    runCode("boxS(\"label\", x, 42)");
    CHECK_EQ(getVar("x"), "42", "boxS赋值");
    reset();
    // box+Input (用ASCII)
    g_logLines.clear();
    runCode("box(x, Input(\"prompt:\"))");
    CHECK(g_waitingInput, "box+Input设置等待");
    CHECK_EQ(g_inputVarName, "x", "box+Input变量名");
    CHECK_EQ(g_inputPrompt, "prompt:", "box+Input提示");
    CHECK(g_inputType==0, "box+Input类型=0");
    reset();
    // box+InputInt
    runCode("box(x, InputInt(\"intprompt:\"))");
    CHECK(g_waitingInput, "box+InputInt设置等待");
    CHECK_EQ(g_inputVarName, "x", "box+InputInt变量名");
    CHECK_EQ(g_inputPrompt, "intprompt:", "box+InputInt提示");
    CHECK(g_inputType==1, "box+InputInt类型=1");
    reset();
    // boxS+Input
    runCode("boxS(\"label\", x, Input(\"prompt\"))");
    CHECK(g_waitingInput, "boxS+Input设置等待");
    CHECK_EQ(g_inputVarName, "x", "boxS+Input变量名");
    CHECK_EQ(g_inputPrompt, "prompt", "boxS+Input提示");
    reset();
}

void test_runCode_Input(){
    cout<<"\n=== [runCode: Input/InputInt] ==="<<endl;
    // Input (用ASCII)
    runCode("Input(\"prompt1:\", name)");
    CHECK(g_waitingInput, "Input设置等待");
    CHECK_EQ(g_inputPrompt, "prompt1:", "Input提示");
    CHECK_EQ(g_inputVarName, "name", "Input变量名");
    CHECK(g_inputType==0, "Input类型=0");
    reset();
    // InputInt
    runCode("InputInt(\"prompt2:\", age)");
    CHECK(g_waitingInput, "InputInt设置等待");
    CHECK_EQ(g_inputPrompt, "prompt2:", "InputInt提示");
    CHECK_EQ(g_inputVarName, "age", "InputInt变量名");
    CHECK(g_inputType==1, "InputInt类型=1");
    reset();
    // Input无参数
    runCode("Input()");
    CHECK(g_waitingInput, "Input()设置等待");
    CHECK_EQ(g_inputPrompt, "", "Input()空提示");
    CHECK_EQ(g_inputVarName, "", "Input()空变量名");
    reset();
    // Input单参数
    runCode("Input(\"onlyprompt\")");
    CHECK_EQ(g_inputPrompt, "onlyprompt", "Input单参数提示");
    CHECK_EQ(g_inputVarName, "", "Input单参数无变量名");
    reset();
}

void test_runLines_Func(){
    cout<<"\n=== [runLines: Func定义] ==="<<endl;
    // 基本函数定义
    vector<string> f1 = {"Func(add, a, b)", "  return a + b", "EndFunc"};
    runLines(f1);
    CHECK(hasVar("add")==false, "函数名不作为变量");
    CHECK(g_funcs.find("add")!=g_funcs.end(), "函数已注册");
    CHECK_EQ(callFunc("add",{"3","4"}), "7", "调用定义的函数");
    reset();
    // 无参数函数
    vector<string> f2 = {"Func(hello)", "  return \"world\"", "EndFunc"};
    runLines(f2);
    CHECK_EQ(callFunc("hello",{}), "world", "无参数函数");
    reset();
    // 多参数函数
    vector<string> f3 = {"Func(sum, a, b, c, d)", "  return a+b+c+d", "EndFunc"};
    runLines(f3);
    CHECK_EQ(callFunc("sum",{"1","2","3","4"}), "10", "四参数函数");
    reset();
    // 空函数体
    vector<string> f4 = {"Func(empty)", "EndFunc"};
    runLines(f4);
    CHECK_EQ(callFunc("empty",{}), "", "空函数体返回空");
    reset();
    // 函数内多语句
    vector<string> f5 = {
        "Func(complex, x)",
        "  y = x * 2",
        "  z = y + 1",
        "  return z",
        "EndFunc"
    };
    runLines(f5);
    CHECK_EQ(callFunc("complex",{"5"}), "11", "函数内多语句");
    CHECK(!hasVar("y"), "Bug#5: 函数内局部变量y不泄漏");
    CHECK(!hasVar("z"), "Bug#5: 函数内局部变量z不泄漏");
    reset();
    // 嵌套函数定义
    vector<string> f6 = {
        "Func(outer)",
        "  Func(inner, x)",
        "    return x * 2",
        "  EndFunc",
        "  return inner(5)",
        "EndFunc"
    };
    runLines(f6);
    CHECK_EQ(callFunc("outer",{}), "10", "嵌套函数定义");
    reset();
}

void test_runLines_if(){
    cout<<"\n=== [runLines: if/elseIf/else] ==="<<endl;
    // 基本if
    vector<string> f1 = {
        "Func(test, x)",
        "  if(x > 0)",
        "    return \"positive\"",
        "  endif",
        "  return \"non-positive\"",
        "EndFunc"
    };
    runLines(f1);
    CHECK_EQ(callFunc("test",{"5"}), "positive", "if真分支");
    CHECK_EQ(callFunc("test",{"-5"}), "non-positive", "if假分支");
    CHECK_EQ(callFunc("test",{"0"}), "non-positive", "if边界0");
    reset();
    // if-else
    vector<string> f2 = {
        "Func(test, x)",
        "  if(x > 0)",
        "    return \"pos\"",
        "  else",
        "    return \"neg\"",
        "  endif",
        "EndFunc"
    };
    runLines(f2);
    CHECK_EQ(callFunc("test",{"5"}), "pos", "if-else真");
    CHECK_EQ(callFunc("test",{"-5"}), "neg", "if-else假");
    reset();
    // if-elseIf-else
    vector<string> f3 = {
        "Func(test, x)",
        "  if(x == 1)",
        "    return \"one\"",
        "  elseIf(x == 2)",
        "    return \"two\"",
        "  elseIf(x == 3)",
        "    return \"three\"",
        "  else",
        "    return \"other\"",
        "  endif",
        "EndFunc"
    };
    runLines(f3);
    CHECK_EQ(callFunc("test",{"1"}), "one", "if-elseIf-else: 1");
    CHECK_EQ(callFunc("test",{"2"}), "two", "if-elseIf-else: 2");
    CHECK_EQ(callFunc("test",{"3"}), "three", "if-elseIf-else: 3");
    CHECK_EQ(callFunc("test",{"4"}), "other", "if-elseIf-else: other");
    reset();
    // 嵌套if
    vector<string> f4 = {
        "Func(test, x, y)",
        "  if(x > 0)",
        "    if(y > 0)",
        "      return \"both positive\"",
        "    else",
        "      return \"x positive, y not\"",
        "    endif",
        "  else",
        "    return \"x not positive\"",
        "  endif",
        "EndFunc"
    };
    runLines(f4);
    CHECK_EQ(callFunc("test",{"5","5"}), "both positive", "嵌套if: 都正");
    CHECK_EQ(callFunc("test",{"5","-5"}), "x positive, y not", "嵌套if: x正y负");
    CHECK_EQ(callFunc("test",{"-5","5"}), "x not positive", "嵌套if: x负");
    reset();
    // if条件为字符串比较
    vector<string> f5 = {
        "Func(test, s)",
        "  if(s == \"hello\")",
        "    return \"greeting\"",
        "  endif",
        "  return \"unknown\"",
        "EndFunc"
    };
    runLines(f5);
    CHECK_EQ(callFunc("test",{"hello"}), "greeting", "if字符串比较真");
    CHECK_EQ(callFunc("test",{"world"}), "unknown", "if字符串比较假");
    reset();
}

void test_runLines_For(){
    cout<<"\n=== [runLines: For循环] ==="<<endl;
    // 基本For
    vector<string> f1 = {
        "sum = 0",
        "For(i, 1, 5)",
        "  sum = sum + i",
        "endfor"
    };
    runLines(f1);
    CHECK_EQ(getVar("sum"), "15", "For求和1-5");
    CHECK_EQ(getVar("i"), "5", "For循环变量最终值");
    reset();
    // For步长
    vector<string> f2 = {
        "result = 0",
        "For(i, 0, 10, 2)",
        "  result = result + i",
        "endfor"
    };
    runLines(f2);
    CHECK_EQ(getVar("result"), "30", "For步长2: 0+2+4+6+8+10");
    reset();
    // For负步长
    vector<string> f3 = {
        "result = 0",
        "For(i, 5, 1, -1)",
        "  result = result * 10 + i",
        "endfor"
    };
    runLines(f3);
    CHECK_EQ(getVar("result"), "54321", "Bug#6: 负步长For");
    reset();
    // For空循环 (start>end, 正步长不执行)
    // 注: For(i,5,1) 默认 step=-1 (因为 start>end), 会执行递减
    // For(i,5,1,1) 显式正步长, start>end 不执行
    vector<string> f4 = {
        "result = 99",
        "For(i, 5, 1, 1)",
        "  result = 0",
        "endfor"
    };
    runLines(f4);
    CHECK_EQ(getVar("result"), "99", "For显式正步长+start>end不执行");
    reset();
    // For倒序默认
    vector<string> f5 = {
        "result = 0",
        "For(i, 3, 1)",
        "  result = result * 10 + i",
        "endfor"
    };
    runLines(f5);
    CHECK_EQ(getVar("result"), "321", "For默认负步长(start>end)");
    reset();
    // For嵌套
    vector<string> f6 = {
        "matrix = \"\"",
        "For(i, 1, 3)",
        "  For(j, 1, 3)",
        "    matrix = matrix + i + \"x\" + j + \" \"",
        "  endfor",
        "endfor"
    };
    runLines(f6);
    CHECK_EQ(getVar("matrix"), "1x1 1x2 1x3 2x1 2x2 2x3 3x1 3x2 3x3 ", "双层嵌套For");
    reset();
    // For中break
    vector<string> f7 = {
        "found = -1",
        "For(i, 1, 100)",
        "  if(i == 7)",
        "    found = i",
        "    break",
        "  endif",
        "endfor"
    };
    runLines(f7);
    CHECK_EQ(getVar("found"), "7", "For中break");
    CHECK_EQ(getVar("i"), "7", "break时循环变量值");
    reset();
    // For中return
    vector<string> f8 = {
        "Func(find, target)",
        "  For(i, 1, 100)",
        "    if(i == target)",
        "      return i",
        "    endif",
        "  endfor",
        "  return -1",
        "EndFunc"
    };
    runLines(f8);
    CHECK_EQ(callFunc("find",{"42"}), "42", "For中return");
    CHECK_EQ(callFunc("find",{"999"}), "-1", "For未找到return");
    reset();
    // For变量作为表达式
    vector<string> f9 = {
        "result = 1",
        "For(i, 1, 5)",
        "  result = result * i",
        "endfor"
    };
    runLines(f9);
    CHECK_EQ(getVar("result"), "120", "For阶乘");
    reset();
    // For计数
    vector<string> f10 = {
        "count = 0",
        "For(i, 1, 10)",
        "  count = count + 1",
        "endfor"
    };
    runLines(f10);
    CHECK_EQ(getVar("count"), "10", "For计数10次");
    reset();
    // For单次
    vector<string> f11 = {
        "result = 0",
        "For(i, 5, 5)",
        "  result = result + i",
        "endfor"
    };
    runLines(f11);
    CHECK_EQ(getVar("result"), "5", "For单次循环(起始==结束)");
    reset();
}

void test_runLines_while(){
    cout<<"\n=== [runLines: while循环] ==="<<endl;
    // 基本while
    vector<string> f1 = {
        "i = 0",
        "sum = 0",
        "while(i < 5)",
        "  i = i + 1",
        "  sum = sum + i",
        "endwhile"
    };
    runLines(f1);
    CHECK_EQ(getVar("sum"), "15", "while求和1-5");
    CHECK_EQ(getVar("i"), "5", "while终止条件");
    reset();
    // while条件不满足
    vector<string> f2 = {
        "result = 99",
        "while(1 > 2)",
        "  result = 0",
        "endwhile"
    };
    runLines(f2);
    CHECK_EQ(getVar("result"), "99", "while条件不满足不执行");
    reset();
    // while中break
    vector<string> f3 = {
        "i = 0",
        "while(i < 100)",
        "  i = i + 1",
        "  if(i == 10)",
        "    break",
        "  endif",
        "endwhile"
    };
    runLines(f3);
    CHECK_EQ(getVar("i"), "10", "while中break");
    reset();
    // while中return
    vector<string> f4 = {
        "Func(find, target)",
        "  i = 1",
        "  while(i < 100)",
        "    if(i == target)",
        "      return i",
        "    endif",
        "    i = i + 1",
        "  endwhile",
        "  return -1",
        "EndFunc"
    };
    runLines(f4);
    CHECK_EQ(callFunc("find",{"25"}), "25", "while中return");
    reset();
    // while嵌套
    vector<string> f5 = {
        "i = 0",
        "j = 0",
        "count = 0",
        "while(i < 3)",
        "  j = 0",
        "  while(j < 3)",
        "    count = count + 1",
        "    j = j + 1",
        "  endwhile",
        "  i = i + 1",
        "endwhile"
    };
    runLines(f5);
    CHECK_EQ(getVar("count"), "9", "嵌套while 3*3=9");
    reset();
    // while死循环(有break保护)
    vector<string> f6 = {
        "count = 0",
        "while(1)",
        "  count = count + 1",
        "  if(count >= 5)",
        "    break",
        "  endif",
        "endwhile"
    };
    runLines(f6);
    CHECK_EQ(getVar("count"), "5", "while(1)带break");
    reset();
}

void test_runLines_comments(){
    cout<<"\n=== [runLines: 注释和空行] ==="<<endl;
    vector<string> lines = {
        "# 这是注释",
        "",
        "   ",
        "x = 42",
        "  # 缩进注释",
        "y = x + 8"
    };
    runLines(lines);
    CHECK_EQ(getVar("x"), "42", "注释行后的赋值");
    CHECK_EQ(getVar("y"), "50", "第二行赋值");
    reset();
}

void test_integration(){
    cout<<"\n=== [集成测试] ==="<<endl;
    // 完整程序
    vector<string> prog = {
        "# 计算斐波那契",
        "Func(fib, n)",
        "  if(n < 2)",
        "    return n",
        "  endif",
        "  return fib(n-1) + fib(n-2)",
        "EndFunc",
        "",
        "result = fib(15)",
        "PrintLog(\"fib(15) = \", result)"
    };
    runLines(prog);
    CHECK_EQ(getVar("result"), "610", "集成: 斐波那契fib(15)");
    reset();
    // 排序程序
    vector<string> sortProg = {
        "Func(bubbleSort, n)",
        "  For(i, 0, 4)",
        "    For(j, 0, 4-i-1)",
        "      if(0==0)",  // 简化测试
        "      endif",
        "    endfor",
        "  endfor",
        "  return n",
        "EndFunc"
    };
    runLines(sortProg);
    CHECK_EQ(callFunc("bubbleSort",{"5"}), "5", "集成: 嵌套循环");
    reset();
    // 字符串处理 (callFunc 参数应传已解析的值)
    vector<string> strProg = {
        "Func(greet, name, times)",
        "  result = \"\"",
        "  For(i, 1, times)",
        "    result = result + \"Hello, \" + name + \"! \"",
        "  endfor",
        "  return result",
        "EndFunc"
    };
    runLines(strProg);
    CHECK_EQ(callFunc("greet",{"Alice","3"}), "Hello, Alice! Hello, Alice! Hello, Alice! ", "集成: 字符串重复拼接");
    reset();
    // 数学计算 (For(i,1,0) 默认负步长会执行递减 1,0 两次)
    // 显式正步长 For(i,1,0,1) 才是空循环
    vector<string> mathProg = {
        "Func(power, base, exp)",
        "  result = 1",
        "  For(i, 1, exp, 1)",
        "    result = result * base",
        "  endfor",
        "  return result",
        "EndFunc"
    };
    runLines(mathProg);
    CHECK_EQ(callFunc("power",{"2","10"}), "1024", "集成: 2^10");
    CHECK_EQ(callFunc("power",{"3","5"}), "243", "集成: 3^5");
    CHECK_EQ(callFunc("power",{"5","0"}), "1", "集成: 5^0 (空循环)");
    reset();
    // 条件组合
    vector<string> condProg = {
        "Func(grade, score)",
        "  if(score >= 90)",
        "    return \"A\"",
        "  elseIf(score >= 80)",
        "    return \"B\"",
        "  elseIf(score >= 70)",
        "    return \"C\"",
        "  elseIf(score >= 60)",
        "    return \"D\"",
        "  else",
        "    return \"F\"",
        "  endif",
        "EndFunc"
    };
    runLines(condProg);
    CHECK_EQ(callFunc("grade",{"95"}), "A", "集成: 成绩A");
    CHECK_EQ(callFunc("grade",{"85"}), "B", "集成: 成绩B");
    CHECK_EQ(callFunc("grade",{"75"}), "C", "集成: 成绩C");
    CHECK_EQ(callFunc("grade",{"65"}), "D", "集成: 成绩D");
    CHECK_EQ(callFunc("grade",{"55"}), "F", "集成: 成绩F");
    reset();
}

// ============================================================
// 恢复机制测试 (模拟多行模式 Input 中断与恢复)
// ============================================================

// 模拟提供输入并恢复执行的辅助函数
void simulateInput(const string& value) {
    if (!g_inputVarName.empty()) setVar(g_inputVarName, value);
    g_inputResult = value;  // 表达式模式结果
    g_waitingInput = false;
    g_inputPrompt.clear();
    flushPendingFrames();
    resumeExecution();
}

void test_resume_sequential(){
    cout<<"\n=== [恢复: 顺序Input] ==="<<endl;
    // 程序: PrintLog("start") -> Input("name:", x) -> PrintLog("hello", x) -> PrintLog("end")
    vector<string> prog = {
        "PrintLog(\"start\")",
        "Input(\"name:\", x)",
        "PrintLog(\"hello\", x)",
        "PrintLog(\"end\")"
    };
    g_logLines.clear();
    runLines(prog);
    // 应该在 Input 处中断，不打印 hello/end
    CHECK(g_waitingInput, "顺序Input: 等待输入");
    CHECK_EQ(g_inputVarName, "x", "顺序Input: 变量名");
    CHECK(g_logLines.size()>=1 && g_logLines[0]=="start", "顺序Input: start已输出");
    bool hasHello = false;
    for (auto& l : g_logLines) if (l.find("hello")!=string::npos) hasHello = true;
    CHECK(!hasHello, "顺序Input: 中断时未输出hello");
    // 模拟用户输入
    g_logLines.clear();
    simulateInput("Alice");
    CHECK(!g_waitingInput, "顺序Input: 输入后恢复");
    CHECK(g_resumeStack.empty(), "顺序Input: 恢复后队列空");
    // 检查 hello Alice 和 end 已输出
    bool foundHello = false, foundEnd = false;
    for (auto& l : g_logLines) {
        if (l == "hello Alice") foundHello = true;
        if (l == "end") foundEnd = true;
    }
    CHECK(foundHello, "顺序Input: 恢复后输出 hello Alice");
    CHECK(foundEnd, "顺序Input: 恢复后输出 end");
    reset();
}

void test_resume_multiple_input(){
    cout<<"\n=== [恢复: 多次Input] ==="<<endl;
    vector<string> prog = {
        "Input(\"first:\", a)",
        "Input(\"second:\", b)",
        "Input(\"third:\", c)",
        "PrintLog(a, b, c)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "多次Input: 第1次等待");
    CHECK_EQ(g_inputVarName, "a", "多次Input: 第1次变量a");
    simulateInput("X");
    CHECK(g_waitingInput, "多次Input: 第2次等待");
    CHECK_EQ(g_inputVarName, "b", "多次Input: 第2次变量b");
    simulateInput("Y");
    CHECK(g_waitingInput, "多次Input: 第3次等待");
    CHECK_EQ(g_inputVarName, "c", "多次Input: 第3次变量c");
    simulateInput("Z");
    CHECK(!g_waitingInput, "多次Input: 全部完成");
    CHECK(g_resumeStack.empty(), "多次Input: 队列空");
    CHECK(!g_logLines.empty() && g_logLines.back()=="X Y Z", "多次Input: 最终输出 X Y Z");
    reset();
}

void test_resume_for_loop(){
    cout<<"\n=== [恢复: For循环内Input] ==="<<endl;
    // For(i, 1, 3) { Input("x:", v); PrintLog(i, v) }
    vector<string> prog = {
        "result = \"\"",
        "For(i, 1, 3)",
        "  Input(\"enter:\", v)",
        "  result = result + v + \" \"",
        "endfor",
        "PrintLog(\"final:\", result)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "For内Input: 第1次中断(i=1)");
    simulateInput("A");
    CHECK(g_waitingInput, "For内Input: 第2次中断(i=2)");
    simulateInput("B");
    CHECK(g_waitingInput, "For内Input: 第3次中断(i=3)");
    simulateInput("C");
    CHECK(!g_waitingInput, "For内Input: 循环完成");
    CHECK(g_resumeStack.empty(), "For内Input: 队列空");
    CHECK_EQ(getVar("result"), "A B C ", "For内Input: 累积结果 A B C ");
    bool foundFinal = false;
    for (auto& l : g_logLines) if (l == "final: A B C ") foundFinal = true;
    CHECK(foundFinal, "For内Input: 最终输出 final: A B C ");
    reset();
}

void test_resume_while_loop(){
    cout<<"\n=== [恢复: while循环内Input] ==="<<endl;
    // i=0; while(i<3) { Input("v:", x); sum = sum + x; i = i + 1 } endwhile; PrintLog(sum)
    vector<string> prog = {
        "i = 0",
        "sum = 0",
        "while(i < 3)",
        "  Input(\"num:\", x)",
        "  sum = sum + x",
        "  i = i + 1",
        "endwhile",
        "PrintLog(\"sum=\", sum)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "while内Input: 第1次中断");
    simulateInput("10");
    CHECK(g_waitingInput, "while内Input: 第2次中断");
    simulateInput("20");
    CHECK(g_waitingInput, "while内Input: 第3次中断");
    simulateInput("30");
    CHECK(!g_waitingInput, "while内Input: 循环完成");
    CHECK(g_resumeStack.empty(), "while内Input: 队列空");
    bool foundSum = false;
    for (auto& l : g_logLines) if (l == "sum= 60") foundSum = true;
    CHECK(foundSum, "while内Input: sum=10+20+30=60");
    reset();
}

void test_resume_if_block(){
    cout<<"\n=== [恢复: if块内Input] ==="<<endl;
    vector<string> prog = {
        "x = 5",
        "if(x > 0)",
        "  Input(\"positive:\", y)",
        "  PrintLog(\"got\", y)",
        "endif",
        "PrintLog(\"done\")"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "if内Input: 中断");
    simulateInput("42");
    CHECK(!g_waitingInput, "if内Input: 恢复");
    CHECK(g_resumeStack.empty(), "if内Input: 队列空");
    bool foundGot = false, foundDone = false;
    for (auto& l : g_logLines) {
        if (l == "got 42") foundGot = true;
        if (l == "done") foundDone = true;
    }
    CHECK(foundGot, "if内Input: 恢复后输出 got 42");
    CHECK(foundDone, "if内Input: 恢复后输出 done");
    reset();
}

void test_resume_nested_for(){
    cout<<"\n=== [恢复: 嵌套For循环内Input] ==="<<endl;
    // For(i,1,2) { For(j,1,2) { Input("v:",x); PrintLog(i,j,x) } endfor } endfor
    vector<string> prog = {
        "result = \"\"",
        "For(i, 1, 2)",
        "  For(j, 1, 2)",
        "    Input(\"v:\", x)",
        "    result = result + i + \",\" + j + \",\" + x + \" \"",
        "  endfor",
        "endfor",
        "PrintLog(result)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "嵌套For: 第1次中断(1,1)");
    simulateInput("a");
    CHECK(g_waitingInput, "嵌套For: 第2次中断(1,2)");
    simulateInput("b");
    CHECK(g_waitingInput, "嵌套For: 第3次中断(2,1)");
    simulateInput("c");
    CHECK(g_waitingInput, "嵌套For: 第4次中断(2,2)");
    simulateInput("d");
    CHECK(!g_waitingInput, "嵌套For: 全部完成");
    CHECK(g_resumeStack.empty(), "嵌套For: 队列空");
    CHECK_EQ(getVar("result"), "1,1,a 1,2,b 2,1,c 2,2,d ", "嵌套For: 4次Input结果正确");
    reset();
}

void test_resume_input_then_code(){
    cout<<"\n=== [恢复: Input后继续执行复杂逻辑] ==="<<endl;
    vector<string> prog = {
        "Input(\"n:\", n)",
        "fact = 1",
        "For(i, 1, n)",
        "  fact = fact * i",
        "endfor",
        "PrintLog(\"factorial=\", fact)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(g_waitingInput, "Input后逻辑: 等待输入n");
    simulateInput("5");
    CHECK(!g_waitingInput, "Input后逻辑: 恢复完成");
    bool foundFact = false;
    for (auto& l : g_logLines) if (l == "factorial= 120") foundFact = true;
    CHECK(foundFact, "Input后逻辑: 5!=120");
    reset();
}

void test_resume_no_input(){
    cout<<"\n=== [恢复: 无Input的程序] ==="<<endl;
    vector<string> prog = {
        "x = 10",
        "y = x * 2",
        "PrintLog(\"result\", y)"
    };
    g_logLines.clear();
    runLines(prog);
    CHECK(!g_waitingInput, "无Input: 不等待");
    CHECK(g_resumeStack.empty(), "无Input: 队列空");
    CHECK(g_pendingFrames.empty(), "无Input: 无pending帧");
    CHECK(!g_logLines.empty() && g_logLines.back()=="result 20", "无Input: 正常执行");
    reset();
}

// ============================================================
// 字符串函数测试 (20+)
// ============================================================
void test_string_functions(){
    cout<<"\n=== [字符串函数] ==="<<endl;
    string r;
    // strLen
    builtinCall("strLen", {"hello"}, r); CHECK_EQ(r, "5", "strLen(hello)");
    builtinCall("strLen", {""}, r); CHECK_EQ(r, "0", "strLen(空)");
    builtinCall("strLen", {"1234567890"}, r); CHECK_EQ(r, "10", "strLen(10字符)");
    // strUpper/strLower
    builtinCall("strUpper", {"hello"}, r); CHECK_EQ(r, "HELLO", "strUpper");
    builtinCall("strLower", {"WORLD"}, r); CHECK_EQ(r, "world", "strLower");
    builtinCall("strUpper", {"MiXeD"}, r); CHECK_EQ(r, "MIXED", "strUpper混合");
    // strSub
    builtinCall("strSub", {"hello", "1", "3"}, r); CHECK_EQ(r, "ell", "strSub(hello,1,3)");
    builtinCall("strSub", {"hello", "0", "2"}, r); CHECK_EQ(r, "he", "strSub前2字符");
    builtinCall("strSub", {"hello", "3"}, r); CHECK_EQ(r, "lo", "strSub从3到末尾");
    builtinCall("strSub", {"hello", "0", "100"}, r); CHECK_EQ(r, "hello", "strSub越界自动截断");
    // strCat
    builtinCall("strCat", {"a","b","c"}, r); CHECK_EQ(r, "abc", "strCat三参数");
    builtinCall("strCat", {"hello"," ","world"}, r); CHECK_EQ(r, "hello world", "strCat含空格");
    // strRep
    builtinCall("strRep", {"ab", "3"}, r); CHECK_EQ(r, "ababab", "strRep(ab,3)");
    builtinCall("strRep", {"x", "0"}, r); CHECK_EQ(r, "", "strRep 0次");
    // strFind/strRFind
    builtinCall("strFind", {"hello world", "world"}, r); CHECK_EQ(r, "6", "strFind");
    builtinCall("strFind", {"hello", "xyz"}, r); CHECK_EQ(r, "-1", "strFind未找到");
    builtinCall("strRFind", {"ababab", "ab"}, r); CHECK_EQ(r, "4", "strRFind最后位置");
    // strReplace
    builtinCall("strReplace", {"hello world", "world", "there"}, r); CHECK_EQ(r, "hello there", "strReplace");
    builtinCall("strReplace", {"aaa", "a", "bb"}, r); CHECK_EQ(r, "bbbbbb", "strReplace多个");
    // strTrim
    builtinCall("strTrim", {"  hello  "}, r); CHECK_EQ(r, "hello", "strTrim两端空格");
    builtinCall("strTrim", {"\thello\t"}, r); CHECK_EQ(r, "hello", "strTrim制表符");
    // strChar
    builtinCall("strChar", {"hello", "1"}, r); CHECK_EQ(r, "e", "strChar第1字符");
    builtinCall("strChar", {"hello", "0"}, r); CHECK_EQ(r, "h", "strChar第0字符");
    // strAscii/strChr
    builtinCall("strAscii", {"A"}, r); CHECK_EQ(r, "65", "strAscii(A)");
    builtinCall("strAscii", {"a"}, r); CHECK_EQ(r, "97", "strAscii(a)");
    builtinCall("strChr", {"65"}, r); CHECK_EQ(r, "A", "strChr(65)");
    builtinCall("strChr", {"97"}, r); CHECK_EQ(r, "a", "strChr(97)");
    // strReverse
    builtinCall("strReverse", {"hello"}, r); CHECK_EQ(r, "olleh", "strReverse");
    builtinCall("strReverse", {"abcde"}, r); CHECK_EQ(r, "edcba", "strReverse5字符");
    // strStartsWith/strEndsWith
    builtinCall("strStartsWith", {"hello world", "hello"}, r); CHECK_EQ(r, "true", "strStartsWith真");
    builtinCall("strStartsWith", {"hello", "world"}, r); CHECK_EQ(r, "false", "strStartsWith假");
    builtinCall("strEndsWith", {"hello.txt", ".txt"}, r); CHECK_EQ(r, "true", "strEndsWith真");
    builtinCall("strEndsWith", {"hello.txt", ".exe"}, r); CHECK_EQ(r, "false", "strEndsWith假");
    // strContains
    builtinCall("strContains", {"hello world", "world"}, r); CHECK_EQ(r, "true", "strContains真");
    builtinCall("strContains", {"hello", "xyz"}, r); CHECK_EQ(r, "false", "strContains假");
    // strCount
    builtinCall("strCount", {"ababab", "ab"}, r); CHECK_EQ(r, "3", "strCount(ab在ababab)");
    builtinCall("strCount", {"hello", "l"}, r); CHECK_EQ(r, "2", "strCount(l在hello)");
    builtinCall("strCount", {"hello", "z"}, r); CHECK_EQ(r, "0", "strCount未找到");
    // strLeft/strRight
    builtinCall("strLeft", {"hello", "2"}, r); CHECK_EQ(r, "he", "strLeft 2");
    builtinCall("strRight", {"hello", "2"}, r); CHECK_EQ(r, "lo", "strRight 2");
    // strToInt/strToFloat
    builtinCall("strToInt", {"42"}, r); CHECK_EQ(r, "42", "strToInt");
    builtinCall("strToInt", {"-5"}, r); CHECK_EQ(r, "-5", "strToInt负数");
    builtinCall("strToFloat", {"3.14"}, r); CHECK_EQ(r, "3.14", "strToFloat");
    // strSplit + strJoin
    builtinCall("strSplit", {"a,b,c", ","}, r);
    string listName = r;
    builtinCall("strJoin", {listName, "-"}, r); CHECK_EQ(r, "a-b-c", "strSplit+strJoin");
    reset();
}

// ============================================================
// 列表函数测试 (10+)
// ============================================================
void test_list_functions(){
    cout<<"\n=== [列表函数] ==="<<endl;
    string r;
    // listNew
    builtinCall("listNew", {"nums", "1", "2", "3"}, r);
    CHECK_EQ(r, "nums", "listNew返回列表名");
    CHECK(isListVar("nums"), "listNew创建列表");
    CHECK_EQ(getList("nums").size(), 3u, "listNew 3个元素");
    // listLen
    builtinCall("listLen", {"nums"}, r); CHECK_EQ(r, "3", "listLen");
    builtinCall("listLen", {"nonexistent"}, r); CHECK_EQ(r, "0", "listLen不存在列表");
    // listGet
    builtinCall("listGet", {"nums", "0"}, r); CHECK_EQ(r, "1", "listGet[0]");
    builtinCall("listGet", {"nums", "1"}, r); CHECK_EQ(r, "2", "listGet[1]");
    builtinCall("listGet", {"nums", "2"}, r); CHECK_EQ(r, "3", "listGet[2]");
    // listSet
    builtinCall("listSet", {"nums", "1", "20"}, r); CHECK_EQ(r, "20", "listSet返回新值");
    CHECK_EQ(getList("nums")[1], "20", "listSet修改成功");
    // listAppend
    builtinCall("listAppend", {"nums", "4"}, r);
    CHECK_EQ(getList("nums").size(), 4u, "listAppend后长度4");
    CHECK_EQ(getList("nums")[3], "4", "listAppend值正确");
    // listInsert
    builtinCall("listInsert", {"nums", "0", "0"}, r);
    CHECK_EQ(getList("nums").size(), 5u, "listInsert后长度5");
    CHECK_EQ(getList("nums")[0], "0", "listInsert值正确");
    // listRemove
    builtinCall("listRemove", {"nums", "0"}, r);
    CHECK_EQ(getList("nums").size(), 4u, "listRemove后长度4");
    CHECK_EQ(getList("nums")[0], "1", "listRemove后第一个元素");
    // listPop
    builtinCall("listPop", {"nums"}, r);
    CHECK_EQ(r, "4", "listPop返回弹出值");
    CHECK_EQ(getList("nums").size(), 3u, "listPop后长度3");
    // listFind
    builtinCall("listFind", {"nums", "20"}, r); CHECK_EQ(r, "1", "listFind找到");
    builtinCall("listFind", {"nums", "999"}, r); CHECK_EQ(r, "-1", "listFind未找到");
    // listContains
    builtinCall("listContains", {"nums", "3"}, r); CHECK_EQ(r, "true", "listContains真");
    builtinCall("listContains", {"nums", "999"}, r); CHECK_EQ(r, "false", "listContains假");
    // listSort
    builtinCall("listNew", {"unsorted", "3", "1", "4", "1", "5", "9", "2", "6"}, r);
    builtinCall("listSort", {"unsorted"}, r);
    CHECK_EQ(getList("unsorted")[0], "1", "listSort第一个");
    CHECK_EQ(getList("unsorted")[7], "9", "listSort最后一个");
    // listReverse
    builtinCall("listReverse", {"unsorted"}, r);
    CHECK_EQ(getList("unsorted")[0], "9", "listReverse第一个");
    // listSum
    builtinCall("listNew", {"sums", "1", "2", "3", "4", "5"}, r);
    builtinCall("listSum", {"sums"}, r); CHECK_EQ(r, "15", "listSum 1+2+3+4+5=15");
    // listJoin
    builtinCall("listJoin", {"sums", ","}, r); CHECK_EQ(r, "1,2,3,4,5", "listJoin逗号分隔");
    builtinCall("listJoin", {"sums", ""}, r); CHECK_EQ(r, "12345", "listJoin空分隔");
    // listCopy
    builtinCall("listCopy", {"sums", "copied"}, r);
    CHECK_EQ(r, "copied", "listCopy返回目标名");
    CHECK_EQ(getList("copied").size(), 5u, "listCopy复制5个元素");
    // listClear
    builtinCall("listClear", {"copied"}, r);
    CHECK_EQ(getList("copied").size(), 0u, "listClear后为空");
    // listPrint
    g_logLines.clear();
    builtinCall("listPrint", {"sums"}, r);
    CHECK_EQ(r, "[1, 2, 3, 4, 5]", "listPrint格式");
    CHECK(!g_logLines.empty(), "listPrint输出到日志");
    reset();
}

// ============================================================
// 错误系统测试
// ============================================================
void test_error_system(){
    cout<<"\n=== [错误系统] ==="<<endl;
    clearErrors();
    CHECK(!hasErrors(), "初始无错误");
    g_currentLineNo = 5;
    reportError(ERR_SYNTAX, "test syntax error", "context1");
    CHECK(hasErrors(), "报告错误后有错误");
    CHECK_EQ(g_errors.size(), 1u, "1个错误");
    CHECK_EQ(g_errors[0].code, ERR_SYNTAX, "错误码正确");
    CHECK_EQ(g_errors[0].line, 5, "行号正确");
    CHECK_EQ(g_errors[0].context, "context1", "上下文正确");
    g_currentLineNo = 10;
    reportError(ERR_DIV_ZERO, "division by zero");
    CHECK_EQ(g_errors.size(), 2u, "2个错误");
    CHECK_EQ(g_errors[1].line, 10, "第二个错误行号");
    // 集成测试: 非法变量名产生错误
    reset();
    runCode("123abc = 5");
    CHECK(hasErrors(), "非法变量名产生错误");
    // 集成测试: 未定义函数作为语句调用产生错误
    reset();
    runCode("nonexistentFunc()");
    CHECK(hasErrors(), "未定义函数语句产生错误");
    reset();
}

// ============================================================
// 集成测试: 字符串+列表在完整程序中
// ============================================================
void test_integration_str_list(){
    cout<<"\n=== [集成: 字符串+列表] ==="<<endl;
    vector<string> prog = {
        "# 字符串和列表集成测试",
        "listNew(nums, 5, 3, 8, 1, 9, 2, 7, 4, 6)",
        "listSort(nums)",
        "PrintLog(\"Sorted:\", listJoin(nums, \",\"))",
        "PrintLog(\"Sum:\", listSum(nums))",
        "PrintLog(\"Len:\", listLen(nums))",
        "PrintLog(\"Min:\", listGet(nums, 0))",
        "PrintLog(\"Max:\", listGet(nums, 8))",
        "s = \"Hello World\"",
        "PrintLog(\"Upper:\", strUpper(s))",
        "PrintLog(\"Len:\", strLen(s))",
        "PrintLog(\"Sub:\", strSub(s, 0, 5))",
        "PrintLog(\"Find:\", strFind(s, \"World\"))",
        "PrintLog(\"Replace:\", strReplace(s, \"World\", \"There\"))"
    };
    runLines(prog);
    bool foundSorted=false, foundSum=false, foundUpper=false, foundReplace=false;
    for(auto&l:g_logLines){
        if(l.find("Sorted: 1,2,3,4,5,6,7,8,9")!=string::npos)foundSorted=true;
        if(l=="Sum: 45")foundSum=true;
        if(l=="Upper: HELLO WORLD")foundUpper=true;
        if(l=="Replace: Hello There")foundReplace=true;
    }
    CHECK(foundSorted, "集成: 排序输出");
    CHECK(foundSum, "集成: 求和45");
    CHECK(foundUpper, "集成: 大写转换");
    CHECK(foundReplace, "集成: 字符串替换");
    reset();
}

// ============================================================
// 常量系统测试 (CboxS)
// ============================================================
void test_const_system(){
    cout<<"\n=== [常量系统 CboxS] ==="<<endl;
    string r;
    // CboxS 定义常量
    builtinCall("CboxS", {"PI", "3.14"}, r);
    CHECK_EQ(r, "PI", "CboxS定义PI");
    CHECK(isConstVar("PI"), "PI是常量");
    CHECK_EQ(getConst("PI"), "3.14", "getConst(PI)");
    // 常量可通过变量名读取
    CHECK_EQ(getVar("PI"), "3.14", "getVar读取常量");
    CHECK(hasVar("PI"), "hasVar识别常量");
    // 常量不可修改
    setVar("PI", "999");
    CHECK_EQ(getConst("PI"), "3.14", "常量PI不可修改");
    CHECK(hasErrors(), "修改常量产生错误");
    clearErrors();
    // 重复定义报错
    builtinCall("CboxS", {"PI", "2.0"}, r);
    CHECK(hasErrors(), "重复定义常量报错");
    clearErrors();
    // getCbox
    builtinCall("getCbox", {"PI"}, r); CHECK_EQ(r, "3.14", "getCbox(PI)");
    builtinCall("getCbox", {"UNDEFINED"}, r); CHECK_EQ(r, "", "getCbox未定义返回空");
    // isCbox
    builtinCall("isCbox", {"PI"}, r); CHECK_EQ(r, "true", "isCbox(PI)=true");
    builtinCall("isCbox", {"x"}, r); CHECK_EQ(r, "false", "isCbox(x)=false");
    // 字符串常量
    builtinCall("CboxS", {"GREETING", "Hello"}, r);
    builtinCall("getCbox", {"GREETING"}, r); CHECK_EQ(r, "Hello", "字符串常量");
    // 在表达式中使用常量
    reset();
    builtinCall("CboxS", {"PI", "3.14"}, r);
    runCode("area = PI * 2 * 2");
    CHECK_EQ(getVar("area"), "12.56", "常量在表达式中使用");
    // eraseVar 不能删除常量
    reset();
    builtinCall("CboxS", {"X", "10"}, r);
    eraseVar("X");
    CHECK(isConstVar("X"), "常量不可删除");
    CHECK(hasErrors(), "删除常量产生错误");
    reset();
}

// ============================================================
// 字典系统测试 (Dict)
// ============================================================
void test_dict_system(){
    cout<<"\n=== [字典系统 Dict] ==="<<endl;
    string r;
    // dictNew
    builtinCall("dictNew", {"scores"}, r);
    CHECK_EQ(r, "scores", "dictNew返回字典名");
    CHECK(isDictVar("scores"), "dictNew创建字典");
    CHECK_EQ(getDict("scores").size(), 0u, "新字典为空");
    // dictSet
    builtinCall("dictSet", {"scores", "Alice", "95"}, r);
    builtinCall("dictSet", {"scores", "Bob", "87"}, r);
    builtinCall("dictSet", {"scores", "Carol", "92"}, r);
    CHECK_EQ(getDict("scores").size(), 3u, "dictSet后3个键值对");
    // dictGet
    builtinCall("dictGet", {"scores", "Alice"}, r); CHECK_EQ(r, "95", "dictGet(Alice)");
    builtinCall("dictGet", {"scores", "Bob"}, r); CHECK_EQ(r, "87", "dictGet(Bob)");
    builtinCall("dictGet", {"scores", "Carol"}, r); CHECK_EQ(r, "92", "dictGet(Carol)");
    builtinCall("dictGet", {"scores", "Unknown"}, r); CHECK_EQ(r, "", "dictGet未定义键返回空");
    // dictHas
    builtinCall("dictHas", {"scores", "Alice"}, r); CHECK_EQ(r, "true", "dictHas(Alice)=true");
    builtinCall("dictHas", {"scores", "Unknown"}, r); CHECK_EQ(r, "false", "dictHas(Unknown)=false");
    // dictLen
    builtinCall("dictLen", {"scores"}, r); CHECK_EQ(r, "3", "dictLen=3");
    // dictKeys
    builtinCall("dictKeys", {"scores"}, r);
    string keysList = r;
    CHECK(isListVar(keysList), "dictKeys返回列表");
    CHECK_EQ(getList(keysList).size(), 3u, "dictKeys 3个键");
    // dictValues
    builtinCall("dictValues", {"scores"}, r);
    string valsList = r;
    CHECK(isListVar(valsList), "dictValues返回列表");
    CHECK_EQ(getList(valsList).size(), 3u, "dictValues 3个值");
    // dictRemove
    builtinCall("dictRemove", {"scores", "Bob"}, r);
    CHECK_EQ(getDict("scores").size(), 2u, "dictRemove后2个");
    builtinCall("dictHas", {"scores", "Bob"}, r); CHECK_EQ(r, "false", "删除后Bob不存在");
    // dictCopy
    builtinCall("dictCopy", {"scores", "backup"}, r);
    CHECK(isDictVar("backup"), "dictCopy创建backup");
    CHECK_EQ(getDict("backup").size(), 2u, "backup有2个键值对");
    builtinCall("dictGet", {"backup", "Alice"}, r); CHECK_EQ(r, "95", "backup中Alice=95");
    // dictMerge
    builtinCall("dictNew", {"extra"}, r);
    builtinCall("dictSet", {"extra", "Dave", "88"}, r);
    builtinCall("dictSet", {"extra", "Alice", "100"}, r);  // 覆盖Alice
    builtinCall("dictMerge", {"scores", "extra"}, r);
    builtinCall("dictGet", {"scores", "Dave"}, r); CHECK_EQ(r, "88", "dictMerge新增Dave");
    builtinCall("dictGet", {"scores", "Alice"}, r); CHECK_EQ(r, "100", "dictMerge覆盖Alice");
    builtinCall("dictLen", {"scores"}, r); CHECK_EQ(r, "3", "dictMerge后3个");
    // dictPrint
    g_logLines.clear();
    builtinCall("dictPrint", {"scores"}, r);
    CHECK(!g_logLines.empty(), "dictPrint输出到日志");
    CHECK(r.find("{")!=string::npos && r.find("}")!=string::npos, "dictPrint格式含大括号");
    // dictClear
    builtinCall("dictClear", {"backup"}, r);
    CHECK_EQ(getDict("backup").size(), 0u, "dictClear后为空");
    // 集成测试: 字典在程序中使用
    reset();
    vector<string> prog = {
        "dictNew(grades)",
        "dictSet(grades, \"Alice\", 95)",
        "dictSet(grades, \"Bob\", 87)",
        "PrintLog(\"Alice:\", dictGet(grades, \"Alice\"))",
        "PrintLog(\"Count:\", dictLen(grades))"
    };
    runLines(prog);
    bool foundAlice=false, foundCount=false;
    for(auto&l:g_logLines){
        if(l=="Alice: 95")foundAlice=true;
        if(l=="Count: 2")foundCount=true;
    }
    CHECK(foundAlice, "集成: dictGet取值");
    CHECK(foundCount, "集成: dictLen计数");
    reset();
}

// ============================================================
// 表达式模式 Input 测试 (Input 作为函数参数)
// ============================================================
void test_input_in_expression(){
    cout<<"\n=== [表达式模式 Input] ==="<<endl;
    // 测试1: func(Input()) - 用户函数参数为 Input
    {
        vector<string> prog = {
            "Func(greet, name)",
            "  return \"hello\" + name",
            "EndFunc",
            "PrintLog(greet(Input()))"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "表达式Input: 等待输入");
        CHECK(g_inputPending, "表达式Input: g_inputPending=true");
        CHECK(g_inputVarName == "", "表达式Input: 无变量名");
        // 模拟输入 "World"
        g_logLines.clear();
        simulateInput("World");
        CHECK(!g_waitingInput, "表达式Input: 输入后恢复");
        CHECK(!g_inputPending, "表达式Input: pending已清除");
        // 检查输出 helloWorld
        bool found = false;
        for (auto& l : g_logLines) if (l == "helloWorld") found = true;
        CHECK(found, "表达式Input: 输出 helloWorld (非 helloInput())");
        // 确保没有错误输出 helloInput()
        bool noBadOutput = true;
        for (auto& l : g_logLines) if (l.find("Input()") != string::npos) noBadOutput = false;
        CHECK(noBadOutput, "表达式Input: 无 helloInput() 错误输出");
        reset();
    }
    // 测试2: Input(prompt) 带提示
    {
        vector<string> prog = {
            "Func(greet, name)",
            "  return \"Hi \" + name",
            "EndFunc",
            "PrintLog(greet(Input(\"Enter name:\")))"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "表达式Input带提示: 等待");
        CHECK_EQ(g_inputPrompt, "Enter name:", "表达式Input带提示: 提示正确");
        simulateInput("Bob");
        bool found = false;
        for (auto& l : g_logLines) if (l == "Hi Bob") found = true;
        CHECK(found, "表达式Input带提示: 输出 Hi Bob");
        reset();
    }
    // 测试3: Input() 直接在 PrintLog 中
    {
        vector<string> prog = {
            "PrintLog(Input())"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "PrintLog(Input()): 等待");
        simulateInput("test123");
        bool found = false;
        for (auto& l : g_logLines) if (l == "test123") found = true;
        CHECK(found, "PrintLog(Input()): 输出 test123");
        reset();
    }
    // 测试4: InputInt() 在表达式中
    {
        vector<string> prog = {
            "PrintLog(InputInt() + 10)"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "InputInt表达式: 等待");
        CHECK(g_inputType == 1, "InputInt表达式: 类型=1");
        simulateInput("5");
        bool found = false;
        for (auto& l : g_logLines) if (l == "15") found = true;
        CHECK(found, "InputInt表达式: 5+10=15");
        reset();
    }
    // 测试5: 多个 Input 在不同行(每行单个Input)
    {
        vector<string> prog = {
            "x = Input()",
            "y = Input()",
            "PrintLog(x + \" \" + y)"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "双Input(分行): 第1次等待");
        simulateInput("Hello");
        CHECK(g_waitingInput, "双Input(分行): 第2次等待");
        simulateInput("World");
        bool found = false;
        for (auto& l : g_logLines) if (l == "Hello World") found = true;
        CHECK(found, "双Input(分行): 输出 Hello World");
        reset();
    }
    // 测试6: Input() 在赋值右侧
    {
        vector<string> prog = {
            "x = Input(\"Value:\")",
            "PrintLog(\"got\", x)"
        };
        g_logLines.clear();
        runLines(prog);
        CHECK(g_waitingInput, "赋值Input: 等待");
        simulateInput("42");
        CHECK_EQ(getVar("x"), "42", "赋值Input: x=42");
        bool found = false;
        for (auto& l : g_logLines) if (l == "got 42") found = true;
        CHECK(found, "赋值Input: 输出 got 42");
        reset();
    }
}

// ============================================================
// 表达式模式完整测试 (覆盖每一种表达式)
// ============================================================
void test_expression_modes_complete(){
    cout<<"\n=== [表达式模式完整覆盖] ==="<<endl;
    string r;

    // ---- 1. 纯数字表达式 ----
    CHECK_EQ(evalExpr("42"), "42", "纯整数");
    CHECK_EQ(evalExpr("-5"), "-5", "负整数");
    CHECK_EQ(evalExpr("3.14"), "3.14", "浮点数");
    CHECK_EQ(evalExpr("1e10"), "1e10", "科学计数法");
    CHECK_EQ(evalExpr("0"), "0", "零");

    // ---- 2. 算术表达式 ----
    CHECK_EQ(evalExpr("3+4"), "7", "加法");
    CHECK_EQ(evalExpr("10-3"), "7", "减法");
    CHECK_EQ(evalExpr("3*4"), "12", "乘法");
    CHECK_EQ(evalExpr("12/4"), "3", "除法");
    CHECK_EQ(evalExpr("2+3*4"), "14", "优先级");
    CHECK_EQ(evalExpr("(2+3)*4"), "20", "括号");
    CHECK_EQ(evalExpr("5*-3"), "-15", "一元负号");
    CHECK_EQ(evalExpr("3 - -5"), "8", "双负号");
    CHECK_EQ(evalExpr("1e-5"), "1e-5", "科学计数法负指数");

    // ---- 3. 字符串字面量 ----
    CHECK_EQ(evalExpr("\"hello\""), "hello", "双引号字符串");
    CHECK_EQ(evalExpr("'world'"), "world", "单引号字符串");
    CHECK_EQ(evalExpr("\"\""), "", "空字符串");
    CHECK_EQ(evalExpr("\"a\\nb\""), "a\nb", "转义换行");
    CHECK_EQ(evalExpr("\"a\\tb\""), "a\tb", "转义制表符");

    // ---- 4. 字符串拼接 ----
    CHECK_EQ(evalExpr("\"a\"+\"b\""), "ab", "字符串+字符串");
    setVar("name","Alice");
    CHECK_EQ(evalExpr("\"Hello \"+name"), "Hello Alice", "字符串+变量");
    setVar("n","5");
    CHECK_EQ(evalExpr("n+3"), "8", "变量+数字(算术)");
    setVar("s","abc");
    CHECK_EQ(evalExpr("s+\"def\""), "abcdef", "变量+字符串(拼接)");
    reset();

    // ---- 5. 变量表达式 ----
    setVar("x","10");
    CHECK_EQ(evalExpr("x"), "10", "纯变量");
    CHECK_EQ(evalExpr("x+5"), "15", "变量+数字");
    CHECK_EQ(evalExpr("x*2"), "20", "变量*数字");
    CHECK_EQ(evalExpr("x/2"), "5", "变量/数字");
    reset();

    // ---- 6. 比较表达式 (cond) ----
    CHECK(cond("3==3"), "等于");
    CHECK(!cond("3==4"), "不等于");
    CHECK(cond("3>2"), "大于");
    CHECK(cond("2<3"), "小于");
    CHECK(cond("3>=3"), "大于等于");
    CHECK(cond("3<=3"), "小于等于");
    CHECK(cond("3!=4"), "不等");
    CHECK(cond("\"a\"==\"a\""), "字符串等于");
    CHECK(!cond("\"a\"==\"b\""), "字符串不等");
    CHECK(cond("true"), "true");
    CHECK(!cond("false"), "false");
    CHECK(cond("1"), "非零为真");
    CHECK(!cond("0"), "零为假");

    // ---- 7. 复合比较 ----
    CHECK(cond("3+2==5"), "表达式比较");
    CHECK(cond("(3+4)*2==14"), "括号表达式比较");
    CHECK(cond("2*3 > 5"), "表达式大于");

    // ---- 8. 用户函数调用 ----
    {
        vector<string> fdef = {"Func(add, a, b)", "  return a + b", "EndFunc"};
        runLines(fdef);
        CHECK_EQ(evalExpr("add(3, 4)"), "7", "函数调用");
        CHECK_EQ(evalExpr("add(10, 20)"), "30", "函数调用2");
        CHECK_EQ(evalExpr("add(add(1,2), 3)"), "6", "嵌套函数");
        reset();
    }

    // ---- 9. 内置字符串函数 ----
    CHECK_EQ(evalExpr("strLen(\"hello\")"), "5", "strLen");
    CHECK_EQ(evalExpr("strUpper(\"hello\")"), "HELLO", "strUpper");
    CHECK_EQ(evalExpr("strLower(\"WORLD\")"), "world", "strLower");
    CHECK_EQ(evalExpr("strSub(\"hello\", 0, 3)"), "hel", "strSub");
    CHECK_EQ(evalExpr("strCat(\"a\",\"b\")"), "ab", "strCat");
    CHECK_EQ(evalExpr("strRep(\"x\", 3)"), "xxx", "strRep");
    CHECK_EQ(evalExpr("strFind(\"hello\", \"ll\")"), "2", "strFind");
    CHECK_EQ(evalExpr("strReplace(\"aaa\", \"a\", \"b\")"), "bbb", "strReplace");
    CHECK_EQ(evalExpr("strReverse(\"abc\")"), "cba", "strReverse");
    CHECK_EQ(evalExpr("strTrim(\"  hi  \")"), "hi", "strTrim");
    CHECK_EQ(evalExpr("strAscii(\"A\")"), "65", "strAscii");
    CHECK_EQ(evalExpr("strChr(65)"), "A", "strChr");
    CHECK_EQ(evalExpr("strLeft(\"hello\", 2)"), "he", "strLeft");
    CHECK_EQ(evalExpr("strRight(\"hello\", 2)"), "lo", "strRight");
    CHECK_EQ(evalExpr("strContains(\"hello\", \"ell\")"), "true", "strContains");
    CHECK_EQ(evalExpr("strStartsWith(\"hello\", \"he\")"), "true", "strStartsWith");
    CHECK_EQ(evalExpr("strEndsWith(\"file.txt\", \".txt\")"), "true", "strEndsWith");
    CHECK_EQ(evalExpr("strCount(\"abab\", \"ab\")"), "2", "strCount");
    CHECK_EQ(evalExpr("strToInt(\"42\")"), "42", "strToInt");
    CHECK_EQ(evalExpr("strToFloat(\"3.14\")"), "3.14", "strToFloat");

    // ---- 10. 内置函数嵌套 ----
    CHECK_EQ(evalExpr("strUpper(strSub(\"hello\", 0, 3))"), "HEL", "strUpper(strSub)");
    CHECK_EQ(evalExpr("strLen(strCat(\"ab\", \"cd\"))"), "4", "strLen(strCat)");
    CHECK_EQ(evalExpr("strReverse(strUpper(\"abc\"))"), "CBA", "strReverse(strUpper)");

    // ---- 11. 列表函数 ----
    runCode("listNew(nums, 3, 1, 4, 1, 5)");
    CHECK_EQ(evalExpr("listLen(nums)"), "5", "listLen");
    CHECK_EQ(evalExpr("listGet(nums, 0)"), "3", "listGet");
    CHECK_EQ(evalExpr("listSum(nums)"), "14", "listSum");
    CHECK_EQ(evalExpr("listFind(nums, 4)"), "2", "listFind");
    CHECK_EQ(evalExpr("listContains(nums, 5)"), "true", "listContains");
    CHECK_EQ(evalExpr("listJoin(nums, \",\")"), "3,1,4,1,5", "listJoin");
    reset();

    // ---- 12. 字典函数 ----
    runCode("dictNew(d)");
    runCode("dictSet(d, \"key\", \"value\")");
    CHECK_EQ(evalExpr("dictGet(d, \"key\")"), "value", "dictGet");
    CHECK_EQ(evalExpr("dictHas(d, \"key\")"), "true", "dictHas");
    CHECK_EQ(evalExpr("dictLen(d)"), "1", "dictLen");
    reset();

    // ---- 13. 常量表达式 ----
    runCode("CboxS(PI, 3)");
    CHECK_EQ(evalExpr("PI"), "3", "常量直接引用");
    CHECK_EQ(evalExpr("PI*2"), "6", "常量在算术中");
    CHECK_EQ(evalExpr("getCbox(PI)"), "3", "getCbox");
    CHECK_EQ(evalExpr("isCbox(PI)"), "true", "isCbox");
    CHECK_EQ(evalExpr("isCbox(x)"), "false", "isCbox非常量");
    reset();

    // ---- 14. 复杂嵌套表达式 ----
    {
        vector<string> prog = {
            "Func(sq, x)",
            "  return x * x",
            "EndFunc",
            "Func(add, a, b)",
            "  return a + b",
            "EndFunc"
        };
        runLines(prog);
        CHECK_EQ(evalExpr("add(sq(3), sq(4))"), "25", "add(sq,sq)");
        CHECK_EQ(evalExpr("sq(add(2, 3))"), "25", "sq(add)");
        CHECK_EQ(evalExpr("strLen(strRep(\"ab\", 3))"), "6", "strLen(strRep)");
        reset();
    }

    // ---- 15. Input 表达式模式 ----
    {
        vector<string> prog = {"PrintLog(Input())"};
        runLines(prog);
        CHECK(g_waitingInput, "Input(): 等待");
        simulateInput("test");
        bool found = false;
        for (auto& l : g_logLines) if (l == "test") found = true;
        CHECK(found, "Input(): 输出test");
        reset();
    }
    {
        vector<string> prog = {
            "Func(greet, n)",
            "  return \"hi\" + n",
            "EndFunc",
            "PrintLog(greet(Input()))"
        };
        runLines(prog);
        CHECK(g_waitingInput, "func(Input()): 等待");
        simulateInput("Bob");
        bool found = false;
        for (auto& l : g_logLines) if (l == "hiBob") found = true;
        CHECK(found, "func(Input()): 输出hiBob");
        reset();
    }
    {
        vector<string> prog = {"PrintLog(InputInt() + 10)"};
        runLines(prog);
        CHECK(g_waitingInput, "InputInt()+10: 等待");
        simulateInput("5");
        bool found = false;
        for (auto& l : g_logLines) if (l == "15") found = true;
        CHECK(found, "InputInt()+10=15");
        reset();
    }
    {
        vector<string> prog = {"x = Input()"};
        runLines(prog);
        CHECK(g_waitingInput, "x=Input(): 等待");
        simulateInput("42");
        CHECK_EQ(getVar("x"), "42", "x=Input(): x=42");
        reset();
    }
    {
        vector<string> prog = {"PrintLog(strUpper(Input()))"};
        runLines(prog);
        CHECK(g_waitingInput, "strUpper(Input()): 等待");
        simulateInput("hello");
        bool found = false;
        for (auto& l : g_logLines) if (l == "HELLO") found = true;
        CHECK(found, "strUpper(Input()): HELLO");
        reset();
    }
    {
        vector<string> prog = {"PrintLog(strLen(Input()))"};
        runLines(prog);
        simulateInput("hello");
        bool found = false;
        for (auto& l : g_logLines) if (l == "5") found = true;
        CHECK(found, "strLen(Input()): 5");
        reset();
    }

    // ---- 16. 字符串中含特殊字符 ----
    CHECK_EQ(evalExpr("\"a>b\""), "a>b", "字符串含>");
    CHECK_EQ(evalExpr("\"a=b\""), "a=b", "字符串含=");
    CHECK_EQ(evalExpr("\"a+b\""), "a+b", "字符串含+");
    CHECK_EQ(evalExpr("\"a(b\""), "a(b", "字符串含(");

    // ---- 17. 空表达式 ----
    CHECK_EQ(evalExpr(""), "0", "空表达式返回0");
    CHECK_EQ(evalExpr("   "), "0", "纯空格返回0");

    // ---- 18. 混合类型拼接 ----
    setVar("n","5");
    setVar("s","hello");
    CHECK_EQ(evalExpr("s + \" \" + n"), "hello 5", "字符串+变量+数字变量");
    reset();

    // ---- 19. list[index] 下标访问 ----
    runCode("listNew(nums, 10, 20, 30, 40, 50)");
    CHECK_EQ(evalExpr("nums[0]"), "10", "list[0]");
    CHECK_EQ(evalExpr("nums[1]"), "20", "list[1]");
    CHECK_EQ(evalExpr("nums[4]"), "50", "list[4]");
    // 变量索引
    setVar("i", "2");
    CHECK_EQ(evalExpr("nums[i]"), "30", "list[变量]");
    // 表达式索引
    CHECK_EQ(evalExpr("nums[i+1]"), "40", "list[表达式]");
    // 下标在字符串拼接中
    CHECK_EQ(evalExpr("\"First: \" + nums[0]"), "First: 10", "list[]在拼接中");
    reset();

    // ---- 20. list[index] 赋值 ----
    runCode("listNew(nums, 10, 20, 30)");
    runCode("nums[1] = 99");
    CHECK_EQ(getList("nums")[1], "99", "list[1]=99赋值");
    runCode("nums[0] = nums[2]");
    CHECK_EQ(getList("nums")[0], "30", "list[0]=list[2]");
    // 用变量索引赋值
    setVar("i", "1");
    runCode("nums[i] = 77");
    CHECK_EQ(getList("nums")[1], "77", "list[变量]=77");
    reset();

    // ---- 21. dict[key] 下标访问 ----
    runCode("dictNew(d)");
    runCode("dictSet(d, \"name\", \"Alice\")");
    runCode("dictSet(d, \"age\", 25)");
    CHECK_EQ(evalExpr("d[\"name\"]"), "Alice", "dict[\"name\"]");
    CHECK_EQ(evalExpr("d[\"age\"]"), "25", "dict[\"age\"]");
    // 变量key
    setVar("k", "name");
    CHECK_EQ(evalExpr("d[k]"), "Alice", "dict[变量key]");
    // 下标在拼接中
    CHECK_EQ(evalExpr("d[\"name\"] + \" is \" + d[\"age\"]"), "Alice is 25", "dict[]在拼接中");
    reset();

    // ---- 22. dict[key] 赋值 ----
    runCode("dictNew(d)");
    runCode("d[\"x\"] = 100");
    CHECK_EQ(getDict("d")["x"], "100", "dict[\"x\"]=100赋值");
    runCode("d[\"y\"] = \"hello\"");
    CHECK_EQ(getDict("d")["y"], "hello", "dict[\"y\"]=\"hello\"");
    // 修改变量key
    setVar("k", "x");
    runCode("d[k] = 999");
    CHECK_EQ(getDict("d")["x"], "999", "dict[变量key]=999");
    reset();

    // ---- 23. 下标访问在完整程序中 ----
    {
        vector<string> prog = {
            "listNew(arr, 1, 2, 3, 4, 5)",
            "sum = 0",
            "For(i, 0, 4)",
            "  sum = sum + arr[i]",
            "endfor",
            "PrintLog(\"Sum:\", sum)"
        };
        runLines(prog);
        bool found = false;
        for (auto& l : g_logLines) if (l == "Sum: 15") found = true;
        CHECK(found, "集成: For循环中arr[i]求和=15");
        reset();
    }
    {
        vector<string> prog = {
            "dictNew(scores)",
            "scores[\"Alice\"] = 95",
            "scores[\"Bob\"] = 87",
            "PrintLog(\"Alice:\", scores[\"Alice\"])",
            "PrintLog(\"Bob:\", scores[\"Bob\"])"
        };
        runLines(prog);
        bool foundA = false, foundB = false;
        for (auto& l : g_logLines) {
            if (l == "Alice: 95") foundA = true;
            if (l == "Bob: 87") foundB = true;
        }
        CHECK(foundA, "集成: dict[]赋值+读取Alice");
        CHECK(foundB, "集成: dict[]赋值+读取Bob");
        reset();
    }
}

// ============================================================
// 库导入系统测试 (GetPack)
// ============================================================
void test_pack_system(){
    cout<<"\n=== [库导入 GetPack] ==="<<endl;
    // 注册一个测试库 "mathlib"
    setPackSource("mathlib", {
        "# mathlib.sal - 数学库",
        "CboxS(PI, 3.14159)",
        "Func(square, x)",
        "  return x * x",
        "EndFunc",
        "Func(cube, x)",
        "  return x * x * x",
        "EndFunc",
        "Func(add, a, b)",
        "  return a + b",
        "EndFunc"
    });
    // 注册另一个库 "strlib"
    setPackSource("strlib", {
        "# strlib.sal - 字符串库",
        "Func(shout, s)",
        "  return strUpper(s) + \"!!!\"",
        "EndFunc",
        "Func(repeat3, s)",
        "  return s + s + s",
        "EndFunc"
    });

    // 测试1: 基本导入
    {
        vector<string> prog = {
            "GetPack(\"mathlib\")",
            "PrintLog(square(5))",
            "PrintLog(cube(3))",
            "PrintLog(add(10, 20))",
            "PrintLog(PI)"
        };
        runLines(prog);
        bool foundSq=false, foundCube=false, foundAdd=false, foundPI=false;
        for(auto&l:g_logLines){
            if(l=="25")foundSq=true;
            if(l=="27")foundCube=true;
            if(l=="30")foundAdd=true;
            if(l=="3.14159")foundPI=true;
        }
        CHECK(foundSq, "库: square(5)=25");
        CHECK(foundCube, "库: cube(3)=27");
        CHECK(foundAdd, "库: add(10,20)=30");
        CHECK(foundPI, "库: 常量PI=3.14159");
        reset();
    }

    // 测试2: 导入多个库
    {
        vector<string> prog = {
            "GetPack(\"mathlib\")",
            "GetPack(\"strlib\")",
            "PrintLog(square(4))",
            "PrintLog(shout(\"hello\"))",
            "PrintLog(repeat3(\"ab\"))"
        };
        runLines(prog);
        bool foundSq=false, foundShout=false, foundRep=false;
        for(auto&l:g_logLines){
            if(l=="16")foundSq=true;
            if(l=="HELLO!!!")foundShout=true;
            if(l=="ababab")foundRep=true;
        }
        CHECK(foundSq, "多库: mathlib.square(4)=16");
        CHECK(foundShout, "多库: strlib.shout=HELLO!!!");
        CHECK(foundRep, "多库: strlib.repeat3=ababab");
        reset();
    }

    // 测试3: 重复导入只加载一次
    {
        vector<string> prog = {
            "GetPack(\"mathlib\")",
            "GetPack(\"mathlib\")",  // 重复导入
            "PrintLog(square(6))"
        };
        g_logLines.clear();
        runLines(prog);
        // 应该只有一条 [Pack] loaded
        int packCount = 0;
        for(auto&l:g_logLines) if(l.find("[Pack] loaded: mathlib")!=string::npos) packCount++;
        CHECK(packCount == 1, "库: 重复导入只加载一次");
        bool foundSq = false;
        for(auto&l:g_logLines) if(l=="36") foundSq = true;
        CHECK(foundSq, "库: 重复导入后函数仍可用 square(6)=36");
        reset();
    }

    // 测试4: 库中函数互相调用
    {
        setPackSource("geometry", {
            "Func(area, r)",
            "  return PI * r * r",
            "EndFunc",
            "Func(circumference, r)",
            "  return 2 * PI * r",
            "EndFunc"
        });
        vector<string> prog = {
            "GetPack(\"mathlib\")",  // 提供 PI
            "GetPack(\"geometry\")", // 提供 area/circumference
            "PrintLog(area(2))",
            "PrintLog(circumference(2))"
        };
        runLines(prog);
        bool foundArea=false, foundCirc=false;
        for(auto&l:g_logLines){
            if(l.find("12.566")!=string::npos) foundArea=true;  // PI*4 ≈ 12.566
            if(l.find("12.566")!=string::npos) foundCirc=true;  // 2*PI*2 ≈ 12.566
        }
        CHECK(foundArea, "库间调用: area(2)≈12.566");
        CHECK(foundCirc, "库间调用: circumference(2)≈12.566");
        reset();
    }

    // 测试5: 导入不存在的库
    {
        vector<string> prog = {
            "GetPack(\"nonexistent\")",
            "PrintLog(\"after\")"
        };
        runLines(prog);
        CHECK(hasErrors(), "库: 导入不存在的库报错");
        bool foundAfter = false;
        for(auto&l:g_logLines) if(l=="after") foundAfter = true;
        CHECK(foundAfter, "库: 导入失败后继续执行");
        reset();
    }

    // 测试6: 库中定义的常量在主程序中使用
    {
        vector<string> prog = {
            "GetPack(\"mathlib\")",
            "r = 5",
            "area = PI * r * r",
            "PrintLog(\"Area:\", area)"
        };
        runLines(prog);
        bool found = false;
        for(auto&l:g_logLines) if(l.find("78.5")!=string::npos) found = true;  // PI*25 ≈ 78.54
        CHECK(found, "库: 常量在主程序计算中 Area≈78.5");
        reset();
    }

    // 清理 pack sources
    g_packSources.clear();
}

// ============================================================
// C++ 库扩展测试 (GetCPack)
// ============================================================

// 模拟C++原生函数
const char* native_fastAdd(const char** args, int n) {
    if (n < 2) return "0";
    int a = atoi(args[0]), b = atoi(args[1]);
    static char buf[32]; sprintf(buf, "%d", a + b);
    return buf;
}
const char* native_fastMul(const char** args, int n) {
    if (n < 2) return "0";
    int a = atoi(args[0]), b = atoi(args[1]);
    static char buf[32]; sprintf(buf, "%d", a * b);
    return buf;
}
const char* native_greeting(const char** args, int n) {
    if (n < 1) return "Hello!";
    static char buf[256]; sprintf(buf, "Hello, %s!", args[0]);
    return buf;
}

void test_cpack_system(){
    cout<<"\n=== [C++库 GetCPack] ==="<<endl;
    // 先注册模拟的C++函数 (模拟DLL加载后的状态)
    registerNativeFunc("fastAdd", native_fastAdd);
    registerNativeFunc("fastMul", native_fastMul);
    registerNativeFunc("greeting", native_greeting);

    // 测试1: 直接调用注册的C++函数
    {
        string r;
        builtinCall("fastAdd", {"3", "4"}, r);
        CHECK_EQ(r, "7", "C++函数: fastAdd(3,4)=7");
        builtinCall("fastMul", {"5", "6"}, r);
        CHECK_EQ(r, "30", "C++函数: fastMul(5,6)=30");
        builtinCall("greeting", {"World"}, r);
        CHECK_EQ(r, "Hello, World!", "C++函数: greeting(World)");
    }

    // 测试2: 在表达式中使用C++函数
    CHECK_EQ(evalExpr("fastAdd(10, 20)"), "30", "表达式: fastAdd(10,20)");
    CHECK_EQ(evalExpr("fastMul(3, 4)"), "12", "表达式: fastMul(3,4)");
    CHECK_EQ(evalExpr("fastAdd(1, 2) + fastAdd(3, 4)"), "10", "表达式: fastAdd+fastAdd");

    // 测试3: 在完整程序中使用
    {
        vector<string> prog = {
            "GetCPack(\"testmath\")",
            "PrintLog(fastAdd(100, 200))",
            "PrintLog(fastMul(7, 8))",
            "PrintLog(greeting(\"User\"))"
        };
        runLines(prog);
        bool found1=false, found2=false, found3=false;
        for (auto& l : g_logLines) {
            if (l == "300") found1 = true;
            if (l == "56") found2 = true;
            if (l == "Hello, User!") found3 = true;
        }
        CHECK(found1, "程序: fastAdd(100,200)=300");
        CHECK(found2, "程序: fastMul(7,8)=56");
        CHECK(found3, "程序: greeting(User)=Hello, User!");
        reset();
    }

    // 测试4: GetCPack 重复加载
    {
        registerNativeFunc("fastAdd", native_fastAdd);
        vector<string> prog = {
            "GetCPack(\"testmath\")",
            "GetCPack(\"testmath\")",
            "PrintLog(fastAdd(1, 1))"
        };
        runLines(prog);
        int cpackCount = 0;
        for (auto& l : g_logLines) if (l.find("[CPack] loaded: testmath") != string::npos) cpackCount++;
        CHECK(cpackCount == 1, "C++库: 重复导入只加载一次");
        bool found = false;
        for (auto& l : g_logLines) if (l == "2") found = true;
        CHECK(found, "C++库: 重复导入后函数仍可用");
        reset();
    }

    // 测试5: C++函数与脚本函数混合使用
    {
        registerNativeFunc("fastAdd", native_fastAdd);
        vector<string> prog = {
            "Func(doubleIt, x)",
            "  return x * 2",
            "EndFunc",
            "GetCPack(\"testmath\")",
            "PrintLog(doubleIt(fastAdd(3, 4)))"
        };
        runLines(prog);
        bool found = false;
        for (auto& l : g_logLines) if (l == "14") found = true;
        CHECK(found, "混合: doubleIt(fastAdd(3,4))=14");
        reset();
    }

    // 清理
    g_nativeFuncs.clear();
    g_loadedCPacks.clear();
}

// ============================================================
// 数学函数测试 (13个)
// ============================================================
void test_math_functions(){
    cout<<"\n=== [数学函数] ==="<<endl;
    string r;
    builtinCall("abs", {"-5"}, r); CHECK_EQ(r, "5", "abs(-5)=5");
    builtinCall("abs", {"5"}, r); CHECK_EQ(r, "5", "abs(5)=5");
    builtinCall("abs", {"-3.14"}, r); CHECK_EQ(r, "3.14", "abs(-3.14)=3.14");
    builtinCall("sqrt", {"9"}, r); CHECK_EQ(r, "3", "sqrt(9)=3");
    builtinCall("sqrt", {"2"}, r); CHECK(r == "1.41421" || r.find("1.414")!=string::npos, "sqrt(2)≈1.414");
    builtinCall("sqrt", {"-1"}, r); CHECK(hasErrors(), "sqrt(-1)报错");
    clearErrors();
    builtinCall("pow", {"2", "10"}, r); CHECK_EQ(r, "1024", "pow(2,10)=1024");
    builtinCall("pow", {"3", "3"}, r); CHECK_EQ(r, "27", "pow(3,3)=27");
    builtinCall("max", {"10", "20"}, r); CHECK_EQ(r, "20", "max(10,20)=20");
    builtinCall("max", {"-5", "-3"}, r); CHECK_EQ(r, "-3", "max(-5,-3)=-3");
    builtinCall("min", {"10", "20"}, r); CHECK_EQ(r, "10", "min(10,20)=10");
    builtinCall("floor", {"3.7"}, r); CHECK_EQ(r, "3", "floor(3.7)=3");
    builtinCall("floor", {"-3.2"}, r); CHECK_EQ(r, "-4", "floor(-3.2)=-4");
    builtinCall("ceil", {"3.2"}, r); CHECK_EQ(r, "4", "ceil(3.2)=4");
    builtinCall("round", {"3.4"}, r); CHECK_EQ(r, "3", "round(3.4)=3");
    builtinCall("round", {"3.6"}, r); CHECK_EQ(r, "4", "round(3.6)=4");
    builtinCall("mod", {"17", "5"}, r); CHECK_EQ(r, "2", "mod(17,5)=2");
    builtinCall("mod", {"10", "3"}, r); CHECK_EQ(r, "1", "mod(10,3)=1");
    builtinCall("mod", {"5", "0"}, r); CHECK(hasErrors(), "mod(5,0)报错");
    clearErrors();
    builtinCall("sin", {"0"}, r); CHECK_EQ(r, "0", "sin(0)=0");
    builtinCall("cos", {"0"}, r); CHECK_EQ(r, "1", "cos(0)=1");
    builtinCall("log", {"1"}, r); CHECK_EQ(r, "0", "log(1)=0");
    builtinCall("log", {"-1"}, r); CHECK(hasErrors(), "log(-1)报错");
    clearErrors();
    // 在表达式中使用
    CHECK_EQ(evalExpr("abs(-10)"), "10", "表达式: abs(-10)");
    CHECK_EQ(evalExpr("max(5, 8)"), "8", "表达式: max(5,8)");
    CHECK_EQ(evalExpr("sqrt(16)"), "4", "表达式: sqrt(16)");
    CHECK_EQ(evalExpr("pow(2, 8)"), "256", "表达式: pow(2,8)");
    CHECK_EQ(evalExpr("abs(-5) + 10"), "15", "表达式: abs(-5)+10");
    reset();
}

// ============================================================
// try-IfErrorToDo 错误处理测试
// ============================================================
void test_try_iferror(){
    cout<<"\n=== [try-IfErrorToDo 错误处理] ==="<<endl;
    // 测试1: try中出错,执行IfErrorToDo
    {
        vector<string> prog = {
            "x = 0",
            "try",
            "  x = sqrt(-1)",
            "  PrintLog(\"no error\")",
            "IfErrorToDo",
            "  PrintLog(\"error caught\")",
            "  x = 0",
            "endTry",
            "PrintLog(\"after\", x)"
        };
        runLines(prog);
        bool foundError = false, foundNoError = true, foundAfter = false;
        for (auto& l : g_logLines) {
            if (l == "error caught") foundError = true;
            if (l == "no error") foundNoError = false;
            if (l == "after 0") foundAfter = true;
        }
        CHECK(foundError, "try: 出错时执行IfErrorToDo");
        CHECK(foundNoError, "try: 出错时不执行try后续代码");
        CHECK(foundAfter, "try: endTry后继续执行");
        reset();
    }
    // 测试2: try中无错误,不执行IfErrorToDo
    {
        vector<string> prog = {
            "try",
            "  x = sqrt(9)",
            "  PrintLog(\"ok\", x)",
            "IfErrorToDo",
            "  PrintLog(\"should not run\")",
            "endTry",
            "PrintLog(\"done\")"
        };
        runLines(prog);
        bool foundOk = false, foundErr = false, foundDone = false;
        for (auto& l : g_logLines) {
            if (l == "ok 3") foundOk = true;
            if (l == "should not run") foundErr = true;
            if (l == "done") foundDone = true;
        }
        CHECK(foundOk, "try无错: 执行try体");
        CHECK(!foundErr, "try无错: 不执行IfErrorToDo");
        CHECK(foundDone, "try无错: 继续执行");
        reset();
    }
    // 测试3: 嵌套try
    {
        vector<string> prog = {
            "try",
            "  try",
            "    x = mod(10, 0)",
            "  IfErrorToDo",
            "    PrintLog(\"inner error\")",
            "  endTry",
            "  PrintLog(\"outer ok\")",
            "IfErrorToDo",
            "  PrintLog(\"outer error\")",
            "endTry"
        };
        runLines(prog);
        bool foundInner = false, foundOuterOk = false, foundOuterErr = false;
        for (auto& l : g_logLines) {
            if (l == "inner error") foundInner = true;
            if (l == "outer ok") foundOuterOk = true;
            if (l == "outer error") foundOuterErr = false;
        }
        CHECK(foundInner, "嵌套try: 内层捕获错误");
        CHECK(foundOuterOk, "嵌套try: 外层继续执行");
        reset();
    }
    // 测试4: try中没有IfErrorToDo
    {
        vector<string> prog = {
            "try",
            "  x = sqrt(-1)",
            "endTry",
            "PrintLog(\"continued\")"
        };
        runLines(prog);
        bool found = false;
        for (auto& l : g_logLines) if (l == "continued") found = true;
        CHECK(found, "try无IfErrorToDo: 出错后继续");
        reset();
    }
}

int main(){
    cout<<"╔══════════════════════════════════════════════════╗"<<endl;
    cout<<"║   R1Sys.cpp 完整测试套件 - 每函数每分支         ║"<<endl;
    cout<<"╚══════════════════════════════════════════════════╝"<<endl;

    // 辅助函数
    test_isIdentifier();
    test_isPureNumber();
    test_isQuoted();
    test_hasStringLiteral();
    test_parseStringLiteral();
    test_numToStr();
    test_startsWith();
    test_findAssignEq();
    test_findTopLevelOp();
    test_findTopLevelMulDiv();
    test_findConcatPlus();

    // 核心求值
    test_calc();
    test_cond();
    test_getArg();
    test_evalExpr();
    test_resolveFuncArg();

    // 函数调用
    test_callFunc();

    // runCode 各分支
    test_runCode_assignment();
    test_runCode_compound();
    test_runCode_return();
    test_runCode_builtins();
    test_runCode_box();
    test_runCode_Input();

    // runLines 各分支
    test_runLines_Func();
    test_runLines_if();
    test_runLines_For();
    test_runLines_while();
    test_runLines_comments();

    // 集成测试
    test_integration();

    // 恢复机制测试
    test_resume_sequential();
    test_resume_multiple_input();
    test_resume_for_loop();
    test_resume_while_loop();
    test_resume_if_block();
    test_resume_nested_for();
    test_resume_input_then_code();
    test_resume_no_input();

    // 字符串+列表+错误系统测试
    test_string_functions();
    test_list_functions();
    test_error_system();
    test_integration_str_list();
    test_const_system();
    test_dict_system();
    test_input_in_expression();
    test_expression_modes_complete();
    test_pack_system();
    test_cpack_system();
    test_math_functions();
    test_try_iferror();

    cout<<"\n══════════════════════════════════════════════════"<<endl;
    cout<<"  总计: "<<g_total<<"  通过: \033[32m"<<g_pass<<"\033[0m  失败: ";
    if(g_fail>0) cout<<"\033[31m"<<g_fail<<"\033[0m"; else cout<<"\033[32m0\033[0m";
    cout<<endl<<"══════════════════════════════════════════════════"<<endl;

    return g_fail>0 ? 1 : 0;
}
