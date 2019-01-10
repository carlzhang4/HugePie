#include <zjunix/pc.h>
#include <zjunix/pid.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/syscall.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/utils.h>
#include <zjunix/log.h>
#include <driver/ps2.h>

//各优先级队列和退出进程队列,退出进程队列就是task_lists[PRI_QUEUE_NUM]
task_node *task_lists[PRI_QUEUE_NUM + 1], *exit_list;
//指向当前进程结构体
task_struct *current_task;
//存储可用pid的AVL树根结点
pid_node *pid_tree;
//邮箱
mailbox_owner *mailbox_tree;

//debug用
#ifdef PC_DEBUG
int debug_cnt = 1;
#endif

//将src所指向的寄存器信息复制到dest所指向的地址处
static void copy_context(context* src, context* dest) {
    dest->epc = src->epc;
    dest->at = src->at;
    dest->v0 = src->v0;
    dest->v1 = src->v1;
    dest->a0 = src->a0;
    dest->a1 = src->a1;
    dest->a2 = src->a2;
    dest->a3 = src->a3;
    dest->t0 = src->t0;
    dest->t1 = src->t1;
    dest->t2 = src->t2;
    dest->t3 = src->t3;
    dest->t4 = src->t4;
    dest->t5 = src->t5;
    dest->t6 = src->t6;
    dest->t7 = src->t7;
    dest->s0 = src->s0;
    dest->s1 = src->s1;
    dest->s2 = src->s2;
    dest->s3 = src->s3;
    dest->s4 = src->s4;
    dest->s5 = src->s5;
    dest->s6 = src->s6;
    dest->s7 = src->s7;
    dest->t8 = src->t8;
    dest->t9 = src->t9;
    dest->hi = src->hi;
    dest->lo = src->lo;
    dest->gp = src->gp;
    dest->sp = src->sp;
    dest->fp = src->fp;
    dest->ra = src->ra;
}

//打印错误信息
void print_error(char *msg)
{
    kernel_printf("%s\n", msg);
}

//初始化，创建idle进程
void init_pc()
{
    //初始化队列链表
    list_init();

    //初始化用于管理pid的avl树
    pid_tree =  Init_pid();

    //初始化消息邮箱
    mailbox_tree = init_mailbox();
    
    task_struct *idle = kmalloc(sizeof(task_struct));
    //分配最小的pid，即0
    idle->pid = IDLE_PID;    
    idle->state = PC_INIT;
    idle->ASID = idle->pid;
    kernel_strcpy(idle->name, "idle");
    idle->level = PRI_QUEUE_NUM-1;
    idle->fixed = 1;
    idle->time_cnt = TIME_UNIT * (idle->level + 1);
    int ret = add_task_to_list(idle->level, idle);

    if(ret != 0)
    {
        print_error("Process init fails!");
        return;
    }
    else
    {
        idle->state = PC_READY;
        current_task = idle;

        //注册杀死当前进程的系统调用
        register_syscall(10, pc_kill_syscall);
        //注册时钟中断，使其引发pc_schedule
        register_interrupt_handler(7, pc_schedule);
        //设置时钟中断相关的寄存器
        asm volatile(
            "li $v0, 1000000\n\t"
            "mtc0 $v0, $11\n\t"
            "mtc0 $zero, $9");
    }
}

