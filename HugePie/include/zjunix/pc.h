#ifndef _ZJUNIX_PC_H
#define _ZJUNIX_PC_H

//debug控制
#ifndef PC_DEBUG
#define PC_DEBUG
#endif  

#ifndef NULL
#define NULL    0
#endif
#define  KERNEL_STACK_SIZE  4096

//各优先级队列，可修改PRI_QUEUE_NUM来修改总数
#define PRI_QUEUE_NUM   3
#define PROC_LEVEL0     0
#define PROC_LEVEL1     1
#define PROC_LEVEL2     2
#define PROC_EXIT_LEVEL PRI_QUEUE_NUM


//一个时间单位
#define TIME_UNIT       300

//进程的五种状态
#define PC_INIT     0
#define PC_READY    1
#define PC_WAITING  2
#define PC_RUNNING  3
#define PC_FINISH   4

typedef struct              //寄存器信息
{
    unsigned int epc;
    unsigned int at;
    unsigned int v0, v1;
    unsigned int a0, a1, a2, a3;
    unsigned int t0, t1, t2, t3, t4, t5, t6, t7;
    unsigned int s0, s1, s2, s3, s4, s5, s6, s7;
    unsigned int t8, t9;
    unsigned int hi, lo;
    unsigned int gp;
    unsigned int sp;
    unsigned int fp;
    unsigned int ra;
} context;

typedef struct 
{
    int pid;
    char name[32];                 //进程名
    context context;
    int ASID;
    int state;
    int fixed;
    int time_cnt;
    int level;
} task_struct;

typedef union 
{
    task_struct task;
    unsigned char kernel_stack[4096];
} task_union;

typedef struct task_node task_node;
struct task_node
{
    task_node *next;
    task_node *previous;
    task_struct *task;
    int level;
};

//pc相关函数
void init_pc();
void pc_schedule(unsigned int status, unsigned int cause, context* pt_context);
task_struct *pc_find_next();
// int pc_peek();
int pc_clear_exit();
int pc_create(void (*func)(), char* name, int level, int fixed);
void pc_kill_syscall(unsigned int status, unsigned int cause, context* pt_context);
int pc_kill(int proc);
task_struct* get_curr_pcb();
int print_proc();
int print_certain_proc(int pid);

//链表相关函数
void list_init();
task_node* list_find(int level, task_struct *proc);
task_node* list_find_by_pid(int level, int pid);
int list_insert(int level, task_node *node);
int list_delete(int level, task_node *node);
int list_move_to_end(task_node *node);
int list_move_to_head(task_node *node);
int add_task_to_list(int level, task_struct *task);


#endif  // !_ZJUNIX_PC_H
