#include <arch.h>
#include <driver/vga.h>
#include <zjunix/slab.h>
#include <zjunix/utils.h>

#define KMEM_ADDR(PAGE, BASE) ((((PAGE) - (BASE)) << PAGE_SHIFT) | 0x80000000)


typedef unsigned int uint;
typedef unsigned char uchar;

struct kmem_cache kmalloc_caches[PAGE_SHIFT];

static uint size_kmem_cache[PAGE_SHIFT] = {8, 16, 32, 64, 96, 128, 192, 256, 512, 1024, 1536, 2048};
//static uint size_kmem_cache[PAGE_SHIFT] = {96, 192, 8, 16, 32, 64, 128, 256, 512, 1024, 1536, 2048};

uint align_func_slab(uint value, uint align){
    value += align - 1;
    value &= ~(align - 1);
    return value;
}

void init_kmem_cpu(struct kmem_cache_cpu *kcpu) {
    kcpu->page = 0;
    kcpu->freeobj = 0;
}

void init_kmem_node(struct kmem_cache_node *knode) {
    INIT_LIST_HEAD(&(knode->full));
    INIT_LIST_HEAD(&(knode->partial));
}

void init_each_slab(struct kmem_cache *cache, uint size) {
    cache->objsize = size;
    cache->objsize = align_func_slab(cache->objsize,SIZE_INT);
    cache->size = cache->objsize + sizeof(void *);  // add one char as mark(available)
    cache->offset = cache->size;
    init_kmem_cpu(&(cache->cpu));
    init_kmem_node(&(cache->node));
}

void init_slab() {
    uint i;

    for (i = 0; i < PAGE_SHIFT; i++) {
        init_each_slab(&(kmalloc_caches[i]), size_kmem_cache[i]);
    }
// #ifdef SLAB_DEBUG
//     kernel_printf("Setup Slub ok :\n");
//     kernel_printf("\tcurrent slab cache size list:\n\t");
//     for (i = 0; i < PAGE_SHIFT; i++) {
//         kernel_printf("%x %x ", kmalloc_caches[i].objsize, (unsigned int)(&(kmalloc_caches[i])));
//     }
//     kernel_printf("\n");
// #endif  // ! SLAB_DEBUG
}

void format_slabpage(struct kmem_cache *cache, struct page *page) {
    uchar *moffset = (uchar *)KMEM_ADDR(page, pages);  // physical addr
    struct slab_head *s_head = (struct slab_head *)moffset;
    uint *ptr;
    uint remaining = (1 << PAGE_SHIFT);

    set_flag(page, _PAGE_SLAB);
    do {
        ptr = (uint *)(moffset + cache->offset);
        moffset += cache->size;
        *ptr = (uint)moffset;
        remaining -= cache->size;
    } while (remaining >= cache->size);

    *ptr = (uint)moffset & ~((1 << PAGE_SHIFT) - 1);
    s_head->end_ptr = ptr;
    s_head->nr_objs = 0;

    cache->cpu.page = page;
    cache->cpu.freeobj = (void **)(*ptr + cache->offset);
    page->virtual = (void *)cache;
    page->slabp = (uint)(*(cache->cpu.freeobj));
}

