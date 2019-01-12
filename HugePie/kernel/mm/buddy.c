#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/list.h>
#include <zjunix/lock.h>
#include <zjunix/utils.h>

typedef unsigned int uint;
typedef unsigned char uchar;

uint kernel_start_pfn, kernel_end_pfn;
struct buddy_sys buddy;
struct page *pages;

//segement tree part 
#define MAX_TREE_PAGES 100
#define PAGE_SHIFT 12
#define OFFSET_TREE 8

struct tree_system{
    uint base_address;
    uint node_num;
    uint offset_tree;
    uint max_tree_pages;
}segment_tree_system;

struct node{
    uint left_address;  //start address
    uint right_address; //end address
    uint max_free_interval; //max consecutive space in pages
    uint left_available;    //max consecutive space from left
    uint right_available;   //max consecutive space from right
    uint lazy;
}tree[MAX_TREE_PAGES];




void build_tree(uint t, uint left, uint right){
    uint middle;
    //init node t
    
    if(t >= MAX_TREE_PAGES){
       kernel_printf("page overflow!\n");
        return;
    }
    else{
        tree[t].left_address = left;
        tree[t].right_address = right;
        tree[t].max_free_interval = right - left + 1;
        tree[t].left_available = right - left + 1;
        tree[t].right_available = right - left + 1;
        
        if(left == right){
            return;
        }
        else{
            //do recursive build tree
            middle = (left + right)/2;
            build_tree(2*t, left, middle);
            build_tree(2*t+1, middle+1, right);
            return;
        }
    }
}

int alloc_pages_tree(uint t, uint length){
    //struct page *page = __alloc_pages_tree(length);
    uint address = __alloc_pages_tree(t,length);
    if (address == -1){
        return 0;
    }
    else{
       kernel_printf("allocate:%d\n",address);
        return (address);
    }
}

int __alloc_pages_tree(uint t, uint length){
    int address;
    if(tree[t].max_free_interval == (tree[t].right_address - tree[t].left_address + 1)){
        address =  tree[t].left_address;
        tree[t].max_free_interval -= length;
        tree[t].left_available = 0;
        tree[t].right_available -= length;
        tree[t].lazy = 1;
        update_leaves(address,length,1);
        return address;
    }
    else{
        if(tree[2*t].max_free_interval >= length){
            return __alloc_pages_tree(2*t, length);
        }
        else if(tree[2*t+1].max_free_interval >= length){
            return __alloc_pages_tree(2*t+1, length);
        }
        else{
            address = tree[2*t].right_address - tree[2*t].right_available + 1;
            update_leaves(address,length,1);
            return address;
            //to do
        }
    }
}

void update(uint t){
    if(t == 1)
        return;
    
    uint n0 = t/2;//father
    uint n1 = 2*n0;
    uint n2 = 2*n0+1;
    uint max_size = tree[n1].right_address - tree[n1].left_address + 1;

    if(tree[n1].max_free_interval == max_size && tree[n2].max_free_interval == max_size){
        tree[n0].left_available = 2 * max_size;
        tree[n0].right_available = 2 * max_size;
        tree[n0].max_free_interval = 2 * max_size;
    }
    else if(tree[n1].max_free_interval == max_size){
        tree[n0].right_available = tree[n2].right_available;
        tree[n0].left_available = tree[n2].left_available + max_size;
        tree[n0].max_free_interval = tree[n2].left_available + max_size;
    }
    else if(tree[n2].max_free_interval == max_size){
        tree[n0].left_available = tree[n1].left_available;
        tree[n0].right_available = tree[n1].right_available + max_size;
        tree[n0].max_free_interval = tree[n1].right_available + max_size;
    }
    else{
        
        if(tree[n1].max_free_interval >= tree[n2].max_free_interval){
            tree[n0].max_free_interval = tree[n1].max_free_interval;
        }
        else{
            tree[n0].max_free_interval = tree[n2].max_free_interval;
        }
        tree[n0].left_available = tree[n1].left_available;
        tree[n0].right_available = tree[n2].right_available;
        if(tree[n1].right_available + tree[n2].left_available > tree[n0].max_free_interval){
            tree[n0].max_free_interval = tree[n1].right_available + tree[n2].left_available;
        }
    }
    update(n0);
    
}

void update_leaf(uint t, uint value){
    if(value == 0){
        tree[t].left_available = 1;
        tree[t].right_available = 1;
        tree[t].max_free_interval = 1;
        update(t);
    }
    else if(value == 1){
        tree[t].left_available = 0;
        tree[t].right_available = 0;
        tree[t].max_free_interval = 0;
        update(t);
    }
}

void update_leaves(uint start, uint size, uint value){
    uint i;
    start = start + OFFSET_TREE;
    for(i = start; i < start + size; i++){
        update_leaf(i,value);
    }
}

