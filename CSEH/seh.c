#include "seh.h"

typedef volatile unsigned int		spinlock_t;

typedef struct _context_listnode_t
{
	struct context_listnode_t *		next;
	seh_context_t *					context;
} context_listnode_t;

typedef struct _context_list
{
	context_listnode_t *			head;
	context_listnode_t *			tail;
	spinlock_t						synchLock;
} context_list_t;

/*
#define SEH_ERROR_ACCESS_VIOLATION			1
#define SEH_ERROR_ARITHMETIC_OPERATION		2
#define SEH_ERROR_DIVIDE_BY_ZERO			3
#define SEH_ERROR_ILLEGAL_INSTRUCTION		4
#define SEH_ERROR_NON_CONTINUABLE			5
#define SEH_ERROR_UNKNOWN					6
*/

const char *seh_error_string[] =
{
	"Access violation",
	"Abnormal arithmetic operation",
	"Divide by zero",
	"Illegal instruction",
	"Non continuable exception",
	"Unknown."
};

static int globalId = 0;
context_list_t listx = { 0,0 };

seh_context_t *sehp_get_context(unsigned long id, int useSehId);


#ifdef _SEH_DPRINT_ENABLE
#define dprint printf
#else
#define dprint
#endif

void *sehp_alloc(int size)
{
	void *p = malloc(size);

	if (p)
		memset(p, 0, size);

	return p;
}

#define getmem(type) (type*)sehp_alloc(sizeof(type))

threadid_t sehp_gettid()
{
#if NTOS
	return GetCurrentThreadId();
#elif UNIX
	return pthread_self();
#endif
}


long sehp_atomic_add(volatile long *valPtr, long addVal)
{
#if NTOS
	return (long)InterlockedAdd((volatile LONG *)valPtr, (LONG)addVal);
#elif UNIX
	return (long)__sync_fetch_and_add(valPtr, addVal);
#endif
}

long sehp_atomic_get(volatile long *ptr)
{
#if NTOS
	return (long)InterlockedAdd((volatile LONG *)ptr, 0);
#elif UNIX
	return (long)__atomic_load_n(ptr, __ATOMIC_SEQ_CST);
#endif
}

void sehp_spinlock(spinlock_t *spin)
{
#if NTOS
	while (InterlockedCompareExchange((volatile LONG *)spin, 1, 0))
		_mm_pause();
#elif UNIX
	while (__sync_val_compare_and_swap(spin, 0, 1))
		__asm volatile ("pause" ::: "memory");
#endif
}

void sehp_spinunlock(spinlock_t *spin)
{
	*spin = 0;
}


int sehp_has_seh_chain(seh_context_t *seh);

void sehp_signal_handler(int sigNum)
{
	seh_context_t *pctx = sehp_get_context(sehp_gettid(), 0);

	dprint("seh trap! pctx = %p, tid: %lu", pctx, sehp_gettid());

	if (!pctx)
	{
		//UNEXPECTED FAULT SIGNAL
		return;
	}

	if (sehp_has_seh_chain(pctx))
	{
		pctx = pctx->seh_chain.blink;
		dprint("chained seh trap. new seh %p (%lu)", pctx, pctx->id);
	}

	switch (sigNum)
	{
	case SIGFPE:
		pctx->faultCode = SEH_ERROR_ARITHMETIC_OPERATION;
		break;
	case SIGSEGV:
		pctx->faultCode = SEH_ERROR_ACCESS_VIOLATION;
		break;
	case SIGILL:
		pctx->faultCode = SEH_ERROR_ILLEGAL_INSTRUCTION;
		break;
	case SIGABRT:
	case SIGTERM:
		pctx->faultCode = SEH_ERROR_NON_CONTINUABLE;
		break;
	default:
		pctx->faultCode = SEH_ERROR_UNKNOWN;
	}

	longjmp(pctx->buf, 1);
}

seh_context_t *sehp_get_context(unsigned long id, int useSehId)
{
	seh_context_t *pctx;

	for (context_listnode_t *node = listx.head, *pnode = NULL; node != NULL; node = node->next)
	{
		pctx = node->context;

		if (useSehId)
		{
			if (pctx->id == id)
				return pctx;
		}
		else if (pctx->threadId == id)
			return pctx;
	}

	return NULL;
}

void sehp_install_signal_traps()
{
	signal(SIGFPE, sehp_signal_handler);
	signal(SIGSEGV, sehp_signal_handler);
	signal(SIGILL, sehp_signal_handler);
}

void sehp_remove_signal_traps()
{
	signal(SIGFPE, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);
	signal(SIGILL, SIG_DFL);
}

int sehp_has_seh_chain(seh_context_t *seh)
{
	return seh->seh_chain.blink != seh->seh_chain.flink;
}

void sehp_link_to_chain(seh_context_t *main, seh_context_t *link)
{
	seh_context_t *oldTail;

	dprint("%p (%lu) is linking to %p (%lu)", link, link->id, main, main->id);

	oldTail = main->seh_chain.blink;

	link->seh_chain.blink = oldTail;
	link->seh_chain.flink = NULL;

	if (oldTail != main)
		oldTail->seh_chain.flink = link;

	main->seh_chain.blink = link;
}

