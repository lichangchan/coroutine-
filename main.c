//源码剖析 https://www.cyhone.com/articles/analysis-of-cloudwu-coroutine/

#include "coroutine.h"
#include <stdio.h>

struct args {
	int n;
};

static void foo(struct schedule * S, void *ud) {
	struct args * arg = ud;
	int start = arg->n;
	int i;
	for (i=0;i<5;i++) {
		printf("coroutine %d : %d\n",coroutine_running(S) , start + i);
		// 切出当前协程
		coroutine_yield(S);
	}
}
/*
static void foo(struct schedule * S, void *ud) {
	struct args * arg = ud;
	int start = arg->n;
	int i;
	for (i=0;i<5;i++) {
		printf("coroutine %d : %d\n",coroutine_running(S) , start + i);
		// 切出当前协程
		coroutine_yield(S);
	}
}

*/





//添加注释




void ldd(){
	
	int a=0;
	int b=0;
	int c=0;
	
	
}
static void test(struct schedule *S) {
	struct args arg1 = {0};
	struct args arg2 = {100};

	// 创建两个协程
	int co1 = coroutine_new(S, foo, &arg1);
	int co2 = coroutine_new(S, foo, &arg2);

	printf("main start\n");
	while (coroutine_status(S,co1) && coroutine_status(S,co2)) {
		// 使用协程 co1
		coroutine_resume(S,co1);
		// 使用协程 co2
		coroutine_resume(S,co2);
	}
	printf("main end\n");
}

int main() {
	// 创建一个协程调度器
	struct schedule * S = coroutine_open();

	test(S);

	// 关闭协程调度器
	coroutine_close(S);

	return 0;
}