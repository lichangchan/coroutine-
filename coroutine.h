#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

struct schedule;

typedef void (*coroutine_func)(struct schedule *, void *ud);
//负责创建并初始化一个协程调度器
struct schedule * coroutine_open(void);
 //负责销毁协程调度器以及清理其管理的所有协程
void coroutine_close(struct schedule *);
// 负责创建并初始化一个新协程对象，同时将该协程对象放到协程调度器里面。
int coroutine_new(struct schedule *, coroutine_func, void *ud);
//切入该协程
void coroutine_resume(struct schedule *, int id);
int coroutine_status(struct schedule *, int id);
int coroutine_running(struct schedule *);
//切出该协程
void coroutine_yield(struct schedule *);

#endif
