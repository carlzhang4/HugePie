#include <zjunix/mail.h>
#include <zjunix/avl.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/syscall.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/utils.h>
#include <zjunix/log.h>

mailbox_owner *init_mailbox()
{
    Type element;
    element.p = NULL;
    element.value = -1;
    mailbox_owner *result = avltree_create_node(element, NULL, NULL);

    return result;
}

//分配一个mailbox,成功则返回0，否则返回-1
int Register_mailbox(mailbox_owner **head, int pid)
{
    if(head == NULL || *head == NULL || FindMailbox(*head, pid)!=NULL)
        return -1;
    
    Type element;
    int i = 0;
    element.p = (mailbox*)kmalloc(sizeof(mailbox));
    for(i = 0;i < MAILBOX_SIZE; i++)
    {
        ((mailbox*)(element.p))->head = (mail*)kmalloc(sizeof(mail));
        //为了标识头节点，将dst,src都设为-1
        ((mailbox*)(element.p))->head->dst = -1;
        ((mailbox*)(element.p))->head->src = -1;
        ((mailbox*)(element.p))->head->next = ((mailbox*)(element.p))->head->previous = NULL;
        ((mailbox*)(element.p))->tail = ((mailbox*)(element.p))->head;
        ((mailbox*)(element.p))->length = 0;
    }
    element.value = pid;
    *head = avltree_insert(*head, element);
    return 0;
}

//删除一个mailbox,需要删除消息队列中的所有消息
int Destroy_mailbox(mailbox_owner **head, int pid)
{
    if(head == NULL || *head == NULL)
        return -1;

    mailbox_owner *target = FindMailbox(*head, pid);
    if(target == NULL || ((mailbox*)target->key.p)->head == NULL)
        return -1;

    mail *first = ((mailbox*)target->key.p)->head;
    mail *temp = NULL;
    while(first->next != NULL)
    {
        temp = first->next;
        first->next = temp->next;
        kfree(temp);
    }
    kfree(first);
    kfree(target);

    Type element;
    element.p = NULL;
    element.value = pid;
    *head = avltree_delete(*head, element);
    return 0;
}

//根据pid找到一个可能存在的mailbox
mailbox_owner* FindMailbox(mailbox_owner *head, int pid)
{
    if(head == NULL || pid < 0)
        return NULL;
    else
    {
        Type element;
        element.p = NULL;
        element.value = pid;
        return avltree_search(head, element);
    }
}

//检查一个进程的mailbox是否存在，是则返回1，不是则返回0
int CheckMailbox(mailbox_owner *head, int pid)
{
    return FindMailbox(head, pid) != NULL;
}

int SendMail(mailbox_owner *head, int src, int dst, char message[MAIL_LENGTH])
{
    // log(LOG_START, "BEGIN_FIND\n");
    mailbox_owner *target = FindMailbox(head, dst);
    // log(LOG_END, "END_FIND\n");
    if(target == NULL || ((mailbox*)target->key.p)->head == NULL)
        return -1;

    if(((mailbox*)target->key.p)->length >= MAILBOX_SIZE)
        return -1;
    // log(LOG_START, "BEGIN_SENDMAIL\n");
    ((mailbox*)target->key.p)->length++;
    mail *tail = ((mailbox*)target->key.p)->tail;
    tail->next = (mail*)kmalloc(sizeof(mail));
    if(tail->previous != NULL)
        tail->previous->next = tail->next;
    tail->next->previous = tail;
    tail = tail->next;
    tail->next = NULL;
    tail->src = src;
    kernel_memcpy(tail->message, message, MAIL_LENGTH);
    // log(LOG_END, "END_SENDMAIL\n");
    return 0;
}

int ReadMail(mailbox_owner *head, int pid, int *src, char message[MAIL_LENGTH])
{
    // log(LOG_START, "BEGIN_FIND\n");
    mailbox_owner *target = FindMailbox(head, pid);
    // log(LOG_END, "END_FIND\n");
    if(target == NULL || ((mailbox*)target->key.p)->head == NULL)
        return -1;

    if(((mailbox*)target->key.p)->length <= 0)
        return -1;
    // log(LOG_START, "BEGIN_READMAIL\n");
    mail *temp = ((mailbox*)target->key.p)->head->next;
    if(temp == NULL)
        return -1;
    kernel_memcpy(message, temp->message, MAIL_LENGTH);
    // log(LOG_STEP, "%s\n", temp->message);
    *src = temp->src;
    // log(LOG_STEP, "STEP1");
    if(temp->next != NULL)
        temp->next->previous = temp->previous;
    // log(LOG_STEP, "STEP2");
    temp->previous->next = temp->next;
    kfree(temp);
    ((mailbox*)target->key.p)->length--;
    // log(LOG_END, "END_READMAIL\n");
    return 0;
}