/*用AVL树来维护pid，初始化时将所有未被分配的pid号插入树中，
之后每次分配标号最小的pid并删除节点，进程结束时则再次将pid插入回来*/
#ifndef _ZJUNIX_PID_H
#define _ZJUNIX_PID_H
#include "avl.h"
#ifndef NULL
#define NULL    0
#endif
#define PID_NUM     128      //最多128个进程
#define IDLE_PID    0        //给idle进程分配一个最小的pid

typedef struct AVLTreeNode pid_node;

//初始化AVL树，返回其头节点
pid_node* Init_pid();

//分配一个pid，实质上插入一个最大pid+1的节点
int Alloc_pid(pid_node **head);

//将一个指定pid的节点插入树中
int Insert_pid(pid_node **head, int pid);

//将一个pid从
int Del_pid(pid_node **head, int pid);

//检查一个pid是否已被分配
int Check_pid(pid_node *head, int pid);

#endif