void free_pages_tree(uint t, uint addr, uint length){
    uint L;
    uint R;
    uint middle;
    if(length == 0){
        return;
    }
    else{
        L = addr;
        R = addr + length - 1;
        middle = (tree[t].left_address + tree[t].right_address)/2;
    }
    if(L == tree[t].left_address && R == tree[t].right_address){
        tree[t].left_available = 0;
        tree[t].right_available = 0;
        tree[t].max_free_interval = 0;
    }
    else{
        if(middle >= R){
            free_pages_tree(2*t,addr, length);
            return;
        }
        else if(middle < L){
            free_pages_tree(2*t+1,addr, length);
        }
        else{
            //free_pages_tree(2*t,L, middle);
            //free_pages_tree(2*t+1,middle+1, R);
            //return;
        }
    }
    update_leaves(addr,length,0);
}


void print_tree(void){
    uint t = 1;
    uint i;
   kernel_printf("Tree node :%d; ",t);
   kernel_printf("address:%d-%d; ",tree[t].left_address,tree[t].right_address);
   kernel_printf("available:%d-%d; ",tree[t].left_available,tree[t].right_available);
   kernel_printf("max_free_interval:%d\n",tree[t].max_free_interval);
   kernel_printf("The leaves state: ");
    for(i=8;i<16;i++){
       kernel_printf(" %d ",!tree[i].max_free_interval);
    }
   kernel_printf("\n\n");
}

void pushdown(uint t){
    if(t == 0){
        return;
    }
    else if(t > MAX_TREE_PAGES){
        return;
    }
    else{
        if(tree[t].lazy != 0){
            tree[2*t].lazy = tree[t].lazy;
            tree[2*t+1].lazy = tree[t].lazy;
            //todo
            tree[t].lazy = 0;
            return;
        }
        else{
            return;
        }
    }
    
}

void pushup(uint t){
    if(t == 0){
        return;
    }
    else if(t > MAX_TREE_PAGES){
        return;
    }

    if(tree[2*t].max_free_interval > tree[2*t+1].max_free_interval){
        tree[t].max_free_interval = tree[2*t].max_free_interval;
    }
    else{
        tree[t].max_free_interval = tree[2*t+1].max_free_interval;
    }
    tree[t].left_available = tree[2*t].left_available;
    tree[t].right_available = tree[2*t+1].right_available;
    
    if(tree[2*t].right_available + tree[2*t+1].left_available > tree[t].max_free_interval){
        tree[t].max_free_interval = tree[2*t].right_available + tree[2*t+1].left_available;
        return;
    }
    else{
        return;
    }

}

void init_tree_system(void){
    segment_tree_system.base_address = 0;
    segment_tree_system.max_tree_pages = 2*(1<<3);
    segment_tree_system.node_num = 1<<3;
    segment_tree_system.offset_tree = 1<<3;
    
    build_tree(1, 0, segment_tree_system.node_num);
    return;
}

//segement tree part 

uint align_func_buddy(uint value, uint align){
    value += align - 1;
    value &= ~(align - 1);
    return value;
}
/*
struct buddy_sys {
    unsigned int buddy_start_pfn;
    unsigned int buddy_end_pfn;
    struct page *start_page;
    struct lock_t lock;
    struct freelist freelist[MAX_BUDDY_ORDER + 1];
};
*/
void buddy_info() {
    uint index;
    kernel_printf("Buddy-system :\n");
    kernel_printf("\tstart page-frame number : %x\n", buddy.buddy_start_pfn);
    kernel_printf("\tend page-frame number : %x\n", buddy.buddy_end_pfn);
    for (index = 0; index <= MAX_BUDDY_ORDER; ++index) {
        kernel_printf("\t(%x)# : %x frees\n", index, buddy.freelist[index].nr_free);
    }
}

void init_pages(uint start_pfn, uint end_pfn) {
    uint i;
    for (i = start_pfn; i < end_pfn; i++) {
        clean_flag(pages + i, -1);
        set_flag(pages + i, _PAGE_RESERVED);
        (pages + i)->reference = 1;
        (pages + i)->virtual = (void *)(-1);
        (pages + i)->bplevel = (-1);
        (pages + i)->slabp = 0;  //the page the page is totally free 
        INIT_LIST_HEAD(&(pages[i].list)); 
    }
}

