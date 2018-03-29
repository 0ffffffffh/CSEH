#ifndef __CSEH_H_
#define __CSEH_H_

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEH_ERROR_ACCESS_VIOLATION			1
#define SEH_ERROR_ARITHMETIC_OPERATION		2
#define SEH_ERROR_DIVIDE_BY_ZERO			3
#define SEH_ERROR_ILLEGAL_INSTRUCTION		4
#define SEH_ERROR_NON_CONTINUABLE			5
#define SEH_ERROR_UNKNOWN					6

#define SEH_MAX_ERROR						SEH_ERROR_UNKNOWN



#if _WIN32
#include <Windows.h>
typedef DWORD		threadid_t;
#define NTOS		1
#else
#include <pthread.h>
typedef pthread_t	threadid_t;
#define UNIX		1
#endif


#if _USE_MSVC_SEH + _USE_GCC_LABEL_SUPPORTED_SEH + _USE_GENERIC_SEH > 1
#error "You can only choose single seh type at the time"
#endif

#ifdef _SEH_AUTOSELECT

#if defined(_MSC_VER)

#define _USE_MSVC_SEH 1

#elif defined(__GNUC__) || defined(__clang__)

#if NTOS
#define _USE_GENERIC_SEH 1
#else
#define _USE_GCC_LABEL_SUPPORTED_SEH 1
#endif

#endif

#else


#endif


typedef struct _seh_context_t
{
	threadid_t              threadId;
	int                     id;
	int                     faultCode;
	jmp_buf                 buf;
	struct
	{
		struct _seh_context_t * flink;
		struct _seh_context_t * blink;
	} seh_chain;
} seh_context_t;

typedef struct _excpinfo_t
{
	int sehId;
	int faultCode;
} excpinfo_t;

#ifdef __cplusplus
extern "C" {
#endif

#if NTOS
	int seh_msvc_filter(excpinfo_t *ex, int code);
#endif

	int seh_getfaultcode(int sehid);

	void seh_destroy(int sehId);

	int seh_create(jmp_buf *buf, excpinfo_t *exp);

	const char *seh_get_exception_string(excpinfo_t *ex);

#ifdef _USE_GENERIC_SEH
#define SEH_TRY  { int __seh0_init = 0; \
			do { \
				jmp_buf __seh0_jbuf; \
				if (__seh0_init) \

#define SEH_CATCH(x) if (!__seh0_init) { \
						if (!setjmp(__seh0_jbuf)) \
							(x)->sehId = seh_create(&__seh0_jbuf,(x)); \
						else { \
							__seh0_init = 0; \
							(x)->faultCode = seh_getfaultcode((x)->sehId); \
							break; \
						} \
						 __seh0_init = 1; \
					} \
					else \
						__seh0_init = 0; \
				} while (__seh0_init); \
			} \
			seh_destroy((x)->sehId); \
			if ((x)->faultCode != 0)

#elif _USE_MSVC_SEH

#define SEH_TRY __try
#define SEH_CATCH(x) __except(seh_msvc_filter(x,GetExceptionCode()))

#elif _USE_GCC_LABEL_SUPPORTED_SEH
#define SEH_TRY { __label__ seh_catch_block, seh_ok; \
					jmp_buf __seh0_jbuf; \
					int __seh0_id; \
                    if (!setjmp(__seh0_jbuf)) { \
                        __seh0_id = seh_create(&__seh0_jbuf,NULL); \
                    } \
                    else { \
                        goto seh_catch_block; \
                    } \


#define SEH_CATCH(x) (x)->faultCode = 0; \
					 (x)->sehId = __seh0_id; \
                            goto seh_ok; \
                            seh_catch_block:; \
							(x)->sehId = __seh0_id; \
                            (x)->faultCode = seh_getfaultcode((x)->sehId); \
                            seh_ok:; \
                    } \
					seh_destroy((x)->sehId); \
                    if ((x)->faultCode != 0)
#else

#endif
#ifdef __cplusplus
}
#endif

#endif //__CSEH_H_