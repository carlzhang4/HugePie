#include <zjunix/pid.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/syscall.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/utils.h>

static pid_node* avltree_create_node(Type key, pid_node *left, pid_node* right)
{
    pid_node* p;

    if ((p = (pid_node *)kmalloc(sizeof(pid_node))) == NULL)
        return NULL;
    p->key = key;
    p->height = 0;
    p->left = left;
    p->right = right;

    return p;
}

#define HEIGHT(p)    ( (p==NULL) ? 0 : (((pid_node *)(p))->height) )

int avltree_height(AVLTree tree)
{
    return HEIGHT(tree);
}

#define MAX(a, b)    ( (a) > (b) ? (a) : (b) )

//以下是AVL树的各种旋转
static pid_node* left_left_rotation(AVLTree k2)
{
    AVLTree k1;

    k1 = k2->left;
    k2->left = k1->right;
    k1->right = k2;

    k2->height = MAX( HEIGHT(k2->left), HEIGHT(k2->right)) + 1;
    k1->height = MAX( HEIGHT(k1->left), k2->height) + 1;

    return k1;
}

static pid_node* right_right_rotation(AVLTree k1)
{
    AVLTree k2;

    k2 = k1->right;
    k1->right = k2->left;
    k2->left = k1;

    k1->height = MAX( HEIGHT(k1->left), HEIGHT(k1->right)) + 1;
    k2->height = MAX( HEIGHT(k2->right), k1->height) + 1;

    return k2;
}

static pid_node* left_right_rotation(AVLTree k3)
{
    k3->left = right_right_rotation(k3->left);

    return left_left_rotation(k3);
}

static pid_node* right_left_rotation(AVLTree k1)
{
    k1->right = left_left_rotation(k1->right);

    return right_right_rotation(k1);
}

pid_node* avltree_insert(AVLTree tree, Type key)
{
    if (tree == NULL) 
    {
        // 新建节点
        tree = avltree_create_node(key, NULL, NULL);
        if (tree==NULL)
        {
            return NULL;
        }
    }
    else if (key < tree->key) // 应该将key插入到"tree的左子树"的情况
    {
        tree->left = avltree_insert(tree->left, key);
        // 插入节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->left) - HEIGHT(tree->right) == 2)
        {
            if (key < tree->left->key)
                tree = left_left_rotation(tree);
            else
                tree = left_right_rotation(tree);
        }
    }
    else if (key > tree->key) // 应该将key插入到"tree的右子树"的情况
    {
        tree->right = avltree_insert(tree->right, key);
        // 插入节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->right) - HEIGHT(tree->left) == 2)
        {
            if (key > tree->right->key)
                tree = right_right_rotation(tree);
            else
                tree = right_left_rotation(tree);
        }
    }
    else //key == tree->key)
    {
        return tree;
    }

    tree->height = MAX( HEIGHT(tree->left), HEIGHT(tree->right)) + 1;

    return tree;
}

static pid_node* delete_node(AVLTree tree, pid_node *z)
{
    // 根为空 或者 没有要删除的节点，直接返回NULL。
    if (tree==NULL || z==NULL)
        return NULL;

    if (z->key < tree->key)        // 待删除的节点在"tree的左子树"中
    {
        tree->left = delete_node(tree->left, z);
        // 删除节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->right) - HEIGHT(tree->left) == 2)
        {
            pid_node *r =  tree->right;
            if (HEIGHT(r->left) > HEIGHT(r->right))
                tree = right_left_rotation(tree);
            else
                tree = right_right_rotation(tree);
        }
    }
    else if (z->key > tree->key)// 待删除的节点在"tree的右子树"中
    {
        tree->right = delete_node(tree->right, z);
        // 删除节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->left) - HEIGHT(tree->right) == 2)
        {
            pid_node *l =  tree->left;
            if (HEIGHT(l->right) > HEIGHT(l->left))
                tree = left_right_rotation(tree);
            else
                tree = left_left_rotation(tree);
        }
    }
    else    // tree是对应要删除的节点。
    {
        // tree的左右孩子都非空
        if ((tree->left) && (tree->right))
        {
            if (HEIGHT(tree->left) > HEIGHT(tree->right))
            {
                pid_node *max = avltree_maximum(tree->left);
                tree->key = max->key;
                tree->left = delete_node(tree->left, max);
            }
            else
            {
                pid_node *min = avltree_minimum(tree->right);
                tree->key = min->key;
                tree->right = delete_node(tree->right, min);
            }
        }
        else
        {
            pid_node *tmp = tree;
            tree = tree->left ? tree->left : tree->right;
            kfree(tmp);
        }
    }

    if (tree != NULL)
        tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    return tree;
}

pid_node* avltree_delete(AVLTree tree, Type key)
{
    pid_node *z; 

    if ((z = avltree_search(tree, key)) != NULL)
        tree = delete_node(tree, z);
    return tree;
}

pid_node* avltree_search(AVLTree x, Type key)
{
    if (x==NULL || x->key==key)
        return x;
  
    if (key < x->key)
        return avltree_search(x->left, key);
    else
        return avltree_search(x->right, key);
}

pid_node* avltree_minimum(AVLTree tree)
{
    if (tree == NULL)
        return NULL;

    while(tree->left != NULL)
        tree = tree->left;
    return tree;
}

pid_node* avltree_maximum(AVLTree tree)
{
    if (tree == NULL)
        return NULL;
 
    while(tree->right != NULL)
        tree = tree->right;
    return tree;
}

void destroy_avltree(AVLTree tree)
{
    if (tree==NULL)
        return ;

    if (tree->left != NULL)
        destroy_avltree(tree->left);
    if (tree->right != NULL)
        destroy_avltree(tree->right);
 
    kfree(tree);
}

//初始化AVL树，返回其头节点
pid_node* Init_pid()
{
    pid_node *result = avltree_create_node(0, NULL, NULL);
    // int i;
    
    // for(i = 1; i < PID_NUM; i++)
    // {
    //     result = avltree_insert(result, i);
    // }
    
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
        pid = max->key+1;
        *head = avltree_insert(*head, pid);
        return pid;
    }
}

//将一个pid插入AVL树中，用于回收pid
int Del_pid(pid_node **head, int pid)
{
    if(head == NULL || *head == NULL)
        return -1;
    else
    {
        *head = avltree_delete(*head, pid);
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
        if(avltree_search(head, pid) == NULL)
            return 0;
        else
            return 1;
    }
}