/*
struct page {
    unsigned int flag;       // the declaration of the usage of this page
    unsigned int reference;  //
    struct list_head list;   // double-way list
    void *virtual;           // default 0x(-1)
    unsigned int bplevel;    // the order level of the page unsigned int sl_objs; represents the number of objects in current if the page is of _PAGE_SLAB, then bplevel is the sl_objs

    unsigned int slabp;       //if the page is used by slab system,then slabp represents the base-addr of free space

};
*/
void init_buddy() {
    uint bpsize = sizeof(struct page);
    uchar *bp_base;
    uint i;

    bp_base = bootmm_alloc_pages(bpsize * bmm.max_pfn, _MM_KERNEL, 1 << PAGE_SHIFT);
    if (bp_base == 0) {
        // the remaining memory must be large enough to allocate to buddy system
        kernel_printf("\nERROR : bootmm_alloc_pages failed!\nInit buddy system failed!\n");
        while (1)
            ;
        //die
    }
    else{
        pages = (struct page *)((uint)bp_base | 0x80000000); //KERNEL_ENTRY 所有entry后+bootmm的16M后的128M空间都在pages中

        init_pages(0, bmm.max_pfn);

        kernel_start_pfn = 0;
        kernel_end_pfn = 0;
        for (i = 0; i < bmm.cnt_infos; ++i) {   //让等于最后一个info的end
            if (bmm.info[i].end > kernel_end_pfn)
                kernel_end_pfn = bmm.info[i].end;
        }                                       
        kernel_end_pfn >>= PAGE_SHIFT;

        buddy.buddy_start_pfn = align_func_buddy(kernel_end_pfn, 1 << MAX_BUDDY_ORDER);
                // the pages that bootmm using cannot be merged into buddy_sys

        buddy.buddy_end_pfn = bmm.max_pfn & ~((1 << MAX_BUDDY_ORDER) - 1);  // remain 2 pages for I/O ??

        // init freelists of all bplevels
        for (i = 0; i <= MAX_BUDDY_ORDER; i++) {
            buddy.freelist[i].nr_free = 0;
            INIT_LIST_HEAD(&(buddy.freelist[i].free_head));
        }
        buddy.start_page = pages + buddy.buddy_start_pfn;
        init_lock(&(buddy.lock));

        for (i = buddy.buddy_start_pfn; i < buddy.buddy_end_pfn; i++) {
            __free_pages(pages + i, 0);
        }
    }


    
}

void __free_pages(struct page *pbpage, uint bplevel) {
    /* page_index -> the current page
     * pair_index -> the buddy group that current page is in
     */
    uint page_index;
    uint pair_index;
    uint combined_index;
    struct page *pair_page;

    lockup(&buddy.lock);

    page_index = pbpage - buddy.start_page;
    // complier do the sizeof(struct) operation, and now page_index is the index ??

    while (bplevel < MAX_BUDDY_ORDER) {
        pair_index = page_index ^ (1 << bplevel); //find pair page index
        pair_page = pbpage + (pair_index - page_index);
  
        if (_is_same_bplevel(pair_page, bplevel) == 0) {
            break;
        }
        else{
            list_del_init(&pair_page->list);
            (buddy.freelist[bplevel].nr_free)--;
            set_bplevel(pair_page, -1);
            combined_index = pair_index & page_index;
            pbpage += (combined_index - page_index);
            page_index = combined_index;
            bplevel++;
        } 
    }
    // reach the level that can not merge
    set_bplevel(pbpage, bplevel);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    (buddy.freelist[bplevel].nr_free)++;

    unlock(&buddy.lock);
}

struct page *__alloc_pages(uint bplevel) {
    uint current_level;
    uint size;
    uint found_flag = 0;
    struct page *page, *buddy_page;
    struct freelist *free;

    lockup(&buddy.lock);

    if(bplevel > MAX_BUDDY_ORDER){
        kernel_printf("cannot allocate such big memory!\n");
        unlock(&buddy.lock);
        return 0;
    }

    for (current_level = bplevel; current_level <= MAX_BUDDY_ORDER; current_level++) {
        free = buddy.freelist + current_level;
        if (!list_empty(&(free->free_head))){
            found_flag = 1;
            break;
        }
    }

    if(found_flag == 0){ //does not find avalible page
        unlock(&buddy.lock);
        return 0;
    }
    else{
            //found
        page = container_of(free->free_head.next, struct page, list);
        list_del_init(&(page->list));
        set_bplevel(page, bplevel);
        set_flag(page, _PAGE_ALLOCED);
        // set_ref(page, 1);
        (free->nr_free)--;  //freelist[bplevel]--

        size = 1 << current_level;
        while (current_level > bplevel) {
            free--;
            current_level--;
            size >>= 1;
            buddy_page = page + size;
            list_add(&(buddy_page->list), &(free->free_head));
            (free->nr_free)++;
            set_bplevel(buddy_page, current_level);
        }

        unlock(&buddy.lock);
        return page;
    } 

}

void *alloc_pages(uint page_num) {
    uint bplevel = 0;
    if (!page_num)
        return 0;
    while (1<<bplevel < page_num) {
        bplevel ++;
    }

    struct page *page = __alloc_pages(bplevel);

    if (page == 0)
        return 0;
    else
        return (void *)((page - pages) << PAGE_SHIFT);
}

void free_pages(void *addr, uint bplevel) {
    __free_pages(pages + ((uint)addr >> PAGE_SHIFT), bplevel);
}







