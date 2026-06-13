#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <sstream>

HWND g_hOutput, g_hInput, g_hMultiEdit, g_hRunBtn, g_hStepBtn, g_hSwitchBtn;
bool g_multiLineMode = false;
std::vector<std::string> g_multiLines;
int g_currentLine = 0;
bool g_waitingInput = false;
std::string g_inputPrompt, g_inputVarName;
int g_inputType = 0;
bool g_returning = false;

void guiOutput(const std::string& text) {
    if (!g_hOutput) return;
    int len = GetWindowTextLength(g_hOutput);
    SendMessage(g_hOutput, EM_SETSEL, len, len);
    SendMessage(g_hOutput, EM_REPLACESEL, 0, (LPARAM)text.c_str());
}

std::string getTime() {
    time_t t = time(0); char b[64];
    strftime(b, sizeof(b), "%H:%M:%S", localtime(&t));
    return b;
}
void Log(const std::string& s) { guiOutput("[" + getTime() + "] " + s + "\r\n"); }

std::map<std::string, std::string> g_vars;
void setVar(const std::string& n, const std::string& v) { g_vars[n] = v; }
std::string getVar(const std::string& n) {
    auto it = g_vars.find(n); return it != g_vars.end() ? it->second : "";
}
bool hasVar(const std::string& n) { return g_vars.find(n) != g_vars.end(); }

struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> body;
};
std::map<std::string, Function> g_funcs;

double calc(const std::string& e) {
    std::string s = e;
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return 0;
    if (hasVar(s)) return atof(getVar(s).c_str());
    int d = 0;
    for (int i = (int)s.size() - 1; i > 0; i--) {
        if (s[i] == ')') d++;
        else if (s[i] == '(') d--;
        else if (d == 0 && s[i] == '+') return calc(s.substr(0,i)) + calc(s.substr(i+1));
        else if (d == 0 && s[i] == '-') return calc(s.substr(0,i)) - calc(s.substr(i+1));
    }
    d = 0;
    for (int i = (int)s.size() - 1; i > 0; i--) {
        if (s[i] == ')') d++;
        else if (s[i] == '(') d--;
        else if (d == 0 && s[i] == '*') return calc(s.substr(0,i)) * calc(s.substr(i+1));
        else if (d == 0 && s[i] == '/') {
            double r = calc(s.substr(i+1));
            return r != 0 ? calc(s.substr(0,i)) / r : 0;
        }
    }
    return atof(s.c_str());
}

bool cond(const std::string& c) {
    std::string s = c;
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    std::string ops[] = {"==","!=",">=","<=",">","<"};
    for (int i = 0; i < 6; i++) {
        size_t p = s.find(ops[i]);
        if (p != std::string::npos) {
            std::string l = s.substr(0,p), r = s.substr(p+ops[i].size());
            while (!l.empty() && l.back() == ' ') l.pop_back();
            while (!r.empty() && r[0] == ' ') r.erase(0,1);
            double lv = calc(l), rv = calc(r);
            if (ops[i]=="==") return lv==rv;
            if (ops[i]=="!=") return lv!=rv;
            if (ops[i]==">=") return lv>=rv;
            if (ops[i]=="<=") return lv<=rv;
            if (ops[i]==">")  return lv>rv;
            if (ops[i]=="<")  return lv<rv;
        }
    }
    if (s=="true") return true;
    if (s=="false") return false;
    if (hasVar(s)) return getVar(s)=="true";
    return calc(s) != 0;
}

std::string getArg(const std::string& s, size_t& pos) {
    while (pos < s.size() && s[pos] == ' ') pos++;
    if (pos >= s.size()) return "";
    std::string r;
    if (s[pos] == '\'' || s[pos] == '"') {
        char q = s[pos]; pos++;
        while (pos < s.size() && s[pos] != q) { r += s[pos]; pos++; }
        if (pos < s.size()) pos++;
        return r;
    }
    int d = 0;
    while (pos < s.size()) {
        if (s[pos] == '(') d++;
        else if (s[pos] == ')') { if (d==0) break; d--; }
        else if (d==0 && s[pos]==',') break;
        r += s[pos]; pos++;
    }
    while (!r.empty() && r[0]==' ') r.erase(0,1);
    while (!r.empty() && r.back()==' ') r.pop_back();
    return r;
}

bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) {
        if (!isalpha(s[i]) && s[i] != '_') return false;
    }
    return true;
}

