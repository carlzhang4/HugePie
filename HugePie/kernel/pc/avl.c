#include <zjunix/avl.h>
#include <zjunix/slab.h>
#include <driver/vga.h>
#include <zjunix/syscall.h>
#include <arch.h>
#include <intr.h>
#include <zjunix/utils.h>

Node* avltree_create_node(Type key, Node *left, Node* right)
{
    Node* p;

    if ((p = (Node *)kmalloc(sizeof(Node))) == NULL)
        return NULL;
    p->key = key;
    p->height = 0;
    p->left = left;
    p->right = right;

    return p;
}

#define HEIGHT(p)    ( (p==NULL) ? 0 : (((Node *)(p))->height) )

int compare(Type x, Type y){
    return (x.value > y.value)?1:(x.value == y.value)?0:-1;
}

int avltree_height(AVLTree tree)
{
    return HEIGHT(tree);
}

#define MAX(a, b)    ( (a) > (b) ? (a) : (b) )

//以下是AVL树的各种旋转
static Node* left_left_rotation(AVLTree k2)
{
    AVLTree k1;

    k1 = k2->left;
    k2->left = k1->right;
    k1->right = k2;

    k2->height = MAX( HEIGHT(k2->left), HEIGHT(k2->right)) + 1;
    k1->height = MAX( HEIGHT(k1->left), k2->height) + 1;

    return k1;
}

static Node* right_right_rotation(AVLTree k1)
{
    AVLTree k2;

    k2 = k1->right;
    k1->right = k2->left;
    k2->left = k1;

    k1->height = MAX( HEIGHT(k1->left), HEIGHT(k1->right)) + 1;
    k2->height = MAX( HEIGHT(k2->right), k1->height) + 1;

    return k2;
}

static Node* left_right_rotation(AVLTree k3)
{
    k3->left = right_right_rotation(k3->left);

    return left_left_rotation(k3);
}

static Node* right_left_rotation(AVLTree k1)
{
    k1->right = left_left_rotation(k1->right);

    return right_right_rotation(k1);
}

Node* avltree_insert(AVLTree tree, Type key)
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
    else if (compare(key, tree->key) == -1) // 应该将key插入到"tree的左子树"的情况
    {
        tree->left = avltree_insert(tree->left, key);
        // 插入节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->left) - HEIGHT(tree->right) == 2)
        {
            if (compare(key, tree->key) == -1)
                tree = left_left_rotation(tree);
            else
                tree = left_right_rotation(tree);
        }
    }
    else if (compare(key, tree->key) == 1) // 应该将key插入到"tree的右子树"的情况
    {
        tree->right = avltree_insert(tree->right, key);
        // 插入节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->right) - HEIGHT(tree->left) == 2)
        {
            if (compare(key, tree->key) == 1)
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

static Node* delete_node(AVLTree tree, Node *z)
{
    // 根为空 或者 没有要删除的节点，直接返回NULL。
    if (tree==NULL || z==NULL)
        return NULL;

    if (compare(z->key, tree->key) == -1)        // 待删除的节点在"tree的左子树"中
    {
        tree->left = delete_node(tree->left, z);
        // 删除节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->right) - HEIGHT(tree->left) == 2)
        {
            Node *r =  tree->right;
            if (HEIGHT(r->left) > HEIGHT(r->right))
                tree = right_left_rotation(tree);
            else
                tree = right_right_rotation(tree);
        }
    }
    else if (compare(z->key, tree->key) == 1)// 待删除的节点在"tree的右子树"中
    {
        tree->right = delete_node(tree->right, z);
        // 删除节点后，若AVL树失去平衡，则进行相应的调节。
        if (HEIGHT(tree->left) - HEIGHT(tree->right) == 2)
        {
            Node *l =  tree->left;
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
                Node *max = avltree_maximum(tree->left);
                tree->key = max->key;
                tree->left = delete_node(tree->left, max);
            }
            else
            {
                Node *min = avltree_minimum(tree->right);
                tree->key = min->key;
                tree->right = delete_node(tree->right, min);
            }
        }
        else
        {
            Node *tmp = tree;
            tree = tree->left ? tree->left : tree->right;
            // if(tmp->key.p != NULL)
            //     kfree(tmp->key.p);
            // kfree(tmp);
        }
    }

    if (tree != NULL)
        tree->height = MAX(HEIGHT(tree->left), HEIGHT(tree->right)) + 1;
    return tree;
}

Node* avltree_delete(AVLTree tree, Type key)
{
    Node *z; 

    if ((z = avltree_search(tree, key)) != NULL)
        tree = delete_node(tree, z);
    return tree;
}

Node* avltree_search(AVLTree x, Type key)
{
    if(x == NULL)
        return NULL;
    if (compare(x->key, key)==0)
        return x;
  
    if (compare(key, x->key) == -1)
        return avltree_search(x->left, key);
    else
        return avltree_search(x->right, key);
}

Node* avltree_minimum(AVLTree tree)
{
    if (tree == NULL)
        return NULL;

    while(tree->left != NULL)
        tree = tree->left;
    return tree;
}

Node* avltree_maximum(AVLTree tree)
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
 
    // kfree(tree);
}
