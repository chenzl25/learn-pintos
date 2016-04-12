#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
// 用于检查stack-overflow的magic number
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
// 准备队列
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
// 所有队列
static struct list all_list;

/* Idle thread. */
// idle线程
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
// initail线程
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
// 给allocate_tid()用的锁
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
// kernel线程的栈帧结构
// 包括了返回地址，要调用的函数，函数的辅助值(函数参数)
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
// 统计数据，idle线程的ticks
static long long idle_ticks;    /* # of timer ticks spent idle. */
// 内核线程的ticks
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
// user线程的ticks
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
// 线程调度的时候每个线程能用的ticks数
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
// 自从上次线程yield到现在的ticks
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
// 默认为false的是否使用多层反馈队列调度的标志
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
// 线程系统的初始化，初始化ready队列，运行一个main(init)线程
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// 通过开启中断来实现线程的抢占
void
thread_start (void) 
{
  /* Create the idle thread. */
  // 使用semaphore来控制同步
  struct semaphore idle_started;
  // 初始化semaphore为0
  sema_init (&idle_started, 0);
  // idle线程的进行
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  // 开启线程调度
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  // 通过使用semaphore来等待idle运行完
  sema_down (&idle_started);
}

// 如果线程是阻塞的话就对block时间进行减1,为0就加入ready队列 
void 
dec_block_ticks_if_thread_bolocked(struct thread *t, void *aux UNUSED) {
  ASSERT(is_thread(t));
  if (t->status == THREAD_BLOCKED) {
    t->block_ticks--;
    if (t->block_ticks == 0) {
      thread_unblock(t);
    }
  }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// timer的每次tick的中断handler会调用该函数，这个函数是运行再硬中断的环境中的
void
thread_tick (void) 
{
  // 获取当前的线程
  struct thread *t = thread_current ();

  /* Update statistics. */
  // 根据线程的类型来对对应的tick数据进行统计
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  // 这里增加检查线程阻塞的逻辑
  thread_foreach (dec_block_ticks_if_thread_bolocked, 0);
  /* Enforce preemption. */
  // 如果当前的线程运行的tick超过了TIME_SLICE这个时间片就会强行抢占
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
// 打印线程的统计量
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
// 创建一个线程
// 需要提供线程的名字，优先级，执行的函数和函数参数
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  // allocate一个线程(也就是一个page)
  t = palloc_get_page (PAL_ZERO);
  // 如果申请page失败就是线程创建失败了
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  // 初始化这个线程
  init_thread (t, name, priority);
  // 获取tid
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  // 先禁止中断，初始化线程的栈，这样原子的操作是为了让stack的中间态不暴露出去，
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  // 内核栈帧空间的申请
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  // 申请switch_entry
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  // 申请switch_threads
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
  // 恢复中断原来的状态
  intr_set_level (old_level);

  /* Add to run queue. */
  // 把线程加入到准备队列，这里就会马上进行调度了，尽管还没返回
  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
// 阻塞线程，该线程会停止被调度，直到thread_unblock()被调用
void
thread_block (void) 
{
  // 软中断
  ASSERT (!intr_context ());
  // 中断禁止了
  ASSERT (intr_get_level () == INTR_OFF);
  // 修改当前线程的状态为block状态
  thread_current ()->status = THREAD_BLOCKED;

  // 调度其他线程
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
// 通过线程id把处于bock状态的线程唤醒到准备运行的状态
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));
  // 断言要unblock的线程的block_tick为0了
  // ASSERT (t->block_ticks == 0);
 
  // 禁止中断
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  // 把线程放到ready队列
  list_push_back (&ready_list, &t->elem);
  // 修改线程状态到ready
  t->status = THREAD_READY;
  // 恢复中断禁止前的状态
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
// 返回当前线程的名字
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
// 返回当前在运行着的线程
struct thread *
thread_current (void) 
{
  // 获取正在运行的线程
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  // 断言获得的是当前线程，因为如果线程拥有的stack overflow了的话就导致奇怪的结果
  // 所以要检查 
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
// 返回当前线程的tid
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
// 让当前线程退出
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 让当前的线程交出CPU
void
thread_yield (void) 
{
  // 获取当前的线程
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  // 断言当前的中断是软中断
  ASSERT (!intr_context ());
  // 禁止中断
  old_level = intr_disable ();
  // 如果当前的线程不是idle线程就把线程加入到ready队列中
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  // 修改线程状态
  cur->status = THREAD_READY;
  // 开始调度
  schedule ();
  // 恢复中断
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
// 让每个线程都执行下func函数
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// 设置当前线程的优先级
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
// 返回当前线程的优先级
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
// 设置当前线程的nice值
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
// 返回当前线程的优先级
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
// 返回100乘以系统平均负载
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
// 返回100乘以当前线程的recent_cpu值
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
// idle线程的执行函数
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
// 线程执行的函数
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  // 开启中断
  intr_enable ();       /* The scheduler runs with interrupts off. */
  // 运行函数
  function (aux);       /* Execute the thread function. */
  // 返回的时候kill掉线程
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
// 返回正在运行着的线程
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  // 通过汇编获取CPU的栈指针，再通过pg_round_down来获取当前线程的指针
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
// 通过线程的magic来判断是否是线程
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
// 初始化一个线程
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);
  // t也就是一个page，全部设置为0
  memset (t, 0, sizeof *t);
  // 先初始化状态为block
  t->status = THREAD_BLOCKED;
  // 初始化名字
  strlcpy (t->name, name, sizeof t->name);
  // 初始化stack的size
  t->stack = (uint8_t *) t + PGSIZE;
  // 初始化优先级
  t->priority = priority;
  // 初始化magic-number
  t->magic = THREAD_MAGIC;
  // 初始化线程的阻塞时间为0
  t->block_ticks = 0;
  // 加入到全体线程队列中
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
// 在线程栈里设置一个frame，随便减少stack的size，返回frame的base
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
// 如果ready队列为空就返回idle_thead，否则取出ready队列的第一个线程来返回
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  // 获取当前运行的线程，这里再运行的线程是switch后的线程了
  struct thread *cur = running_thread ();
  // 断言中断已经被关闭
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  // 设置current-thread为正在运行
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  // 重新计算线程的ticks，重新计算线程时间切换片
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  // 调用process_activate触发新的地址空间
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
     // 如果我们切换的线程是dying的话就进行palloc_free_page
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      // 利用page管理机制来清理掉prev指向的page
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  // 获取当前的线程和下一个要切换到的线程
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  // 断言断言关闭了，当前的线程已经加入到ready队列，next是一个线程
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));
  // 如果不是只剩下一个线程，就进行切换
  if (cur != next)
    prev = switch_threads (cur, next);
  // 最后处理
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
// 申请tid，并返回，这里用了lock来实现同步
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