// 前向声明（所有互相调用的函数）
void runCode(const std::string& input);
void runLines(const std::vector<std::string>& lines);
std::string callFunc(const std::string& name, const std::vector<std::string>& args);
std::string resolveValue(const std::string& raw);
std::string resolveExprWithFuncs(const std::string& expr);

// 解析表达式中的嵌套函数调用
std::string resolveExprWithFuncs(const std::string& expr) {
    std::string result = expr;
    size_t pos = result.size();
    while (pos > 0) {
        pos--;
        if (result[pos] == ')') {
            int depth = 1;
            size_t left = pos;
            while (left > 0 && depth > 0) {
                left--;
                if (result[left] == ')') depth++;
                else if (result[left] == '(') depth--;
            }
            if (depth != 0) continue;
            
            size_t funcEnd = left;
            size_t funcStart = left;
            while (funcStart > 0 && (isalnum(result[funcStart-1]) || result[funcStart-1] == '_')) funcStart--;
            
            std::string funcName = result.substr(funcStart, funcEnd - funcStart);
            
            if (g_funcs.find(funcName) != g_funcs.end()) {
                std::string argsStr = result.substr(left + 1, pos - left - 1);
                std::vector<std::string> cargs;
                size_t ap = 0;
                while (ap < argsStr.size()) {
                    std::string arg = getArg(argsStr, ap);
                    cargs.push_back(resolveExprWithFuncs(arg));
                    if (ap < argsStr.size() && argsStr[ap] == ',') ap++;
                }
                for (size_t i = 0; i < cargs.size(); i++) {
                    if (isIdentifier(cargs[i]) && !hasVar(cargs[i])) continue;
                    if ((cargs[i][0]=='\''||cargs[i][0]=='"') && cargs[i].back()==cargs[i][0]) continue;
                    cargs[i] = resolveValue(cargs[i]);
                }
                std::string funcResult = callFunc(funcName, cargs);
                result.replace(funcStart, pos - funcStart + 1, funcResult);
                pos = funcStart + funcResult.size();
            }
        }
    }
    return result;
}

std::string resolveValue(const std::string& raw) {
    if (raw.empty()) return "";
    if ((raw[0]=='"'||raw[0]=='\'') && raw.back()==raw[0] && raw.size()>=2)
        return raw.substr(1, raw.size()-2);
    if (hasVar(raw) && raw.find_first_of("+-*/ ()'\"")==std::string::npos)
        return getVar(raw);
    bool hasQ = (raw.find('"')!=std::string::npos || raw.find('\'')!=std::string::npos);
    bool hasP = (raw.find('+')!=std::string::npos);
    if (hasQ && hasP) {
        std::string result;
        size_t i = 0;
        while (i < raw.size()) {
            while (i < raw.size() && raw[i]==' ') i++;
            if (i >= raw.size()) break;
            if (raw[i]=='"' || raw[i]=='\'') {
                char q = raw[i]; i++;
                while (i < raw.size() && raw[i]!=q) { result+=raw[i]; i++; }
                if (i < raw.size()) i++;
            }
            else if (raw[i]=='+' || raw[i]==',') { i++; }
            else {
                std::string tok;
                while (i < raw.size() && raw[i]!=' ' && raw[i]!='+' && raw[i]!=',' &&
                       raw[i]!='"' && raw[i]!='\'' && raw[i]!=')') { tok+=raw[i]; i++; }
                if (!tok.empty()) {
                    if (hasVar(tok)) result+=getVar(tok);
                    else result+=tok;
                }
            }
        }
        return result;
    }
    if (raw.find('(') != std::string::npos && raw.find(')') != std::string::npos) {
        return resolveExprWithFuncs(raw);
    }
    if (hasVar(raw)) return getVar(raw);
    try {
        double val = calc(raw);
        char buf[64];
        if (val==(int)val) sprintf(buf,"%d",(int)val);
        else sprintf(buf,"%g",val);
        return buf;
    } catch (...) { return raw; }
}

std::string resolveFuncArg(const std::string& raw) {
    if (raw.empty()) return "";
    if ((raw[0]=='\''||raw[0]=='"') && raw.back()==raw[0])
        return raw.substr(1, raw.size()-2);
    if (isIdentifier(raw) && !hasVar(raw))
        return raw;
    return resolveValue(raw);
}

