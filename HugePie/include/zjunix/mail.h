#ifndef _ZJUNIX_MAIL_H
#define _ZJUNIX_MAIL_H
#include "avl.h"
#ifndef NULL
#define NULL    0
#endif
#define MAILBOX_NUM     128      //最多128mailbox
#define MAILBOX_SIZE   4
#define MAIL_LENGTH     100

typedef struct AVLTreeNode mailbox_owner;

//邮件结构
typedef struct mail mail;
struct mail
{
    //来源进程pid和目标进程pid
    int src;
    int dst;
    //消息
    char message[MAIL_LENGTH];
    
    mail *next, *previous;
};

typedef struct mailbox mailbox;

struct mailbox
{
    mail *head, *tail;
    int length;
};

mailbox_owner *init_mailbox();

//分配一个mailbox，实质上插入一个最大pid+1的节点
int Register_mailbox(mailbox_owner **head, int pid);

//将一个mailbox从树中删除
int Destroy_mailbox(mailbox_owner **head, int pid);

//查找一个进程的mailbox，若不存在则返回NULL
mailbox_owner* FindMailbox(mailbox_owner *head, int pid);

int CheckMailbox(mailbox_owner *head, int pid);

int SendMail(mailbox_owner *head, int src, int dst, char message[MAIL_LENGTH]);

int ReadMail(mailbox_owner *head, int pid, int *src, char message[MAIL_LENGTH]);

#endif
