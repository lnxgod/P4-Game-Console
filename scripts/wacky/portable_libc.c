/* SPDX-License-Identifier: MIT
 * Small cartridge-local helpers; keep the OS import allowlist unchanged. */
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
size_t ww_p4_strlen(const char *s) { size_t n=0; while(s[n]) ++n; return n; }
char *ww_p4_strncpy(char *d, const char *s, size_t n)
{
    size_t i=0; for(;i<n && s[i];++i) d[i]=s[i];
    for(;i<n;++i) d[i]=0;
    return d;
}
char *ww_p4_strcpy(char *d, const char *s)
{
    size_t i=0; do { d[i]=s[i]; } while(s[i++]); return d;
}
int ww_p4_toupper(int c) { return c >= 'a' && c <= 'z' ? c-'a'+'A' : c; }
long ww_p4_strtol(const char *s, char **end, int base)
{
    const char *start=s; int negative=0; unsigned long value=0;
    if(base != 10) { *end=(char *)start; return 0; }
    while(*s==' ' || *s=='\t') ++s;
    if(*s=='-' || *s=='+') negative=(*s++=='-');
    const char *digits=s;
    while(*s>='0' && *s<='9') {
        unsigned long d=(unsigned long)(*s++-'0');
        if(value > ((unsigned long)LONG_MAX-d)/10) {
            while(*s>='0' && *s<='9') ++s;
            *end=(char *)s; return negative ? LONG_MIN : LONG_MAX;
        }
        value=value*10+d;
    }
    *end=(char *)(digits==s ? start : s);
    return negative ? -(long)value : (long)value;
}
static void append(char *dst,size_t cap,size_t *n,char c)
{
    if(*n+1<cap) dst[*n]=c;
    ++*n;
}
int ww_p4_snprintf(char *dst,size_t cap,const char *fmt,...)
{
    size_t n=0; va_list args; va_start(args,fmt);
    while(*fmt) {
        if(*fmt!='%') { append(dst,cap,&n,*fmt++); continue; }
        ++fmt;
        if(*fmt=='s') {
            const char *s=va_arg(args,const char *);
            while(*s) append(dst,cap,&n,*s++);
        } else if(*fmt=='u') {
            unsigned v=va_arg(args,unsigned); char digits[10];size_t len=0;
            do { digits[len++]=(char)('0'+v%10); v/=10; } while(v);
            while(len) append(dst,cap,&n,digits[--len]);
        } else if(*fmt=='%') append(dst,cap,&n,'%');
        else { va_end(args); if(cap) dst[0]=0; return -1; }
        ++fmt;
    }
    va_end(args);
    if(cap) dst[n<cap ? n : cap-1]=0;
    return n>(size_t)INT_MAX ? -1 : (int)n;
}

int ww_p4_abs(int v) { return v == INT_MIN ? INT_MAX : v < 0 ? -v : v; }