seh_context_t *sehp_pop_seh(seh_context_t *main)
{
	seh_context_t *pseh, *newLast;

	if (!sehp_has_seh_chain(main))
	{
		dprint("seh_pop | %p (%lu) is not chained", main, main->id);
		return NULL;
	}

	pseh = main->seh_chain.blink;
	newLast = pseh->seh_chain.blink;
	pseh->seh_chain.blink = NULL;

	if (newLast == main)
		newLast->seh_chain.flink = main;
	else
		newLast->seh_chain.flink = NULL;

	dprint("seh_pop | %p (%lu) popped", pseh, pseh->id);

	return pseh;
}

void sehp_register(seh_context_t *ctx)
{
	context_listnode_t *node = getmem(context_listnode_t);
	context_listnode_t *p;


	if (!node)
		return;

	node->context = ctx;

	ctx->seh_chain.flink = ctx;
	ctx->seh_chain.blink = ctx;

	sehp_spinlock(&listx.synchLock);

	dprint("sehp_register | ctx: %p (%lu)", ctx, ctx->id);

	if (!listx.head)
	{
		listx.head = listx.tail = node;
#if defined(_USE_GENERIC_SEH) || defined(_USE_GCC_LABEL_SUPPORTED_SEH)
		sehp_install_signal_traps();
#endif
	}
	else
	{
		p = listx.tail;
		p->next = node;
		listx.tail = p;
	}

	sehp_spinunlock(&listx.synchLock);
}

seh_context_t* sehp_deregister(int sehId)
{
	seh_context_t *pctx = NULL;

	dprint("seh id %lu deregistering..", sehId);

	for (context_listnode_t *node = listx.head, *pnode = NULL; node != NULL; node = node->next)
	{
		if (node->context->id == sehId)
		{
			sehp_spinlock(&listx.synchLock);

			if (node == listx.head)
				listx.head = node->next;
			else
			{
				if (node == listx.tail)
					listx.tail = pnode;
				pnode->next = node->next;
			}

#if defined(_USE_GENERIC_SEH) || defined(_USE_GCC_LABEL_SUPPORTED_SEH)

			if (!listx.head)
				sehp_remove_signal_traps();
#endif

			sehp_spinunlock(&listx.synchLock);

			pctx = node->context;
			free(node);
			break;
		}

		pnode = node;
	}

	dprint("deregister %s", pctx != NULL ? "OK" : "NOT FOUND");

	return pctx;
}

#if NTOS
int seh_msvc_filter(excpinfo_t *ex, int code)
{
	ex->sehId = 0;

	switch (code)
	{
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		ex->faultCode = SEH_ERROR_NON_CONTINUABLE;
		break;
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_STACK_OVERFLOW:
		ex->faultCode = SEH_ERROR_ACCESS_VIOLATION;
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
	case EXCEPTION_FLT_INEXACT_RESULT:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW:
	case EXCEPTION_FLT_STACK_CHECK:
	case EXCEPTION_FLT_UNDERFLOW:
		ex->faultCode = SEH_ERROR_ARITHMETIC_OPERATION;
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		ex->faultCode = SEH_ERROR_DIVIDE_BY_ZERO;
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		ex->faultCode = SEH_ERROR_ILLEGAL_INSTRUCTION;
		break;
	default:
		ex->faultCode = SEH_ERROR_UNKNOWN;
	}

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int seh_getfaultcode(int sehid)
{
	seh_context_t *pctx = sehp_get_context(sehid, 1);

	if (!pctx)
	{
		pctx = sehp_get_context(sehp_gettid(), 0);

		if (!pctx)
			return 0;

		if (sehp_has_seh_chain(pctx))
			pctx = pctx->seh_chain.blink;
		else
			return 0;
	}

	dprint("getfaultcode | pctx = %p, sehid = %lu", pctx, sehid);

	return pctx->faultCode;
}

void seh_destroy(int sehId)
{
	seh_context_t *pctx = sehp_deregister(sehId);

	dprint("destroying seh %lu, %s", sehId, pctx != NULL ? "ok" : "no");

	if (!pctx)
	{
		/*
		hmm looks like its a nested seh.
		find the root seh by being executed thread's id
		*/
		dprint("checking for nested seh");
		pctx = sehp_get_context(sehp_gettid(), 0);

		if (!pctx)
		{
			dprint("illegal call");
			//probably its an illegal call which is out of the seh mechanism.
			return;
		}

		//pop the last seh from the stacked seh chain.

		dprint("%p (%lu) is chained seh", pctx, pctx->id);

		if (sehp_has_seh_chain(pctx))
			pctx = sehp_pop_seh(pctx);
		else
			pctx = NULL;
	}

	if (pctx)
		free(pctx);
}

int seh_create(jmp_buf *buf, excpinfo_t *exp)
{
	seh_context_t *cbuf = getmem(seh_context_t);
	seh_context_t *early;

	if (exp)
		memset(exp, 0, sizeof(excpinfo_t));

	cbuf->id = sehp_atomic_add(&globalId, 1);
	cbuf->threadId = sehp_gettid();
	memcpy(cbuf->buf, buf, sizeof(jmp_buf));

	early = sehp_get_context(cbuf->threadId, 0);

	dprint("seh_create | early=%p, new_seh_id: %lu", early, cbuf->id);

	if (early != NULL)
		sehp_link_to_chain(early, cbuf);
	else
		sehp_register(cbuf);

	return cbuf->id;
}

const char *seh_get_exception_string(excpinfo_t *ex)
{
	if (ex->faultCode < 1 || ex->faultCode > SEH_MAX_ERROR)
		return "<NULL>";

	return seh_error_string[ex->faultCode - 1];
}