std::string callFunc(const std::string& name, const std::vector<std::string>& args) {
    auto it = g_funcs.find(name);
    if (it == g_funcs.end()) return "";
    
    Function& f = it->second;
    std::map<std::string, std::string> savedVars;
    
    for (size_t i = 0; i < f.params.size() && i < args.size(); i++) {
        if (hasVar(f.params[i])) savedVars[f.params[i]] = getVar(f.params[i]);
        setVar(f.params[i], args[i]);
    }
    
    if (hasVar("__ret__")) savedVars["__ret__"] = getVar("__ret__");
    setVar("__ret__", "");
    
    g_returning = false;
    runLines(f.body);
    g_returning = false;
    
    std::string ret = getVar("__ret__");
    for (auto& sv : savedVars) setVar(sv.first, sv.second);
    
    return ret;
}

void runCode(const std::string& input) {
    std::string s = input;
    while (!s.empty() && s[0]==' ') s.erase(0,1);
    while (!s.empty() && s.back()==' ') s.pop_back();
    if (s.empty() || s[0]=='#') return;
    
    if (s.find("return ")==0) {
        std::string val = s.substr(7);
        while (!val.empty() && val[0]==' ') val.erase(0,1);
        setVar("__ret__", resolveFuncArg(val));
        g_returning = true;
        return;
    }
    
    size_t p = s.find('(');
    size_t eq = s.find('=');
    
    if (eq!=std::string::npos && eq>0 && !(eq+1<s.size()&&s[eq+1]=='=') &&
        !(eq>0&&(s[eq-1]=='!'||s[eq-1]=='<'||s[eq-1]=='>')) &&
        !(eq>0&&(s[eq-1]=='+'||s[eq-1]=='-'||s[eq-1]=='*'||s[eq-1]=='/')) &&
        (p==std::string::npos || eq<p)) {
        
        std::string vn=s.substr(0,eq), vl=s.substr(eq+1);
        while (!vn.empty()&&vn.back()==' ') vn.pop_back();
        while (!vl.empty()&&vl[0]==' ') vl.erase(0,1);
        while (!vl.empty()&&vl.back()==' ') vl.pop_back();
        if (vn.empty()||vl.empty()) return;
        
        setVar(vn, resolveFuncArg(vl));
        return;
    }
    
    if (p!=std::string::npos && s.back()==')') {
        std::string fn=s.substr(0,p);
        while (!fn.empty()&&fn.back()==' ') fn.pop_back();
        
        if (g_funcs.find(fn)!=g_funcs.end()) {
            std::vector<std::string> args;
            size_t pos=p+1;
            while (pos<s.size()&&s[pos]!=')') {
                args.push_back(resolveFuncArg(getArg(s,pos)));
                if (pos<s.size()&&s[pos]==',') pos++;
            }
            callFunc(fn, args);
            return;
        }
        
        if (fn=="PrintLog") {
            std::string inner=s.substr(p+1);
            if (!inner.empty()&&inner.back()==')') inner.pop_back();
            Log(resolveValue(inner));
            return;
        }
        
        std::vector<std::string> rawArgs;
        size_t pos=p+1;
        while (pos<s.size()&&s[pos]!=')') {
            rawArgs.push_back(getArg(s,pos));
            if (pos<s.size()&&s[pos]==',') pos++;
        }
        if (fn=="box") { if (rawArgs.size()>=2) setVar(rawArgs[0],resolveFuncArg(rawArgs[1])); return; }
        if (fn=="boxS") { if (rawArgs.size()>=3) setVar(rawArgs[1],resolveFuncArg(rawArgs[2])); return; }
        if (fn=="Input") {
            g_waitingInput=true; g_inputType=0;
            g_inputPrompt=rawArgs.size()>0?resolveFuncArg(rawArgs[0]):"";
            g_inputVarName=rawArgs.size()>1?rawArgs[1]:"";
            Log(g_inputPrompt); return;
        }
        if (fn=="InputInt") {
            g_waitingInput=true; g_inputType=1;
            g_inputPrompt=rawArgs.size()>0?resolveFuncArg(rawArgs[0]):"";
            g_inputVarName=rawArgs.size()>1?rawArgs[1]:"";
            Log(g_inputPrompt); return;
        }
        if (fn=="showAllBoxes") { for (auto& v:g_vars) Log("  "+v.first+" = "+v.second); return; }
        if (fn=="clearAllBoxes") { g_vars.clear(); return; }
        if (fn=="showFuncs") { for (auto& f:g_funcs) Log("Func: "+f.first); return; }
        Log("? "+fn);
        return;
    }
    
    std::string ops[]={"+=","-=","*=","/="};
    for (int j=0;j<4;j++) {
        size_t op=s.find(ops[j]);
        if (op!=std::string::npos&&op>0) {
            std::string vn=s.substr(0,op), rv=s.substr(op+2);
            while (!vn.empty()&&vn.back()==' ') vn.pop_back();
            while (!rv.empty()&&rv[0]==' ') rv.erase(0,1);
            if (hasVar(vn)) {
                double cur=atof(getVar(vn).c_str()), val=calc(rv), res;
                if (j==0) res=cur+val; else if (j==1) res=cur-val;
                else if (j==2) res=cur*val; else res=(val!=0?cur/val:0);
                char buf[64]; sprintf(buf,"%g",res); setVar(vn,buf);
                return;
            }
        }
    }
    
    Log("? "+s);
}

