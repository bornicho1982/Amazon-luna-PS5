// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
/* libc compatibility and xlocale wrappers for PS5 eboot.bin (standalone user-space).
 * No raw syscalls (0f 05), no kernel mprotect, no FreeBSD kernel dependencies. */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <wctype.h>
#include <wchar.h>
#include <locale.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* usrsctp: constant-time comparison of authentication data. */
int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    const unsigned char *p1 = (const unsigned char*)b1, *p2 = (const unsigned char*)b2;
    unsigned char diff = 0;
    while (n--) diff |= *p1++ ^ *p2++;
    return diff != 0;
}

/* thread-local destructor registration: safe user-space no-op */
int __cxa_thread_atexit_impl(void (*dtor)(void *), void *obj, void *dso_symbol) {
    (void)dtor; (void)obj; (void)dso_symbol;
    return 0;
}

/* plog (libdatachannel logger): returns current thread id via legal pthread_self() */
long syscall(long number, ...) {
    (void)number;
    return (long)(uintptr_t)pthread_self();
}

/* emulated TLS resolver for clang -femulated-tls */
typedef struct __emutls_control {
    size_t size;
    size_t align;
    union {
        uintptr_t index;
        void *address;
    } object;
} __emutls_control;

void *__emutls_get_address(__emutls_control *control) {
    if (!control->object.address) {
        control->object.address = calloc(1, control->size ? control->size : 1);
    }
    return control->object.address;
}

/* reentrant time functions */
struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (!result) return NULL;
    struct tm *t = localtime(timep);
    if (!t) return NULL;
    *result = *t;
    return result;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (!result) return NULL;
    struct tm *t = gmtime(timep);
    if (!t) return NULL;
    *result = *t;
    return result;
}

/* --- Pure user-space xlocale implementation for libc++.a (locale.cpp) --- */
typedef struct _xlocale *locale_t;

locale_t newlocale(int category_mask, const char *locale, locale_t base) {
    (void)category_mask; (void)locale; (void)base;
    return (locale_t)1;
}

int freelocale(locale_t loc) {
    (void)loc;
    return 0;
}

locale_t uselocale(locale_t newloc) {
    (void)newloc;
    return (locale_t)1;
}

locale_t duplocale(locale_t loc) {
    (void)loc;
    return (locale_t)1;
}

struct lconv *localeconv_l(locale_t loc) {
    (void)loc;
    return localeconv();
}

int snprintf_l(char *str, size_t size, locale_t loc, const char *format, ...) {
    (void)loc;
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int asprintf_l(char **ret, locale_t loc, const char *format, ...) {
    (void)loc;
    va_list ap;
    va_start(ap, format);
    int r = vasprintf(ret, format, ap);
    va_end(ap);
    return r;
}

int sscanf_l(const char *ibuf, locale_t loc, const char *fmt, ...) {
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(ibuf, fmt, ap);
    va_end(ap);
    return r;
}

long long strtoll_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoll(nptr, endptr, base);
}

unsigned long long strtoull_l(const char *nptr, char **endptr, int base, locale_t loc) {
    (void)loc;
    return strtoull(nptr, endptr, base);
}

double strtod_l(const char *nptr, char **endptr, locale_t loc) {
    (void)loc;
    return strtod(nptr, endptr);
}

float strtof_l(const char *nptr, char **endptr, locale_t loc) {
    (void)loc;
    return strtof(nptr, endptr);
}

long double strtold_l(const char *nptr, char **endptr, locale_t loc) {
    (void)loc;
    return strtold(nptr, endptr);
}

int strcoll_l(const char *s1, const char *s2, locale_t loc) {
    (void)loc;
    return strcoll(s1, s2);
}

size_t strxfrm_l(char *dst, const char *src, size_t n, locale_t loc) {
    (void)loc;
    return strxfrm(dst, src, n);
}

size_t strftime_l(char *s, size_t maxsize, const char *format, const struct tm *timeptr, locale_t loc) {
    (void)loc;
    return strftime(s, maxsize, format, timeptr);
}

int wcscoll_l(const wchar_t *s1, const wchar_t *s2, locale_t loc) {
    (void)loc;
    return wcscoll(s1, s2);
}

size_t wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t n, locale_t loc) {
    (void)loc;
    return wcsxfrm(dst, src, n);
}

#undef iswctype
int iswctype_l(wint_t wc, wctype_t desc, locale_t loc) {
    (void)loc;
    return (iswctype)(wc, desc);
}

wint_t btowc_l(int c, locale_t loc) {
    (void)loc;
    return btowc(c);
}

int wctob_l(wint_t c, locale_t loc) {
    (void)loc;
    return wctob(c);
}

size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, locale_t loc) {
    (void)loc;
    return mbrlen(s, n, ps);
}

size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, locale_t loc) {
    (void)loc;
    return mbrtowc(pwc, s, n, ps);
}

size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, locale_t loc) {
    (void)loc;
    return wcrtomb(s, wc, ps);
}

size_t mbsrtowcs_l(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, locale_t loc) {
    (void)loc;
    return mbsrtowcs(dst, src, len, ps);
}

size_t wcsrtombs_l(char *dst, const wchar_t **src, size_t len, mbstate_t *ps, locale_t loc) {
    (void)loc;
    return wcsrtombs(dst, src, len, ps);
}

size_t mbsnrtowcs_l(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps, locale_t loc) {
    (void)nms; (void)loc;
    return mbsrtowcs(dst, src, len, ps);
}

size_t wcsnrtombs_l(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps, locale_t loc) {
    (void)nwc; (void)loc;
    return wcsrtombs(dst, src, len, ps);
}

