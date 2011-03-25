/*
Copyright (C) 2010  Oguz Kartal

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __SEH__
#define __SEH__

#include <signal.h>
#include <setjmp.h>

#define CSEH_SEGMENTATION_FAULT 1
#define CSEH_FPE_FAULT 2
#define CSEH_ILLEGAL_INSTRUCTION 3
#define CSEH_DIVIDE_BY_ZERO 4
#define CSEH_NONCOUNTABLE_FAULT 5


#if defined(_WIN32) || defined(WIN32)
#define PLATFORM_WINDOWS
#elif defined(unix) || defined(_unix) || defined(__unix) || defined(__unix__)
#define PLATFORM_UNIX
#else
#error "Not supported OS type"
#endif

#if defined (__x86_64__) || defined (__x86_64) || defined (__amd64) || defined (__amd64__) || defined (_AMD64_) || defined (_M_X64)
#define ARCH_X64
#elif defined(__i386__) || defined (i386) || defined(_M_IX86) || defined(_X86_) || defined(__THW_INTEL)
#define ARCH_X86
#else
#error "Not supported CPU Arch."
#endif

#if defined(PLATFORM_WINDOWS)
#define USE_VEH
#endif


typedef struct _seh_info
{
	unsigned char _exception_raised;
	unsigned char _is_in_seh_area;
	unsigned long *_exception_type_ptr;
#if defined (PLATFORM_UNIX)
	sigjmp_buf _context;
#elif defined (PLATFORM_WINDOWS)
	jmp_buf _context;
#endif
	int _seh_id;
}seh_info;

#if defined(PLATFORM_UNIX)

#define SETJMP(buf) sigsetjmp(buf,1)
static sigjmp_buf _buf_context;
#define SIZEOFJMPBUF sizeof(sigjmp_buf)

#elif defined(PLATFORM_WINDOWS)

#define SETJMP(buf) setjmp(buf)
static jmp_buf _buf_context;
#define SIZEOFJMPBUF sizeof(jmp_buf)

#endif


int process_seh(unsigned char *context,int id, unsigned long *exception_type);
int finalize_seh(int id);
void register_onfail_handler(void (*failhandler)());

void shutdown_seh_mgr();

#define SEH_TRY(id) goto catch_init_##id; \
					try_begin_##id: \

#define SEH_CATCH(id) catch_init_##id: \
									SETJMP(_buf_context); \
									switch (process_seh((unsigned char *)_buf_context,id,0)) \
									{ \
										case -1: \
											goto try_begin_##id; \
										case 0: \
											goto seh_final_##id; \
										case 1: \
											finalize_seh(id); \
									} \


#define SEH_CATCH_EX(id, exceptiontype) catch_init_##id: \
									SETJMP(_buf_context); \
									switch (process_seh((unsigned char *)_buf_context,id,exceptiontype)) \
									{ \
										case -1: \
											goto try_begin_##id; \
										case 0: \
											goto seh_final_##id; \
										case 1: \
											finalize_seh(id); \
									} \


#define SEH_END(id)  seh_final_##id: finalize_seh(id) ;


#endif //__SEH__
