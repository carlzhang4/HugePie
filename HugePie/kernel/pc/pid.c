#include <zjunix/pid.h>
#include <zjunix/avl.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/syscall.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/utils.h>

//初始化AVL树，返回其头节点
pid_node* Init_pid()
{
    Type element;
    element.p = NULL;
    element.value = 0;
    pid_node *result = avltree_create_node(element, NULL, NULL);
    
    return result;
}

//分配一个pid，实质上是从AVL树中删去最小节点，将其数值返回
int Alloc_pid(pid_node **head)
{
    pid_node *max = avltree_maximum(*head);
    int pid;
    
    if(max == NULL)
        return -1;  //代表无可用节点或错误
    else
    {
        pid = max->key.value+1;
        Type element;
        element.p = NULL;
        element.value = pid;
        *head = avltree_insert(*head, element);
        return pid;
    }
}

//插入一个指定pid的节点
int Insert_pid(pid_node **head, int pid)
{
    if(pid < 0 || Check_pid(*head, pid))
        return -1;

    Type element;
    element.p = NULL;
    element.value = pid;
    *head = avltree_insert(*head, element);

    return 0;
}

//将一个pid插入AVL树中，用于回收pid
int Del_pid(pid_node **head, int pid)
{
    if(head == NULL || *head == NULL)
        return -1;
    else
    {
        Type element;
        element.p = NULL;
        element.value = pid;
        *head = avltree_delete(*head, element);
        return 0;
    }
}

//检查一个pid是否已被分配
int Check_pid(pid_node *head, int pid)
{
    int ret;
    if(head == NULL || pid < 0 || pid >= PID_NUM)
        return 0;
    else
    {
        Type element;
        element.p = NULL;
        element.value = pid;
        if(avltree_search(head, element) == NULL)
            return 0;
        else
            return 1;
    }
}