int mbtowc_l(wchar_t *pwc, const char *s, size_t n, locale_t loc) {
    (void)loc;
    return mbtowc(pwc, s, n);
}

/* catgets stubs */
typedef void *nl_catd;

nl_catd catopen(const char *name, int oflag) {
    (void)name; (void)oflag;
    return (nl_catd)1;
}

char *catgets(nl_catd catd, int set_id, int msg_id, const char *s) {
    (void)catd; (void)set_id; (void)msg_id;
    return (char*)s;
}

int catclose(nl_catd catd) {
    (void)catd;
    return 0;
}

/* FreeBSD / Sony Rune & mb_cur_max helpers */
extern int __mb_cur_max;
int ___mb_cur_max_l(locale_t loc) {
    (void)loc;
    return __mb_cur_max ? __mb_cur_max : 1;
}

unsigned long ___runetype_l(__ct_rune_t c, locale_t loc) {
    (void)c; (void)loc;
    return 0;
}

#undef tolower
__ct_rune_t ___tolower_l(__ct_rune_t c, locale_t loc) {
    (void)loc;
    return (tolower)((int)c);
}

#undef toupper
__ct_rune_t ___toupper_l(__ct_rune_t c, locale_t loc) {
    (void)loc;
    return (toupper)((int)c);
}

_RuneLocale *__runes_for_locale(locale_t loc, int *p) {
    (void)loc;
    if (p) *p = 0;
    return (_RuneLocale *)&_DefaultRuneLocale;
}

/* --- Name resolution --- */
int xc_resolve_ipv4(const char *host, uint32_t *addr) {
    struct in_addr in;
    if (inet_pton(AF_INET, host, &in) == 1) {
        *addr = in.s_addr;
        return 0;
    }
    return -1;
}

static struct addrinfo *make_ai(int family, const void *addr, unsigned scope, int port, const struct addrinfo *hints) {
    size_t salen = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct addrinfo *ai = (struct addrinfo*)calloc(1, sizeof *ai + salen);
    if (!ai) return NULL;
    struct sockaddr *sa = (struct sockaddr *)(ai + 1);
    if (family == AF_INET) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)sa;
        s4->sin_len = sizeof *s4;
        s4->sin_family = AF_INET;
        s4->sin_port = htons((uint16_t)port);
        memcpy(&s4->sin_addr, addr, 4);
    } else {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)sa;
        s6->sin6_len = sizeof *s6;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons((uint16_t)port);
        s6->sin6_scope_id = scope;
        memcpy(&s6->sin6_addr, addr, 16);
    }
    ai->ai_family = family;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_flags = hints ? hints->ai_flags : 0;
    ai->ai_addrlen = (socklen_t)salen;
    ai->ai_addr = sa;
    return ai;
}

int getaddrinfo(const char *host, const char *serv, const struct addrinfo *hints, struct addrinfo **res) {
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int flags = hints ? hints->ai_flags : 0;
    int port = 0;
    if (!res) return EAI_FAIL;
    *res = NULL;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return EAI_FAMILY;
    if (serv && *serv) {
        char *end;
        long p = strtol(serv, &end, 10);
        if (*end || p < 0 || p > 65535) {
            if (!strcmp(serv, "https")) p = 443;
            else if (!strcmp(serv, "http")) p = 80;
            else return EAI_SERVICE;
        }
        port = (int)p;
    }

    unsigned char a[16];
    unsigned scope = 0;
    if (!host) {
        if (family == AF_INET6) {
            memcpy(a, (flags & AI_PASSIVE) ? &in6addr_any : &in6addr_loopback, 16);
            return (*res = make_ai(AF_INET6, a, 0, port, hints)) ? 0 : EAI_MEMORY;
        }
        uint32_t v4 = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
        return (*res = make_ai(AF_INET, &v4, 0, port, hints)) ? 0 : EAI_MEMORY;
    }
    if (family != AF_INET6 && inet_pton(AF_INET, host, a) == 1)
        return (*res = make_ai(AF_INET, a, 0, port, hints)) ? 0 : EAI_MEMORY;
    if (family != AF_INET) {
        char tmp[64];
        snprintf(tmp, sizeof tmp, "%s", host);
        char *pct = strchr(tmp, '%');
        if (pct) {
            *pct = 0;
            scope = (unsigned)strtoul(pct + 1, NULL, 10);
        }
        if (inet_pton(AF_INET6, tmp, a) == 1)
            return (*res = make_ai(AF_INET6, a, scope, port, hints)) ? 0 : EAI_MEMORY;
    }
    if (flags & AI_NUMERICHOST) return EAI_NONAME;
    if (family == AF_INET6) return EAI_NONAME;
    uint32_t v4;
    if (xc_resolve_ipv4(host, &v4) != 0) return EAI_NONAME;
    return (*res = make_ai(AF_INET, &v4, 0, port, hints)) ? 0 : EAI_MEMORY;
}

void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen,
                int flags) {
    (void)salen;
    (void)flags;
    int port;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
        if (host && hostlen && !inet_ntop(AF_INET, &s4->sin_addr, host, (socklen_t)hostlen)) return EAI_OVERFLOW;
        port = ntohs(s4->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
        if (host && hostlen && !inet_ntop(AF_INET6, &s6->sin6_addr, host, (socklen_t)hostlen)) return EAI_OVERFLOW;
        port = ntohs(s6->sin6_port);
    } else {
        return EAI_FAMILY;
    }
    if (serv && servlen) snprintf(serv, servlen, "%d", port);
    return 0;
}