//进程调度函数，实行多级反馈队列算法（修改版），每个队列内使用RR算法。
void pc_schedule(unsigned int status, unsigned int cause, context* pt_context)
{
    // kernel_printf("   %d\n", current_task->pid);
    task_node *current_node = list_find_by_pid(current_task->level, current_task->pid);
    task_node *temp = NULL;

    task_struct *next = pc_find_next();
    if(next == NULL)
    {
        print_error("Error occurs during pc_schedule, no available processes!");
        pc_clear_exit();
        while(1)
            ;        
    }

    if(current_task->state == PC_FINISH){
        list_delete(current_node->level, current_node);
        add_task_to_list(PROC_EXIT_LEVEL, current_task);
    }

    if(current_node == NULL)
    {
        print_error("Error occurs during pc_schedule, current process is wrong!");
        kernel_printf("%d", current_task);
        pc_clear_exit();
        while(1)
            ;
    }


    if(next != current_task)
    {
        //如果当前进程被固定住了或是处于最低优先级,则不改变其位置
        if(current_task->fixed == 1 || current_task->level==PRI_QUEUE_NUM -1 || current_task->state==PC_FINISH) ;
            //list_move_to_end(current_node);
        else
        {
            //减少进程的剩余时间片
            current_task->time_cnt--;
            if(current_task->time_cnt == 0)
            {
                list_delete(current_node->level, current_node);
                add_task_to_list(current_task->level+1, current_task);
                current_task->level++;
                current_task->time_cnt = TIME_UNIT*(current_task->level+1);
            }
        }
        
        if(current_task->state != PC_FINISH)
        {
            //保存当前进程上下文至其task_struct中
            copy_context(pt_context, &(current_task->context));
            current_task->state = PC_READY;
        }
        current_task = next;
        //将下一个进程的task_struct中的上下文加载至pt_context中
        copy_context(&(current_task->context), pt_context);
        next->state = PC_RUNNING;
    }
    else //意味着当前进程与下一个进程相同,也就是只有idle进程
    {
        if(current_task->pid != IDLE_PID)
        {
            print_error("Idle process is missing!");
            pc_clear_exit();
            current_task = NULL;
            while(1)
            ;
        }
    }

        //清理结束进程列表
    if(pc_clear_exit() != 0)
    {
        print_error("Error occurs when clearing the exit list!");
        while(1)
            ;
    }

    //将cp0中到count寄存器复位为0，结束时钟中断
    asm volatile("mtc0 $zero, $9\n\t");
}

//寻找在某一优先级队列可用的进程
task_node *pc_find_available(int level)
{
    task_node *node = NULL;

    for(node = task_lists[level]; node != NULL; node = node->next)
    {
        if(node->task != NULL && node->task->state==PC_READY)
            break;
    }
    
    return node;
}

//找到下一个用于运行的进程
task_struct *pc_find_next()
{
    task_node *current_node = list_find_by_pid(current_task->level, current_task->pid);
    task_node *next_task = NULL;
    int level;
    if(current_node == NULL)
    {
        return NULL;
    }

    // if(current_node->next != NULL)
    //     next_task = current_node->next;
    // else
    //     next_task = task_lists[current_node->level]->next;
    // // else if((next_task = pc_find_available(current_node->level)) == NULL)
    // //     next_task = current_node;
    // return next_task->task;

    //当前是链表的最后一个元素且不在最低一级，则从更低一级队列去找
    if(current_node->next == NULL && current_node->level < PRI_QUEUE_NUM - 1)  
    {
        level = current_node->level + 1;
        while(level < PRI_QUEUE_NUM)
        {
            next_task = pc_find_available(level);
            if(next_task != NULL)
                break;
            else
                level++;
        }
        if(next_task != NULL && next_task->task != NULL)
        {
            list_move_to_end(next_task);
            return next_task->task;
        }
    }

    //否则从更高优先级队列开始找，若更高优先级的没有可用的，则选择本队列内的下一个
    level = 0;
    while(level < current_node->level)
    {
        next_task = pc_find_available(level);
        if(next_task != NULL)
            break;
        else
            level++;
    }
    if(next_task == NULL)
    {
        if(current_node->next != NULL && current_node->next->task != NULL && current_node->next->task->state == PC_READY)
            next_task = current_node->next;
        else if((next_task = pc_find_available(current_node->level)) == NULL)
            next_task = current_node;
    }
    
    return next_task->task; 
}

