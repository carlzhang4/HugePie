/*用AVL树来维护pid，初始化时将所有未被分配的pid号插入树中，
之后每次分配标号最小的pid并删除节点，进程结束时则再次将pid插入回来*/
#ifndef _ZJUNIX_PID_H
#define _ZJUNIX_PID_H

#ifndef NULL
#define NULL    0
#endif
#define PID_NUM     128      //最多128个进程
#define IDLE_PID    0        //给idle进程分配一个最小的pid

typedef int Type;

typedef struct AVLTreeNode{
    Type key;                    // 关键字(键值)
    int height;
    struct AVLTreeNode *left;    // 左孩子
    struct AVLTreeNode *right;    // 右孩子
}*AVLTree, pid_node;

int avltree_height(AVLTree tree);

/*以下是和avl树相关的各种操作*/
//查找"AVL树x"中键值为key的节点
pid_node* avltree_search(AVLTree x, Type key);
// 查找最小结点：返回tree为根结点的AVL树的最小结点。
pid_node* avltree_minimum(AVLTree tree);
// 查找最大结点：返回tree为根结点的AVL树的最大结点。
pid_node* avltree_maximum(AVLTree tree);
// 将结点插入到AVL树中，返回根节点
pid_node* avltree_insert(AVLTree tree, Type key);
// 删除结点(key是节点值)，返回根节点
pid_node* avltree_delete(AVLTree tree, Type key);
// 销毁AVL树
void destroy_avltree(AVLTree tree);

//初始化AVL树，返回其头节点
pid_node* Init_pid();

//分配一个pid，实质上是从BST中删去最小节点，将其数值返回
int Alloc_pid(pid_node **head);

//将一个pid插入BST中
int Del_pid(pid_node **head, int pid);

//检查一个pid是否已被分配
int Check_pid(pid_node *head, int pid);

#endif
