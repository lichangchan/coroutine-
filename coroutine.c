#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
	#include <sys/ucontext.h>
#else 
	#include <ucontext.h>
#endif 

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16

/*
ucontext相关接口，主要有如下四个：
    getcontext：获取当前context
    setcontext：切换到指定context
    makecontext: 用于将一个新函数和堆栈，绑定到指定context中
    swapcontext：保存当前context，并且切换到指定context
*/

struct coroutine;

struct schedule {
	char stack[STACK_SIZE];	// 运行时栈，此栈即是共享栈

	ucontext_t main; // 主协程的上下文
	int nco;        // 当前存活的协程个数
	int cap;        // 协程管理器的当前最大容量，即可以同时支持多少个协程。如果不够了，则进行 2 倍扩容
	int running;    // 正在运行的协程 ID
	struct coroutine **co; // 一个一维数组，用于存放所有协程。其长度等于 cap
};

struct coroutine {
	coroutine_func func; // 协程所用的函数
	void *ud;  // 协程参数
	ucontext_t ctx; // 协程上下文
	struct schedule * sch; // 该协程所属的调度器
	ptrdiff_t cap; 	 // 已经分配的内存大小
	ptrdiff_t size; // 当前协程运行时栈，保存起来后的大小
	int status;	// 协程当前的状态
	char *stack; // 当前协程的保存起来的运行时栈
};
struct coroutine * 
_co_new(struct schedule *S , coroutine_func func, void *ud) {
	struct coroutine * co = malloc(sizeof(*co));
	co->func = func;
	co->ud = ud;
	co->sch = S;
	co->cap = 0;
	co->size = 0;
	co->status = COROUTINE_READY;
	co->stack = NULL;
	return co;
}

void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

struct schedule * 
coroutine_open(void) {
	struct schedule *S = malloc(sizeof(*S));
	S->nco = 0;
	S->cap = DEFAULT_COROUTINE;
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

void 
coroutine_close(struct schedule *S) {
	int i;
	for (i=0;i<S->cap;i++) {
		struct coroutine * co = S->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	free(S->co);
	S->co = NULL;
	free(S);
}

int 
coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	struct coroutine *co = _co_new(S, func , ud);
	if (S->nco >= S->cap) {
		int id = S->cap;
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} else {
		int i;
		for (i=0;i<S->cap;i++) {
			int id = (i+S->nco) % S->cap;
			if (S->co[id] == NULL) {
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	}
	assert(0);
	return -1;
}

static void
mainfunc(uint32_t low32, uint32_t hi32) {
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];
	C->func(S,C->ud);
	_co_delete(C);
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

void 
coroutine_resume(struct schedule * S, int id) {
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY:
		getcontext(&C->ctx); //初始化 ucontext_t 结构体，将当前的上下文放到 C->ctx 里面
		C->ctx.uc_stack.ss_sp = S->stack;// 设置当前协程的运行时栈，也是共享栈。
		C->ctx.uc_stack.ss_size = STACK_SIZE;
		C->ctx.uc_link = &S->main;//如果协程执行完，则切换到 S->main 主协程中进行执行。如果不设置, 则默认为 NULL，那么协程执行完，整个程序就结束了。
		S->running = id;
		C->status = COROUTINE_RUNNING;
		uintptr_t ptr = (uintptr_t)S;
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		//接下来调用了 swapcontext 函数，这个函数比较简单，但也非常核心。
		//作用是将当前的上下文内容放入 S->main 中，并将 C->ctx 的上下文替换到当前上下文。
		//这样的话，将会执行新的上下文对应的程序了。在 coroutine 中, 也就是开始执行 mainfunc 这个函数。
		//(mainfunc 是对用户提供的协程函数的封装)。
		swapcontext(&S->main, &C->ctx);
		break;
	case COROUTINE_SUSPEND:
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);
		S->running = id;
		C->status = COROUTINE_RUNNING;
		swapcontext(&S->main, &C->ctx);
		break;
	default:
		assert(0);
	}
}

static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);
	if (C->cap < top - &dummy) {
		free(C->stack);
		C->cap = top-&dummy;
		C->stack = malloc(C->cap);
	}
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size);
}
//调用 coroutine_yield 可以使当前正在运行的协程切换到主协程中运行。
//此时，该协程会进入 SUSPEND 状态
//coroutine_yield 的具体实现依赖于两个行为：
//    调用 _save_stack 将当前协程的栈保存起来。因为 coroutine 是基于共享栈的，所以协程的栈内容需要单独保存起来。
//    swapcontext 将当前上下文保存到当前协程的 ucontext 里面，同时替换当前上下文为主协程的上下文。 这样的话，当前协程会被挂起，主协程会被继续执行。

void
coroutine_yield(struct schedule * S) {
	int id = S->running;
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C,S->stack + STACK_SIZE);
	C->status = COROUTINE_SUSPEND;
	S->running = -1;
	swapcontext(&C->ctx , &S->main);
}

int 
coroutine_status(struct schedule * S, int id) {
	assert(id>=0 && id < S->cap);
	if (S->co[id] == NULL) {
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

int 
coroutine_running(struct schedule * S) {
	return S->running;
}