//清除退出进程队列，成功则返回0
int pc_clear_exit()
{
    task_struct *task = NULL;
    if(exit_list == NULL)
    {
        print_error("Error: exit list is missing!");
        return -1;
    }

    while(exit_list->next != NULL && exit_list->next->task != NULL)
    {
        if(exit_list->next->task->state != PC_FINISH)
        {
            print_error("Error: Unfinished task %d appears in exit list!");
            return -1;
        }
        else
        {
            //删除可能存在的mailbox
            Destroy_mailbox(&mailbox_tree, exit_list->next->task->pid);
            //删除pid
            Del_pid(&pid_tree, exit_list->next->task->pid);
            task = exit_list->next->task;
            //从exit_list中删除节点,要先删除节点再释放task空间
            list_delete(PROC_EXIT_LEVEL, exit_list->next);
            //清理进程空间
            kfree(task);
        }
    }

    return 0;
}

//用于进程的创建，若成功则返回pid
int pc_create(void (*func)(), char* name, int level, int fixed, int mpid)
{
    if(level < 0 || level >= PRI_QUEUE_NUM || (fixed != 0 && fixed != 1))
    {
        kernel_printf("Process creating fails!\n");
        return -1;
    }
    int pid;

    if(mpid >= 0 && Check_pid(pid_tree ,mpid) == 0)
    {
        pid = mpid;
        
        if(Insert_pid(&pid_tree, pid) < 0)
            return -1;
    }
    else
        pid = Alloc_pid(&pid_tree);
    unsigned int init_gp;
    
    //获得的pid小于0意味着无可用pid
    if(pid < 0)
    {
        print_error("No available PIDs!");
        return -1;
    }
    if(level >= PRI_QUEUE_NUM || (fixed != 0 && fixed != 1))
    {
        print_error("Wrong arguments of level and fixed!");
        return -1;
    }
    // task_union *new = (task_union*)kmalloc(sizeof( task_union));
    task_struct *new_task = (task_struct*)kmalloc(sizeof(task_struct));
    new_task->pid = pid;
    new_task->state = PC_INIT;
    new_task->ASID = pid;
    new_task->context.epc = (unsigned int)func;
    new_task->context.sp = (unsigned int)kmalloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
    //获得当前gp
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
    new_task->context.gp = init_gp;
    kernel_strcpy(new_task->name, name);
    new_task->level = level;
    new_task->fixed = fixed;
    new_task->time_cnt = TIME_UNIT*(level+1);
    new_task->state = PC_READY;
    add_task_to_list(level, new_task);

    return pid;
}

//杀死当前进程的系统调用
void pc_kill_syscall(unsigned int status, unsigned int cause, context* pt_context)
{
    task_node *current_node = list_find_by_pid(current_task->level, current_task->pid);
    if(current_node == NULL)
    {
        print_error("Error occurs during killing current process, current process is wrong!");
        return;
    }
    else
    {
        current_task->state = PC_FINISH;
        // disable_interrupts();
        pc_schedule(status, cause, pt_context);
        // enable_interrupts();
    }
}

//用pid杀死指定进程
int pc_kill(int pid)
{
    if(pid < 0)
    {
        print_error("PID must be a non-negative integer!");
        return -1;
    }
    if(!Check_pid(pid_tree ,pid))
    {
        print_error("PID doesn't exsist");
        return -1;
    }
    //idle进程不能被杀死
    if (pid == 0) {
        print_error("Idle process cannot be killed!");
        return -1;
    }

    //kernel进程不能被杀死 
    if (pid == 1) {
        print_error("Kernel processes cannot be killed!");
        return -1;
    }

    //进程不能杀死自身
    if (pid == current_task->pid) {
        print_error("You can't kill current task!");
        return -1;
    } 

    //关中断
    disable_interrupts();

    int level;
    task_node *target_node = NULL;
    //寻找该pid对应的节点
    for(level = 0; level < PRI_QUEUE_NUM; level++)
    {
        target_node = list_find_by_pid(level, pid);
        if(target_node != NULL)
            break;
    }
    if(target_node == NULL)
    {
        print_error("Target process not found!");
        enable_interrupts();
        return -1;
    }

    target_node->task->state = PC_FINISH;
    add_task_to_list(PROC_EXIT_LEVEL, target_node->task);
    list_delete(target_node->level, target_node);

    enable_interrupts();
}

