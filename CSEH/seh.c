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

#include "seh.h"
#include <stdio.h>
#include <string.h>


#if defined(PLATFORM_WINDOWS)
	#include <windows.h>
#elif defined(PLATFORM_UNIX)
	#include <pthread.h>
#endif

//Maximum SEH object count.
#define MAX_SEH 32

//Synchronization object for multi-threaded seh.
typedef struct _sehsync
{
	unsigned char guarded_in_handler;
#if defined(PLATFORM_WINDOWS)
	HANDLE mutex;
#elif defined(PLATFORM_UNIX)
    pthread_mutex_t mutex;
#endif

}sehsync;

#if defined(PLATFORM_WINDOWS)
PVOID g_veh_handle;
#define LONGJMP(buf,val) longjmp(buf,val)
#elif defined(PLATFORM_UNIX)
#define LONGJMP(buf,val) siglongjmp(buf,val)
#endif

//SEH variable definitions
sehsync g_sync; //Sync object
seh_info g_sehs[MAX_SEH]; //Seh list
jmp_buf g_invalid_handler; //Invalid handler context
int g_seh_index=0; //SEH Index
unsigned char g_seh_inited=0; //SEH Initialization status
void (*g_failhandler)();

//Creates a sync mutext for MT.
void create_seh_mutex()
{
#if defined(PLATFORM_WINDOWS)
	g_sync.mutex = CreateMutex(NULL,FALSE,NULL);
#elif defined(PLATFORM_UNIX)
	pthread_mutex_init(&g_sync.mutex,NULL);
#endif
}

//Delete sync mutex.
void delete_seh_mutex()
{
#if defined(PLATFORM_WINDOWS)
	CloseHandle((HANDLE)g_sync.mutex);
#elif defined(PLATFORM_UNIX)
	pthread_mutex_destroy(&g_sync.mutex);
#endif
}

//Guard area
unsigned char seh_guard()
{
#if defined(PLATFORM_WINDOWS)
	return WaitForSingleObject((HANDLE)g_sync.mutex,INFINITE) == WAIT_OBJECT_0;
#elif defined(PLATFORM_UNIX)
	return pthread_mutex_lock(&g_sync.mutex) == 0;
#endif
}

//Leave guarded area.
void seh_leave_guard()
{
#if defined(PLATFORM_WINDOWS)
	ReleaseMutex((HANDLE)g_sync.mutex);
#elif defined(PLATFORM_UNIX)
	pthread_mutex_unlock(&g_sync.mutex);
#endif
}

void handler(int c);

//POSIX signal handler registration
void install_signal_traps()
{
    signal(SIGSEGV,handler);
    signal(SIGILL, handler);
    signal(SIGFPE, handler);
}

#if defined (PLATFORM_WINDOWS)

//Converts POSIX context to Win32 CONTEXT for VEH Enabled Windows
void jmpbuf2win32context(jmp_buf buf, PCONTEXT context)
{
#if defined(ARCH_X64)
#error "jmpbuf2win32context not implemented yet for AMD64. implementing very soon";
#else
	long *jmpptr = (long *)buf;
	context->Ebp = *jmpptr;
	context->Ebx = *(jmpptr+1);
	context->Edi = *(jmpptr+2);
	context->Esi = *(jmpptr+3);
	context->Esp = *(jmpptr+4);
	context->Eip = *(jmpptr+5);
#endif
}

