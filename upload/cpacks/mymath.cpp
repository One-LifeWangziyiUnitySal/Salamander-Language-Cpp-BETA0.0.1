// cpacks/mymath.cpp - 数学扩展库
// 用 GetCPack("mymath") 导入
// registerPack 接收 registerNativeFunc 函数指针

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

typedef const char* (*NativeFunc)(const char** args, int argc);
typedef void (*RegisterFunc)(const char* name, NativeFunc func);

const char* cAdd(const char** args, int n) {
    if (n < 2) return "0";
    static char buf[32];
    sprintf(buf, "%d", atoi(args[0]) + atoi(args[1]));
    return buf;
}

const char* cMul(const char** args, int n) {
    if (n < 2) return "0";
    static char buf[32];
    sprintf(buf, "%d", atoi(args[0]) * atoi(args[1]));
    return buf;
}

const char* cSqrt(const char** args, int n) {
    if (n < 1) return "0";
    static char buf[32];
    double v = atof(args[0]);
    if (v < 0) return "0";
    sprintf(buf, "%g", sqrt(v));
    return buf;
}

const char* cIsPrime(const char** args, int n) {
    if (n < 1) return "false";
    int num = atoi(args[0]);
    if (num < 2) return "false";
    for (int i = 2; i * i <= num; i++) {
        if (num % i == 0) return "false";
    }
    return "true";
}

const char* cFactorial(const char** args, int n) {
    if (n < 1) return "1";
    long long result = 1;
    int num = atoi(args[0]);
    for (int i = 2; i <= num; i++) result *= i;
    static char buf[32];
    sprintf(buf, "%lld", result);
    return buf;
}

const char* cFibonacci(const char** args, int n) {
    if (n < 1) return "0";
    int num = atoi(args[0]);
    if (num <= 0) return "0";
    if (num == 1) return "0";
    if (num == 2) return "1";
    long long a = 0, b = 1, c;
    for (int i = 3; i <= num; i++) {
        c = a + b;
        a = b;
        b = c;
    }
    static char buf[32];
    sprintf(buf, "%lld", b);
    return buf;
}

const char* cGCD(const char** args, int n) {
    if (n < 2) return "0";
    int a = atoi(args[0]), b = atoi(args[1]);
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    static char buf[32];
    sprintf(buf, "%d", a);
    return buf;
}

const char* cReverseStr(const char** args, int n) {
    if (n < 1) return "";
    static char buf[1024];
    const char* s = args[0];
    int len = strlen(s);
    for (int i = 0; i < len; i++) buf[i] = s[len - 1 - i];
    buf[len] = 0;
    return buf;
}

const char* cSortList(const char** args, int n) {
    if (n < 1) return "";
    static char buf[4096];
    int nums[256];
    int count = 0;
    const char* p = args[0];
    int val = 0;
    while (*p && count < 256) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
        } else if (*p == ',') {
            nums[count++] = val;
            val = 0;
        }
        p++;
    }
    if (count < 256) nums[count++] = val;
    for (int i = 0; i < count - 1; i++)
        for (int j = 0; j < count - 1 - i; j++)
            if (nums[j] > nums[j+1]) { int t = nums[j]; nums[j] = nums[j+1]; nums[j+1] = t; }
    int pos = 0;
    for (int i = 0; i < count; i++) {
        pos += sprintf(buf + pos, i < count - 1 ? "%d," : "%d", nums[i]);
    }
    return buf;
}

// DLL 入口 — 接收 registerNativeFunc 函数指针
extern "C" __declspec(dllexport) void registerPack(RegisterFunc reg) {
    reg("cAdd", cAdd);
    reg("cMul", cMul);
    reg("cSqrt", cSqrt);
    reg("cIsPrime", cIsPrime);
    reg("cFactorial", cFactorial);
    reg("cFibonacci", cFibonacci);
    reg("cGCD", cGCD);
    reg("cReverseStr", cReverseStr);
    reg("cSortList", cSortList);
}