//获得current_task结构体指针
task_struct* get_curr_pcb()
{
    return current_task;
}

//打印出每一级队列中的进程信息
int print_proc()
{
    int level;
    task_node *temp;

    // disable_interrupts();
    if(current_task == NULL)
    {
        print_error("Current process is NULL!");
        return -1;
    }
    // kernel_printf("Current process:\n");
    // print_certain_proc(current_task->pid);
    
    for(level = 0; level <= PRI_QUEUE_NUM; level++)
    {
        if(level == PRI_QUEUE_NUM)
            kernel_printf("Exit Queue : ");
        else
            kernel_printf("Priority Queue %d : ", level);
        for(temp = task_lists[level]; temp != NULL; temp = temp->next)
        {
            // log(LOG_START, "Print begin");
            if(temp->task == NULL)
            {
                // log(LOG_OK, "Print ok0");
                kernel_printf("head  ");
                // log(LOG_OK, "Print ok1");
            }
            else
            {
                // log(LOG_OK, "Print ok2");
                kernel_printf("PID:%d NAME:%s ", temp->task->pid, temp->task->name);
                // log(LOG_OK, "Print ok3");
            }
            // log(LOG_START, "Print end");
        }
        kernel_printf("\n");
        sleep(1000);
    }

    // enable_interrupts();
    return 0;
}

//根据pid打印出某一特定进程的详细信息
int print_certain_proc(int pid)
{
    if(Check_pid(pid_tree, pid) == 0)
        return -1;
    if(pid < 0)
    {
        print_error("PID must be a non-negative integer!");
        return -1;
    }

    int level;
    task_node *target_node = NULL;
    //寻找该pid对应的节点
    for(level = 0; level < PRI_QUEUE_NUM; level++)
    {
        target_node = list_find_by_pid(level, pid);
        if(target_node != NULL)
            break;
    }
    if(target_node == NULL)
    {
        print_error("Target process not found!");
        return -1;
    }

    //进程的状态信息
    char state[10];
    switch(target_node->task->state)
    {
        case(0): kernel_strcpy(state, "INIT");
                 break;
        case(1): kernel_strcpy(state, "READY");
                 break;
        case(2): kernel_strcpy(state, "WAITING");
                 break;
        case(3): kernel_strcpy(state, "RUNNING");
                 break;
        case(4): kernel_strcpy(state, "FINISH");
                 break;
        default: kernel_strcpy(state, "ERROR");
    }
    char pre[10], next[10];
    if(target_node->previous == NULL)
        kernel_strcpy(pre, "NULL");
    else if(target_node->previous->task == NULL)
        kernel_strcpy(pre, "head");
    else
        kernel_strcpy(pre, target_node->previous->task->name);
    if(target_node->next == NULL || target_node->next->task == NULL)
        kernel_strcpy(next, "NULL");
    else
        kernel_strcpy(next, target_node->next->task->name);

    kernel_printf("PID: %d\n", pid);
    // sleep(1000);
    kernel_printf("NAME: %s\n", target_node->task->name);
    // sleep(1000);
    kernel_printf("LEVEL: %d\n", target_node->level);
    // sleep(1000);
    kernel_printf("FIXED: %d\n", target_node->task->fixed);
    // sleep(1000);
    kernel_printf("STATE: %s\n", state);
    // sleep(1000);
    kernel_printf("REMAINING TIME: %d\n", target_node->task->time_cnt);
    // sleep(1000);
    kernel_printf("Previous: %s   Next: %s\n", pre, next);
    // sleep(1000);

    return 0;
}

void list_init()
{
    int i;

    for(i = 0; i <= PRI_QUEUE_NUM; i++)  //三个队列的第一个节点都对应着空进程，表头
    {
        task_lists[i] = (task_node*)kmalloc(sizeof(task_node));
        task_lists[i]->next = NULL;
        task_lists[i]->previous = NULL;
        task_lists[i]->task = NULL;
        task_lists[i]->level = i;
    }

    exit_list = task_lists[PROC_EXIT_LEVEL];
}