void runLines(const std::vector<std::string>& lines) {
    for (size_t i=0;i<lines.size();i++) {
        if (g_waitingInput) break;
        if (g_returning) { g_returning = false; break; }
        std::string s=lines[i];
        while (!s.empty()&&s[0]==' ') s.erase(0,1);
        while (!s.empty()&&s.back()==' ') s.pop_back();
        if (s.empty()||s[0]=='#') continue;
        
        if (s.find("Func(")==0) {
            size_t p1=s.find('('), p2=s.find_last_of(')');
            if (p1==std::string::npos||p2==std::string::npos) continue;
            std::string as=s.substr(p1+1,p2-p1-1);
            std::vector<std::string> fa;
            size_t ap=0;
            while (ap<as.size()) { fa.push_back(getArg(as,ap)); if (ap<as.size()&&as[ap]==',') ap++; }
            if (fa.size()<1) continue;
            Function func; func.name=fa[0];
            for (size_t k=1;k<fa.size();k++) {
                std::string pn=fa[k];
                while (!pn.empty()&&pn[0]==' ') pn.erase(0,1);
                while (!pn.empty()&&pn.back()==' ') pn.pop_back();
                if (!pn.empty()) func.params.push_back(pn);
            }
            int depth=0;
            for (size_t j=i+1;j<lines.size();j++) {
                std::string l=lines[j];
                while (!l.empty()&&l[0]==' ') l.erase(0,1);
                while (!l.empty()&&l.back()==' ') l.pop_back();
                if (l=="EndFunc"&&depth==0) { i=j; break; }
                if (l.find("Func(")==0||l.find("if(")==0||l.find("For(")==0||l.find("while(")==0) depth++;
                if (l=="EndFunc"||l=="endif"||l=="endfor"||l=="endwhile") depth--;
                func.body.push_back(lines[j]);
            }
            g_funcs[func.name]=func;
            Log("[Func] "+func.name+" ("+std::to_string(func.params.size())+" params)");
            continue;
        }
        
        if (s.find("if(")==0) {
            size_t p1=s.find('('), p2=s.find_last_of(')');
            if (p1==std::string::npos||p2==std::string::npos) continue;
            bool ok=cond(s.substr(p1+1,p2-p1-1));
            bool matched=ok;
            std::vector<std::string> ib,eb;
            bool inElse=false;
            int depth=0;
            for (size_t j=i+1;j<lines.size();j++) {
                std::string l=lines[j];
                while (!l.empty()&&l[0]==' ') l.erase(0,1);
                while (!l.empty()&&l.back()==' ') l.pop_back();
                if (l=="endif"&&depth==0) { i=j; break; }
                if (l.find("if(")==0||l.find("For(")==0||l.find("while(")==0||l.find("Func(")==0) depth++;
                if (l=="endif"||l=="endfor"||l=="endwhile"||l=="EndFunc") depth--;
                if (l=="else"&&depth==0) { inElse=true; continue; }
                if (l.find("elseIf(")==0&&depth==0) {
                    if (!matched) {
                        size_t a=l.find('('), b=l.find_last_of(')');
                        if (a!=std::string::npos&&b!=std::string::npos) {
                            ok=cond(l.substr(a+1,b-a-1));
                            if (ok) { matched=true; ib.clear(); inElse=false; }
                        }
                    }
                    continue;
                }
                if (inElse) eb.push_back(lines[j]); else ib.push_back(lines[j]);
            }
            if (matched) { bool sv=g_returning; g_returning=false; runLines(ib); g_returning=sv; }
            else if (!eb.empty()) { bool sv=g_returning; g_returning=false; runLines(eb); g_returning=sv; }
            continue;
        }
        
        if (s.find("For(")==0) {
            size_t p1=s.find('('), p2=s.find_last_of(')');
            if (p1==std::string::npos||p2==std::string::npos) continue;
            std::string as=s.substr(p1+1,p2-p1-1);
            std::vector<std::string> fa;
            size_t ap=0;
            while (ap<as.size()) { fa.push_back(getArg(as,ap)); if (ap<as.size()&&as[ap]==',') ap++; }
            if (fa.size()<3) continue;
            std::string var=fa[0];
            int start=(int)calc(fa[1]), end=(int)calc(fa[2]), step=1;
            bool hasSE=false;
            if (fa.size()>=4) {
                if (fa[3].find_first_of("+-*/")!=std::string::npos) hasSE=true;
                else step=(int)calc(fa[3]);
            } else step=(start<=end)?1:-1;
            std::vector<std::string> fb;
            int depth=0;
            for (size_t j=i+1;j<lines.size();j++) {
                std::string l=lines[j];
                while (!l.empty()&&l[0]==' ') l.erase(0,1);
                while (!l.empty()&&l.back()==' ') l.pop_back();
                if (l=="endfor"&&depth==0) { i=j; break; }
                if (l.find("For(")==0||l.find("while(")==0||l.find("if(")==0||l.find("Func(")==0) depth++;
                if (l=="endfor"||l=="endwhile"||l=="endif"||l=="EndFunc") depth--;
                fb.push_back(lines[j]);
            }
            if (fb.empty()) continue;
            int val=start, iter=0;
            while (iter<100000) {
                bool cont=hasSE?((start<=end)?(val<=end):(val>=end)):((step>=0)?(val<=end):(val>=end));
                if (!cont) break;
                char buf[64]; sprintf(buf,"%d",val); setVar(var,buf);
                bool sv=g_returning; g_returning=false; runLines(fb); g_returning=sv;
                if (g_waitingInput) break;
                if (hasSE) val=(int)calc(fa[3]); else val+=step;
                iter++;
            }
            continue;
        }
        
        if (s.find("while(")==0) {
            size_t p1=s.find('('), p2=s.find_last_of(')');
            if (p1==std::string::npos||p2==std::string::npos) continue;
            std::string cs=s.substr(p1+1,p2-p1-1);
            std::vector<std::string> wb;
            int depth=0;
            for (size_t j=i+1;j<lines.size();j++) {
                std::string l=lines[j];
                while (!l.empty()&&l[0]==' ') l.erase(0,1);
                while (!l.empty()&&l.back()==' ') l.pop_back();
                if (l=="endwhile"&&depth==0) { i=j; break; }
                if (l.find("while(")==0||l.find("For(")==0||l.find("if(")==0||l.find("Func(")==0) depth++;
                if (l=="endwhile"||l=="endfor"||l=="endif"||l=="EndFunc") depth--;
                wb.push_back(lines[j]);
            }
            if (wb.empty()) continue;
            int iter=0;
            while (iter<100000) {
                if (!cond(cs)) break;
                bool sv=g_returning; g_returning=false; runLines(wb); g_returning=sv;
                if (g_waitingInput) break;
                iter++;
            }
            continue;
        }
        
        runCode(s);
        if (g_returning) break;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hOutput=CreateWindow("EDIT","",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,
                10,10,660,300,hWnd,(HMENU)1,GetModuleHandle(NULL),NULL);
            g_hInput=CreateWindow("EDIT","",WS_CHILD|WS_VISIBLE|WS_BORDER,
                10,320,420,28,hWnd,(HMENU)2,GetModuleHandle(NULL),NULL);
            g_hMultiEdit=CreateWindow("EDIT","",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|WS_BORDER,
                10,320,420,80,hWnd,(HMENU)5,GetModuleHandle(NULL),NULL);
            ShowWindow(g_hMultiEdit,SW_HIDE);
            g_hRunBtn=CreateWindow("BUTTON","运行",WS_CHILD|WS_VISIBLE,440,318,50,28,hWnd,(HMENU)3,GetModuleHandle(NULL),NULL);
            g_hStepBtn=CreateWindow("BUTTON","逐行",WS_CHILD|WS_VISIBLE,500,318,50,28,hWnd,(HMENU)6,GetModuleHandle(NULL),NULL);
            g_hSwitchBtn=CreateWindow("BUTTON","多行",WS_CHILD|WS_VISIBLE,560,318,50,28,hWnd,(HMENU)7,GetModuleHandle(NULL),NULL);
            CreateWindow("BUTTON","清除",WS_CHILD|WS_VISIBLE,620,318,50,28,hWnd,(HMENU)4,GetModuleHandle(NULL),NULL);
            Log("SimpleIDE v2.0");
            break;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hWnd,&rc);
            int w=rc.right-rc.left, h=rc.bottom-rc.top;
            SetWindowPos(g_hOutput,NULL,10,10,w-20,h-150,SWP_NOZORDER);
            SetWindowPos(g_hInput,NULL,10,h-130,w-260,28,SWP_NOZORDER);
            SetWindowPos(g_hMultiEdit,NULL,10,h-130,w-260,80,SWP_NOZORDER);
            SetWindowPos(g_hRunBtn,NULL,w-240,h-132,50,28,SWP_NOZORDER);
            SetWindowPos(g_hStepBtn,NULL,w-180,h-132,50,28,SWP_NOZORDER);
            SetWindowPos(g_hSwitchBtn,NULL,w-120,h-132,50,28,SWP_NOZORDER);
            HWND b=FindWindowEx(hWnd,NULL,"BUTTON","清除");
            if(b) SetWindowPos(b,NULL,w-60,h-132,50,28,SWP_NOZORDER);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam)==3) {
                if (g_waitingInput) {
                    char buf[1024]; GetWindowText(g_hInput,buf,1024);
                    if (strlen(buf)>0) { Log(g_inputPrompt+buf); if (!g_inputVarName.empty()) setVar(g_inputVarName,buf); g_waitingInput=false; SetWindowText(g_hInput,""); }
                } else if (g_multiLineMode) {
                    char buf[65536]; GetWindowText(g_hMultiEdit,buf,65536);
                    std::string t(buf); std::vector<std::string> ls; std::istringstream iss(t); std::string l;
                    while (std::getline(iss,l)) { while(!l.empty()&&(l.back()=='\r'||l.back()=='\n')) l.pop_back(); ls.push_back(l); }
                    Log("=== 运行 ("+std::to_string(ls.size())+"行) ===");
                    runLines(ls); Log("=== 完毕 ===");
                } else {
                    char buf[1024]; GetWindowText(g_hInput,buf,1024);
                    if (strlen(buf)>0) { runCode(buf); SetWindowText(g_hInput,""); }
                }
            }
            if (LOWORD(wParam)==4) SetWindowText(g_hOutput,"");
            if (LOWORD(wParam)==6) {
                if (!g_multiLineMode) break;
                if (g_multiLines.empty()) {
                    char buf[65536]; GetWindowText(g_hMultiEdit,buf,65536);
                    std::string t(buf); std::istringstream iss(t); std::string l;
                    while (std::getline(iss,l)) { while(!l.empty()&&(l.back()=='\r'||l.back()=='\n')) l.pop_back(); g_multiLines.push_back(l); }
                    g_currentLine=0;
                }
                if (g_currentLine<(int)g_multiLines.size()) { runCode(g_multiLines[g_currentLine]); g_currentLine++; }
                if (g_currentLine>=(int)g_multiLines.size()) { Log("=== 逐行完毕 ==="); g_multiLines.clear(); g_currentLine=0; }
            }
            if (LOWORD(wParam)==7) {
                g_multiLineMode=!g_multiLineMode;
                ShowWindow(g_hInput,g_multiLineMode?SW_HIDE:SW_SHOW);
                ShowWindow(g_hMultiEdit,g_multiLineMode?SW_SHOW:SW_HIDE);
                SetWindowText(g_hSwitchBtn,g_multiLineMode?"单行":"多行");
            }
            break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
    }
    return DefWindowProc(hWnd,msg,wParam,lParam);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR, int nCS) {
    WNDCLASS w={0}; w.lpfnWndProc=WndProc; w.hInstance=hI;
    w.hCursor=LoadCursor(NULL,IDC_ARROW); w.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    w.lpszClassName="IDE"; RegisterClass(&w);
    HWND h=CreateWindow("IDE","SimpleIDE v2.0",WS_OVERLAPPEDWINDOW,100,100,700,500,NULL,NULL,hI,NULL);
    ShowWindow(h,nCS); UpdateWindow(h);
    MSG m; while(GetMessage(&m,NULL,0,0)){TranslateMessage(&m);DispatchMessage(&m);}
    return (int)m.wParam;
}