void *slab_alloc(struct kmem_cache *cache) {
    struct slab_head *s_head;
    void *object = 0;
    struct page *newpage;

    if (cache->cpu.freeobj)
        object = *(cache->cpu.freeobj);

    // 1st: check if the freeobj is in the boundary situation
while(1){
    if (is_bound((uint)object, (1 << PAGE_SHIFT))) {//object == 0
        // 2nd: the page is full
        if (cache->cpu.page) {
            list_add_tail(&(cache->cpu.page->list), &(cache->node.full));
        }

        if (list_empty(&(cache->node.partial))) {
            // call the buddy system to allocate one more page to be slab-cache
            newpage = __alloc_pages(0);  // get bplevel = 0 page === one page
            if (!newpage) {
                // allocate failed, memory in system is used up
                kernel_printf("ERROR: slab request one page in cache failed\n");
                while (1)
                    ;
            }

#ifdef SLAB_DEBUG
            kernel_printf("\tnew page, index: %x \n", newpage - pages);
#endif  // ! SLAB_DEBUG
        // using standard format to shape the new-allocated page,
        // set the new page to be cpu.page
            format_slabpage(cache, newpage);
            object = *(cache->cpu.freeobj);
            // as it's newly allocated no check may be need
            continue;
        }
        // get the header of the cpu.page(struct page)
        cache->cpu.page = container_of(cache->node.partial.next, struct page, list);
        list_del(cache->node.partial.next);
        object = (void *)(cache->cpu.page->slabp);
        cache->cpu.freeobj = (void **)((uchar *)object + cache->offset);
        continue;
    }

    cache->cpu.freeobj = (void **)((uchar *)object + cache->offset);
    cache->cpu.page->slabp = (uint)(*(cache->cpu.freeobj));
    s_head = (struct slab_head *)KMEM_ADDR(cache->cpu.page, pages);
    ++(s_head->nr_objs);

    // slab may be full after this allocation
    if (is_bound(cache->cpu.page->slabp, 1 << PAGE_SHIFT)) {
        list_add_tail(&(cache->cpu.page->list), &(cache->node.full));
        init_kmem_cpu(&(cache->cpu));
    }
    return object;
}
}

void slab_free(struct kmem_cache *cache, void *object) {
    struct page *opage = pages + ((uint)object >> PAGE_SHIFT);
    uint *ptr;
    struct slab_head *s_head = (struct slab_head *)KMEM_ADDR(opage, pages);

    if (!(s_head->nr_objs)) {
        kernel_printf("ERROR : slab_free error!\n");
        // die();
        while (1)
            ;
    }

    ptr = (uint *)((uchar *)object + cache->offset);
    *ptr = *((uint *)(s_head->end_ptr));
    *((uint *)(s_head->end_ptr)) = (uint)object;
    --(s_head->nr_objs);

    if (list_empty(&(opage->list)))
        return;

    if (!(s_head->nr_objs)) {
        __free_pages(opage, 0);
        return;
    }

    list_del_init(&(opage->list));
    list_add_tail(&(opage->list), &(cache->node.partial));
}

uint get_slab(uint size) {
    uint itop = PAGE_SHIFT;
    uint i;
    uint bf_num = (1 << (PAGE_SHIFT - 1));  // half page
    uint bf_index = PAGE_SHIFT;             // record the best fit num & index

    /**
    for (i = 0; i < itop; i++) {
        if ((kmalloc_caches[i].objsize >= size) && (kmalloc_caches[i].objsize < bf_num)) {
            bf_num = kmalloc_caches[i].objsize;
            bf_index = i;
        }
    }
    **/
    for(i = 0; i < itop; i++){
        if((kmalloc_caches[i].objsize >= size)){
            bf_index = i;
            break;
        }
    }
    return bf_index;
}

void *kmalloc(uint size) {
    struct kmem_cache *cache;
    uint bf_index;

    if (!size)
        return 0;

    // if the size larger than the max size of slab system, then call buddy to
    // solve this
    if (size > kmalloc_caches[PAGE_SHIFT - 1].objsize) {
        size = align_func_slab(size, 1 << PAGE_SHIFT);
        return (void *)(KERNEL_ENTRY | (uint)alloc_pages(size >> PAGE_SHIFT));
    }

    bf_index = get_slab(size);
    if (bf_index >= PAGE_SHIFT) {
        kernel_printf("ERROR: No available slab\n");
        while (1)
            ;
    }
    return (void *)(KERNEL_ENTRY | (uint)slab_alloc(&(kmalloc_caches[bf_index])));
}

void kfree(void *obj) {
    struct page *page;

    obj = (void *)((uint)obj & (~KERNEL_ENTRY));
    page = pages + ((uint)obj >> PAGE_SHIFT);
    if (!(page->flag == _PAGE_SLAB))
        return free_pages((void *)((uint)obj & ~((1 << PAGE_SHIFT) - 1)), page->bplevel);
    else
        return slab_free(page->virtual, obj);
}