//找到一个进程在某一级级队列中的对应的节点
task_node* list_find(int level, task_struct *proc)
{
    if(proc == NULL)
        return NULL;
    
    task_node *node = NULL;
    for(node = task_lists[level]->next; node!=NULL; node = node->next)
    {
        if(node->task->pid == proc->pid)
            break;
    }

    return node;
}

//根据pid找到一个进程在某一级级队列中的对应的节点
task_node* list_find_by_pid(int level, int pid)
{
    if(pid < 0)   //pid都是非负整数
        return NULL;
    
    task_node *node = NULL;
    for(node = task_lists[level]->next; node!=NULL && node->task!=NULL; node = node->next)
    {
        if(node->task->pid == pid)
            break;
    }

    return node;
}

//将一个节点移到其所在队列的末尾，返回值为0表示成功
int list_move_to_end(task_node *node)
{
    if(node == NULL || node->task == NULL)
        return -1;
    if(node->next == NULL)   //node已经在末尾了
        return 0;

    int level = node->level;
    task_node *tail = NULL;
    for(tail = task_lists[level]; tail!=NULL && tail->next!=NULL; tail = tail->next);

    if(node->previous != NULL)
        node->previous->next = node->next;
    if(node->next != NULL)
        node->next->previous = node->previous;
    node->previous = tail;
    node->next = NULL;
    tail->next = node;

    return 0;
}

//将一个节点移到其所在队列的开头(空节点之后)，返回值为0表示成功
int list_move_to_head(task_node *node)
{
    if(node == NULL || node->task == NULL)
        return -1;
    if(node->previous->task == NULL)   //node已经在开头了(空节点之后)
        return 0;

    int level = node->level;
    task_node *head = task_lists[level];

    if(node->previous != NULL)
        node->previous->next = node->next;
    if(node->next != NULL)
        node->next->previous = node->previous;
    if(head->next != NULL)
        head->next->previous = node;
    node->next = head->next;
    node->previous = head;
    head->next = node;

    return 0;
}

//将一个节点插入到某一级队列的末尾，返回值为0表示成功
int list_insert(int level, task_node *node)
{
    if(node == NULL)
        return -1;

    task_node *tail = NULL;
    for(tail = task_lists[level]; tail!=NULL && tail->next!=NULL; tail = tail->next);
    tail->next = node;
    node->previous = tail;
    node->next = NULL;
    node->level = level;
    return 0;
}

//从某一级队列中删除某一节点，返回值为0表示成功
int list_delete(int level, task_node *node)
{
    if(node == NULL || node->task == NULL) //空任务节点不允许删除
        return -1;
    
    int i;
    task_node *target_node = NULL;
    
    target_node = list_find_by_pid(level, node->task->pid);
    
    if(target_node == NULL)
        return -1;
    if(target_node->next != NULL)
        target_node->next->previous = target_node->previous;
    if(target_node->previous != NULL)
        target_node->previous->next = target_node->next;
    kfree(target_node);
    
    return 0;
}

//把一个进程加入某一级队列的末尾
int add_task_to_list(int level, task_struct *task)
{
    if(task == NULL)
        return -1;
    
    task_node *node = (task_node *)kmalloc(sizeof(task_node));
    node->level = level;
    node->task = task;
    list_insert(level, node);

    return 0;
}

//给当前进程注册一个mailbox
int pc_register_mailbox()
{
    return Register_mailbox(&mailbox_tree, current_task->pid);
}

//删除当前进程可能存在的mailbox
int pc_destroy_mailbox()
{
    return Destroy_mailbox(&mailbox_tree, current_task->pid);
}

//向一个指定pid的进程(其必须已注册邮箱)发送消息,成功则返回0
int pc_send_mail(int dst, char message[MAIL_LENGTH])
{   
    return SendMail(mailbox_tree, current_task->pid, dst, message);
}

int pc_read_mail(int *src, char message[MAIL_LENGTH])
{
    return ReadMail(mailbox_tree, current_task->pid, src, message);
}