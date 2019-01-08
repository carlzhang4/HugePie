#ifndef _ZJUNIX_AVL_H
#define _ZJUNIX_AVL_H

#ifndef NULL
#define NULL    0
#endif
#define IDLE_PID    0        //给idle进程分配一个最小的pid

struct Element{
    int value;
    void *p;
};
typedef struct Element Type;

typedef struct AVLTreeNode{
    Type key;                    // 关键字(键值)
    int height;
    struct AVLTreeNode *left;    // 左孩子
    struct AVLTreeNode *right;    // 右孩子
}*AVLTree, Node;

int avltree_height(AVLTree tree);

/*以下是和avl树相关的各种操作*/
Node* avltree_create_node(Type key, Node *left, Node* right);
//查找"AVL树x"中键值为key的节点
Node* avltree_search(AVLTree x, Type key);
// 查找最小结点：返回tree为根结点的AVL树的最小结点。
Node* avltree_minimum(AVLTree tree);
// 查找最大结点：返回tree为根结点的AVL树的最大结点。
Node* avltree_maximum(AVLTree tree);
// 将结点插入到AVL树中，返回根节点
Node* avltree_insert(AVLTree tree, Type key);
// 删除结点(key是节点值)，返回根节点
Node* avltree_delete(AVLTree tree, Type key);
// 销毁AVL树
void destroy_avltree(AVLTree tree);


#endif
