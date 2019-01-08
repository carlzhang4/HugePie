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
        (pages + i)->slabp = 0;  // initially, the free space is the whole page
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
    kernel_printf(":::changed\n");
    uint bpsize = sizeof(struct page);
    uchar *bp_base;
    uint i;

    bp_base = bootmm_alloc_pages(bpsize * bmm.max_pfn, _MM_KERNEL, 1 << PAGE_SHIFT);
    if (bp_base == 0) {
        // the remaining memory must be large enough to allocate the whole group
        // of buddy page struct
        kernel_printf("\nERROR : bootmm_alloc_pages failed!\nInit buddy system failed!\n");
        while (1)
            ;
        //die
    }

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

    buddy.buddy_end_pfn = bmm.max_pfn & ~((1 << MAX_BUDDY_ORDER) - 1);  // remain 2 pages for I/O

    // init freelists of all bplevels
    for (i = 0; i < MAX_BUDDY_ORDER + 1; i++) {
        buddy.freelist[i].nr_free = 0;
        INIT_LIST_HEAD(&(buddy.freelist[i].free_head));
    }
    buddy.start_page = pages + buddy.buddy_start_pfn;
    init_lock(&(buddy.lock));

    for (i = buddy.buddy_start_pfn; i < buddy.buddy_end_pfn; i++) {
        __free_pages(pages + i, 0);
    }
}

void __free_pages(struct page *pbpage, uint bplevel) {
    /* page_index -> the current page
     * pair_index -> the buddy group that current page is in
     */
    uint page_index, pair_index;
    uint combined_index;
    struct page *pair_page;

    lockup(&buddy.lock);

    page_index = pbpage - buddy.start_page;
    // complier do the sizeof(struct) operation, and now page_index is the index

    while (bplevel < MAX_BUDDY_ORDER) {
        pair_index = page_index ^ (1 << bplevel);
        pair_page = pbpage + (pair_index - page_index);
        // kernel_printf("group%x %x\n", (page_index), pair_index);
        if (_is_same_bplevel(pair_page, bplevel) == 0) {
            // kernel_printf("%x %x\n", pair_page->bplevel, bplevel);

            break;
        }
        list_del_init(&pair_page->list);
        (buddy.freelist[bplevel].nr_free)--;
        set_bplevel(pair_page, -1);
        combined_index = pair_index & page_index;
        pbpage += (combined_index - page_index);
        page_index = combined_index;
        bplevel++;
    }
    set_bplevel(pbpage, bplevel);
    list_add(&(pbpage->list), &(buddy.freelist[bplevel].free_head));
    (buddy.freelist[bplevel].nr_free)++;
    // kernel_printf("v%x__addto__%x\n", &(pbpage->list),
    // &(buddy.freelist[bplevel].free_head));
    unlock(&buddy.lock);
}

struct page *__alloc_pages(uint bplevel) {
    uint current_level, size;
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

    if(found_flag == 0){
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
        (free->nr_free)--;

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