//VEH Enabled Win32 Exception handler
long __stdcall win32_handler(PEXCEPTION_POINTERS excp)
{
	seh_info *act_seh_object=NULL;
	int curr_seh_index = 0;

	//get lastest seh. its active.
	curr_seh_index = g_seh_index - 1;

	//check for valid seh.
	if (curr_seh_index < 0)
	{
		//we aren't in SEH_TRY scope. go to invalid handler.
		jmpbuf2win32context(g_invalid_handler,excp->ContextRecord);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	else
	{
		//serious problem!
		if (!g_sehs[curr_seh_index]._is_in_seh_area)
		{
			if (g_failhandler)
				g_failhandler();
		}
	}

	//guard cricital area
	seh_guard();

	//we are in the guarded area.
	g_sync.guarded_in_handler = 1;
	act_seh_object = &g_sehs[curr_seh_index];

	//mark to raised.
	act_seh_object->_exception_raised = 1;

	//convert POSIX context to Windows CONTEXT
	jmpbuf2win32context(act_seh_object->_context,excp->ContextRecord);

	//if used SEH_CATCH_EX, we must detect to exception type.
	if (act_seh_object->_exception_type_ptr)
	{
		switch (excp->ExceptionRecord->ExceptionCode)
		{
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			*act_seh_object->_exception_type_ptr = CSEH_NONCOUNTABLE_FAULT;
			break;
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_STACK_OVERFLOW:
			*act_seh_object->_exception_type_ptr = CSEH_SEGMENTATION_FAULT;
			break;
		case EXCEPTION_FLT_DENORMAL_OPERAND:
		case EXCEPTION_FLT_INEXACT_RESULT:
		case EXCEPTION_FLT_INVALID_OPERATION:
		case EXCEPTION_FLT_OVERFLOW:
		case EXCEPTION_FLT_STACK_CHECK:
		case EXCEPTION_FLT_UNDERFLOW:
			*act_seh_object->_exception_type_ptr = CSEH_FPE_FAULT;
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			*act_seh_object->_exception_type_ptr = CSEH_DIVIDE_BY_ZERO;
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			*act_seh_object->_exception_type_ptr = CSEH_ILLEGAL_INSTRUCTION;
			break;
		}
	}
	
	//continue with restored context
	return EXCEPTION_CONTINUE_EXECUTION;
}

#endif

//POSIX signal handler.
void handler(int c)
{
	seh_info *act_seh_object=NULL;
	int curr_seh_index = 0;
	
	//get lastest seh. its active.
	curr_seh_index = g_seh_index - 1;

	//check for valid seh.
	if (curr_seh_index < 0)
	{
		//we aren't in SEH_TRY scope. go to invalid handler.
		LONGJMP(g_invalid_handler,0);
		return;
	}
	else
	{
		//serious problem!
		if (!g_sehs[curr_seh_index]._is_in_seh_area)
		{
			if (g_failhandler)
				g_failhandler();
		}
	}

	//guard cricital area
	seh_guard();

	//we are in the guarded area.
	g_sync.guarded_in_handler = 1;

	act_seh_object = &g_sehs[curr_seh_index];

	//if used SEH_CATCH_EX, we must detect to exception type.
	if (act_seh_object->_exception_type_ptr)
	{
		switch (c)
		{
			case SIGFPE:
				*act_seh_object->_exception_type_ptr = CSEH_FPE_FAULT;
				break;
			case SIGSEGV:
				*act_seh_object->_exception_type_ptr = CSEH_SEGMENTATION_FAULT;
				break;
			case SIGILL:
				*act_seh_object->_exception_type_ptr = CSEH_ILLEGAL_INSTRUCTION;
				break;
		}
	}

	//mark to raised.
	act_seh_object->_exception_raised = 1;

	//continue with restored context
	LONGJMP(act_seh_object->_context,0);
}

//Unknown exception handler. And its used to initialization.
void unknown_exception_handler()
{
	//is inited seh?
	if (!g_seh_inited)
	{
		//no mark to inited.
		g_seh_inited=1;
		memset(g_invalid_handler,0,sizeof(SIZEOFJMPBUF));
		//capture invalid exception handler location.
		SETJMP(g_invalid_handler);
	}

	//OUT OF SEH_TRY exceptions comes here.
}

//Initialize SEH
void initialize_seh()
{
	if (!g_seh_inited)
	{
		g_failhandler = NULL;
		//create sync mutex
		create_seh_mutex();

		//initialize platform depended fault handlers

#if defined(PLATFORM_WINDOWS) && defined(USE_VEH)
		g_veh_handle = AddVectoredExceptionHandler(0,win32_handler);
#else
		install_signal_traps();
#endif
		//register out of SEH_TRY handler context
		unknown_exception_handler();
		return;
	}
}

int find_seh(int id)
{
	int i;

	for (i=0;i<g_seh_index;i++)
	{
		if (g_sehs[i]._seh_id == id)
			return i;
	}

	return -1;
}

int delete_seh(int index)
{
	//make sure to valid index.
	if (index < 0 || index >= MAX_SEH)
		return 0;

	//is it guarded?
	if (!g_sync.guarded_in_handler)
		seh_guard(); //no, guard it. else, already guarded in by handler.

	//erase
	memset(&g_sehs[index], 0, sizeof(seh_info));
	
	//reorder
	if (index != g_seh_index-1)
	{
		memmove(&g_sehs[index],&g_sehs[index+1],sizeof(seh_info) * (g_seh_index-(index+1)));
		memset(&g_sehs[g_seh_index-1],0,sizeof(seh_info));
	}
	
	g_seh_index--;

	//clean guarded mark if necessary.
	if (g_sync.guarded_in_handler)
	{
		g_sync.guarded_in_handler = 0;
#if !defined(USE_VEH)
		install_signal_traps();
#endif
	}

	//leave guard.
	seh_leave_guard();
	return 1;
}

int create_seh(unsigned char* context,int id, unsigned long *extype)
{
	if (g_seh_index == MAX_SEH-1)
		return 0;

	if (id <= 0)
		return 0;
	
	//guard area
	seh_guard();

	//set context and other stuff to current seh slot

	memcpy(g_sehs[g_seh_index]._context,context,SIZEOFJMPBUF);
	g_sehs[g_seh_index]._seh_id = id;
	g_sehs[g_seh_index]._exception_raised = 0;
	g_sehs[g_seh_index]._is_in_seh_area = 1;
	g_sehs[g_seh_index]._exception_type_ptr = extype;

	g_seh_index++;

	//release guard.
	seh_leave_guard();

	return 1;
}

int process_seh(unsigned char* context,int id, unsigned long *exception_type)
{
	int seh;

	//is this already inited?
	if (!g_seh_inited)
		initialize_seh();

	//find seh location by id
	seh = find_seh(id);

	//not found seh. we should create new one.
	if (seh == -1)
	{
		if (create_seh(context,id,exception_type))
		{
			memset(context,0,sizeof(context));
			return -1; //created.
		}
	}

	//Does any exception raised?
	if (g_sehs[seh]._exception_raised == 0)
		return 0; //no exception occurred.


	//yep. the exception has raised.
	return 1;
}


//cleanups seh object normally.
int finalize_seh(int id)
{
	int seh = find_seh(id);

	if (seh==-1)
		return 0;

	return delete_seh(seh);
}

void register_onfail_handler(void (*failhandler)())
{
	g_failhandler = failhandler;
}

void shutdown_seh_mgr()
{
	seh_leave_guard();
	delete_seh_mutex();

#if defined(USE_VEH) && defined(PLATFORM_WINDOWS)
	RemoveVectoredContinueHandler(g_veh_handle);
#endif
}
