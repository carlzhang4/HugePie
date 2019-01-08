#include "fat.h"
#include <driver/vga.h>
#include <zjunix/log.h>
#include "utils.h"
struct fs_info fat_info;
#define DIR_DATA_BUF_NUM 4
BUF_512 dir_buf [DIR_BUF_NUM];
u8 currentFileName[12];
u32 dir_clock_head = 0;
BUF_512 fat_buf[FAT_BUF_NUM];
extern u8 MBR_buf[512];

u32 init_fat_info()
{

    /* MBR partition 1 entry starts from +446, and LBA starts from +8 */
    fat_info.base_addr = get_u32(MBR_buf + 446 + 8);


    //fat_info.base_addr = fat_base_address;
    //get BPB record 
    if(read_block(fat_info.BPB.data,fat_info.base_addr,1) == 1){
        goto init_fat_info_error;
    }
    log(LOG_OK, "Get FAT BPB");

    //update information of fat_info
    fat_info.sectors_per_fat = fat_info.BPB.attr.num_of_sectors_per_fat;
    fat_info.total_data_sectors = fat_info.BPB.attr.num_of_sectors - fat_info.BPB.attr.reserved_sectors -  fat_info.sectors_per_fat * 2;
    fat_info.total_sectors = fat_info.BPB.attr.num_of_sectors;
    fat_info.total_data_clusters = fat_info.total_data_sectors / fat_info.BPB.attr.sectors_per_cluster;
    
    // root dir base sector
    fat_info.first_data_sector = fat_info.sectors_per_fat * 2 + fat_info.BPB.attr.reserved_sectors;

    /* Keep FSInfo in buf */
    read_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1);
    log(LOG_OK, "Get FSInfo sector");

    log(LOG_OK, "FAT infomation get");
    return 0;
    init_fat_info_error:
        return 1;
}

// init fat file system
void init_fat_buf() {
    int i = 0;
    for (i = 0; i < FAT_BUF_NUM; i++) {
        fat_buf[i].cur = 0xffffffff;
        fat_buf[i].state = 0;
    }
}

void init_dir_buf() {
    int i = 0;
    for (i = 0; i < DIR_DATA_BUF_NUM; i++) {
        dir_buf[i].cur = 0xffffffff;
        dir_buf[i].state = 0;
    }
}

u32 init_fat() {
    if (init_fat_info() != 0){
        log(LOG_FAIL, "File system init failed.");
        return 1;
    }
    init_fat_buf();
    init_dir_buf();
    return 0;
}

// find the offset of next slash and get the filename/subdir between two slashes
u32 to_next_slash(u8 *f)
{
    u32 i,j,k;
    for(i = 0;i<11;i++) 
        currentFileName[i] = 0x20;
    currentFileName[11] = 0;
    j = 0;
    k = 0;    
    for(i = 0;(*(f+i)!=0) && (*(f+i) != '/');i++)
    ;
    for(j = 0; (*(f+j) != '.' ) && j < 8 && (*(f+j)!= 0) && j < i;j++,k++){
        if((*(f+j) <= 'z') && (*(f+j) >= 'a'))
            currentFileName[k] = (u8)(*(f+j) -'a'+'A');
        else 
            currentFileName[k] = (u8)(*(f+j));
    }
    if(*(f+j) == '.'){
        j++;k = 8;
        for( ;k<11 && j < 12 && j < i;j++,k++){
            if((*(f+j) <= 'z') && (*(f+j) >= 'a'))
                currentFileName[k] = (u8)(*(f+j) -'a'+'A');
            else 
                currentFileName[k] = (u8)(*(f+j));
        }
    }

    return i;

}
// if s1 == s2, return 0, else return 1;
u32 filename_cmp(u8 *s1, u8 *s2)
{
    u32 i;
    for(i = 0; i<11;i++){
        if(s1[i] != s2[i])
            return 1;
    }
    return 0;
}

u32 fs_find(FILE* file)
{
    u8 *f = file->path;
    u32 index;
    u32 i,j,k;
    u32 currentCluster = 2;
    u32 next_slash;
    u32 done_this_dir = 0;
    u32 next_clust;
    if(*(f++) != '/')  
        return 1;


    u32 sec_of_clust;
    //search every directory in the path
    while(1)
    { 
        done_this_dir = 0;
        file->dir_entry_pos = 0xffffffff;
        next_slash = to_next_slash(f);
        index = fs_read_512(dir_buf,fs_dataclus2sec(currentCluster),&dir_clock_head,DIR_DATA_BUF_NUM);
        if(index == 0xFFFFFFFF) {
            kernel_printf("fat.c:114::Failed to read Cluster %d",currentCluster);
            return 1;
        }
        //search every sector of current dir    
        for(sec_of_clust = 1; sec_of_clust <= fat_info.BPB.attr.sectors_per_cluster;sec_of_clust++){       
         
            //search every entry
           // kernel_printf("Current Dir Sec: %d\n",fs_dataclus2sec(currentCluster));
        /* kernel_printf("CurrentFileName: %s",currentFileName);
         kernel_printf("/\n");*/

            for (i = 0;i<512;i+=32){
               /* kernel_printf("  %d",i/32);;
                for(j = 0;j < 11;j++)
                    kernel_printf("%c",*(dir_buf[index].buf + i + j));
                kernel_printf("/\n");*/

                if(*(dir_buf[index].buf+i) == 0) //reach the last entry and didn't find the file
                {
                    done_this_dir = 1;
                    //log(LOG_OK,"Finished searching current cluster\n");
                    break;
                }
                if(filename_cmp(currentFileName,dir_buf[index].buf + i)==0)//found a dir/file has the same filename
                {
                    if((*(dir_buf[index].buf+i + 11) == 0x08) || (*(dir_buf[index].buf+i + 11) == 0x0f))
                        continue;       //长文件名/卷标
                    else{
                        file->dir_entry_pos = i;
                        file->dir_entry_sector = dir_buf[index].cur - fat_info.base_addr;
                        for(j = 0;j < 32; j++){
                            file->entry.data[j] = *(dir_buf[index].buf + i + j);
                        }
                        done_this_dir = 1;   
                        break;
                    }
                }
            }
            if(done_this_dir == 1)    //jump out search for this cluster 
                break;                   
            //go to next sec in this cluster
            if(sec_of_clust < fat_info.BPB.attr.sectors_per_cluster){
                index = fs_read_512(dir_buf,fs_dataclus2sec(currentCluster) + sec_of_clust,&(dir_clock_head),DIR_DATA_BUF_NUM);
                if(index == 0xffffffff){
                    kernel_printf("fat.c:148::Failed to read Cluster %d",currentCluster);
                    return 1;
                }
               // kernel_printf("Sec : %d, Cluster: %d",sec_of_clust,currentCluster);
            }
            // go to the 1st sec of next cluster of current dir    
            else{
                if(get_fat_entry_value(currentCluster,&next_clust)==1)
                    return 1;
                else if(next_clust <= fat_info.total_data_clusters){
                    currentCluster = next_clust;
                    index = fs_read_512(dir_buf,fs_dataclus2sec(currentCluster),&dir_clock_head,DIR_DATA_BUF_NUM);
                    if(index == 0xFFFFFFFF) {
                        kernel_printf("fat.c:159::Failed to read Cluster %d",currentCluster);
                        return 1;
                    }
                    sec_of_clust = 1;
                }
                else{       //all clusters are searched
                    done_this_dir = 1;
                    break;
                }    
            }
        }

        // jump out of the search of current directory
        //if didn't find
        if(file->dir_entry_pos == 0xffffffff)
            return 0;
        //if come to the end of the path
        if(f[next_slash] == 0){
            return 0;
        }
        // if didn't come to the end of path and next jump isn't a subdir
        if(*(file->entry.data+11) != 0x10)    
            return 1;
        //now we need to jump to next subdir
        else
        {
            next_clust = get_start_cluster(file);
            f += next_slash + 1;
            if (next_clust <= fat_info.total_data_clusters) {
                index = fs_read_512(dir_buf, fs_dataclus2sec(next_clust), &dir_clock_head, DIR_DATA_BUF_NUM);
                if (index == 0xffffffff){
                return 1;
                }
                currentCluster = next_clust;
            }
            else   
                return 1;
        } 
    }
}

u32 fs_open(FILE *file, u8 * filename)
{
    u32 i;
    for (i = 0;i<LOCAL_DATA_BUF_NUM;i++)
    {
        file->data_buf[i].cur = 0xffffffff;
        file->data_buf[i].state = 0;
    }
    file->clock_head = 0;

    for(i = 0;i<256;i++)
        file->path[i] = 0;
    for(i = 0;i<256;i++)
        if(filename[i] == 0)
            break;
        else{
            file->path[i] = filename[i];
        }
    
    file->loc = 0;

    //if err when fs_find
    if(fs_find(file) == 1){
        log(LOG_FAIL,"fs_open:can't find the file %s",filename);
        return 1;
    }
    //if doesn't exist
    if(file->dir_entry_pos == 0xffffffff)
        return 1;
    return 0;
}


u32 fs_read(FILE * file,u8 * buf, u32 count)
{
    u32 startCluster, endCluster;
    u32 startByte,endByte;
    u32 bytesPerCluster = fat_info.BPB.attr.sectors_per_cluster<<9;
    u32 index;
    u32 currentClus = get_start_cluster(file);
    u32 nextClust;
    u32 i,sp;
    u32 startByteofClust;

    //if count exceeds the file, then read to EOF
    if(file->loc + count > file->entry.attr.size)
        count = file->entry.attr.size - file->loc;
    //if count = 0, return directly
    if(count == 0)
        return 0;
    //if this file is empty,return directly
    if(currentClus == 0)
        return 0;

    startCluster = file->loc >> fs_wa(bytesPerCluster);
    startByte = file->loc % bytesPerCluster;
    endCluster = (file->loc + count -1) >> fs_wa(bytesPerCluster);
    endByte =  (file->loc + count - 1) % bytesPerCluster;
/*
    kernel_printf("startCluster = %d,startByte = %d\n",startCluster,startByte);
    kernel_printf("endCluster = %d,endByte = %d\n\n",endCluster,endByte);
       */ 

    //set currentCluster to startCluster
    for(i = 0; i < startCluster; i++){
        if(get_fat_entry_value(currentClus,&nextClust) == 1)
            return 0xffffffff;

        currentClus = nextClust;
    }
    sp = 0;
    //if there is only one cluster to read
    if(startCluster == endCluster){
        index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&file->clock_head,LOCAL_DATA_BUF_NUM);
        if(index == 0xffffffff)
            return 0xffffffff;
        for(i = startByte;i<endByte;i++){
            buf[sp++] = file->data_buf[index].buf[i];
        }
        file->loc += count;
        return sp;
    }
    else{
        //read the first cluster
        index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&file->clock_head,LOCAL_DATA_BUF_NUM);
        if(index == 0xffffffff)
            return 0xffffffff;
        for(i = startByte;i<bytesPerCluster;i++){
            buf[sp++] = file->data_buf[index].buf[i];
        }  
        if(get_fat_entry_value(currentClus,&nextClust)==1)
            return 0xffffffff;
        currentClus = nextClust;

        //read the whole cluster of inner clusters
        while(startCluster < endCluster){
            index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&file->clock_head,LOCAL_DATA_BUF_NUM);
            if(index == 0xffffffff)
                return 0;
            for(i = 0;i<bytesPerCluster;i++)
                buf[sp++] = file->data_buf[index].buf[i];
            if(get_fat_entry_value(currentClus,&nextClust)==1)
                return 0xffffffff;
            currentClus = nextClust;
            startCluster++;
        }

        //read till endByte of endCluster
        index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&file->clock_head,LOCAL_DATA_BUF_NUM);
        if(index == 0xffffffff)
            return 0xffffffff;
        for(i = startByte;i<endByte;i++){
            buf[sp++] = file->data_buf[index].buf[i];
        }
        file->loc += count;
        return sp;
    }        
}

/* fflush, write global buffers to sd */
u32 fs_fflush() {
    u32 i;

    // FSInfo shoud add base_addr
    if (write_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1) == 1)
        return 1;

    if (write_block(fat_info.fat_fs_info, 7 + fat_info.base_addr, 1) == 1)
        return 1;

    for (i = 0; i < FAT_BUF_NUM; i++)
        if (write_fat_sector(i) == 1)
            return 1;

    for (i = 0; i < DIR_DATA_BUF_NUM; i++)
        if (fs_write_512(dir_buf + i) == 1)
            return 1;

    return 0;
}

/* Close: write all buf in memory to SD */
u32 fs_close(FILE *file) {
    u32 i;
    u32 index;

    /* Write directory entry */
    index = fs_read_512(dir_buf, file->dir_entry_sector, &dir_clock_head, DIR_DATA_BUF_NUM);
    if (index == 0xffffffff)
        return 1;

    dir_buf[index].state = 3;

    // Issue: need file->dir_entry to be local partition offset
    for (i = 0; i < 32; i++)
        *(dir_buf[index].buf + file->dir_entry_pos + i) = file->entry.data[i];
    /* do fflush to write global buffers */
    if (fs_fflush() == 1)
        return 1;
    /* write local data buffer */
    for (i = 0; i < LOCAL_DATA_BUF_NUM; i++)
        if (fs_write_4k(file->data_buf + i) == 1)
            return 1;

    return 0;
}


/* Find a free data cluster */
u32 fs_next_free(u32 start, u32 *next_free) {
    u32 clus;
    u32 ClusEntryVal;

    *next_free = 0xFFFFFFFF;

    for (clus = start; clus <= fat_info.total_data_clusters + 1; clus++) {
        if (get_fat_entry_value(clus, &ClusEntryVal) == 1)
            return 1;
        //found a empty cluster
        if (ClusEntryVal == 0) {
            *next_free = clus;
            break;
        }
    }

    return 0;
}

u8 new_alloc_empty[PAGE_SIZE];
/* Alloc a new free data cluster */
u32 fs_alloc(u32 *new_alloc) {
    u32 i;
    u32 index;
    u32 nextFreeClust,clust;
    clust = get_u32(fat_info.fat_fs_info + 492) + 1;
    
    //FSI_next_free > FSI_free_count
    if (clust > get_u32(fat_info.fat_fs_info + 488) + 1) {
        if (fs_next_free(2, &clust) == 1)
            return 1;

        if (fs_modify_fat(clust, 0xFFFFFFFF) == 1)
            return 1;
    }
    
    // FAT allocated and update FSI_Nxt_Free
    if (fs_modify_fat(clust, 0xFFFFFFFF) == 1)
        return 1;

    if (fs_next_free(clust, &nextFreeClust) == 1)
        return 1;

    /* no available free cluster */
    if (nextFreeClust > fat_info.total_data_clusters + 1)
        return 1;

    set_u32(fat_info.fat_fs_info + 492, nextFreeClust - 1);

    *new_alloc = clust;

    /* Erase new allocated cluster */
    if (write_block(new_alloc_empty, fs_dataclus2sec(clust), fat_info.BPB.attr.sectors_per_cluster) == 1)
        return 1;

    return 0;
    
}


/* Write to file */
u32 fs_write(FILE *file, const u8 *buf, u32 count) {
    u32 startCluster, endCluster;
    u32 startByte,endByte;
    u32 bytesPerCluster = fat_info.BPB.attr.sectors_per_cluster<<9;
    u32 index;
    u32 currentClus = get_start_cluster(file);
    u32 nextClust;
    u32 i,j,sp;
    u32 startByteofClust;

    if(count == 0) 
        return 0;
    startCluster = file->loc >> fs_wa(bytesPerCluster);
    startByte = file->loc % bytesPerCluster;
    endCluster = (file->loc + count -1) >> fs_wa(bytesPerCluster);
    endByte =  (file->loc + count) % bytesPerCluster;
    u32 newEmptyClust;
     
    //if the file is empty and haven't alloc a cluster, alloc an empty cluster
    if((get_entry_filesize(file->entry.data) == 0)&& get_start_cluster(file) == 0)
    {
        if( fs_alloc(&newEmptyClust) == 1)
            return 0xffffffff;
        file->entry.attr.starthi = (u16)(((newEmptyClust >> 16) & 0xFFFF));
        file->entry.attr.startlow = (u16)((newEmptyClust & 0xFFFF));
        if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(newEmptyClust)) == 1)
            return 0xffffffff;
    }
    //come the startCluster
    currentClus = get_start_cluster(file);
    for(i=0;i<startCluster;i++){
        if(get_fat_entry_value(currentClus,&nextClust) == 1)
            return 0xffffffff;
        if(nextClust == 0)
            return 0xffffffff;
        //need to alloc a new cluster after this cluster    
        if(nextClust > fat_info.total_data_clusters)
        {
            if(fs_alloc(&newEmptyClust)==1)
                return 0xffffffff;
            if(fs_modify_fat(currentClus,newEmptyClust)==1)
                return 0xffffffff;
            if(fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(newEmptyClust)) == 1)
                return 0xffffffff;

        }    
        currentClus = nextClust;

    }
    u32 endbyte;
    //write the first cluster
    if(index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&(file->clock_head),LOCAL_DATA_BUF_NUM)==1)
        return 0xffffffff;
    // state used for clock is 2'b11
    file->data_buf[index].state = 3;    
    endbyte = (startCluster == endCluster)?endByte:bytesPerCluster;
    for(j=startByte;j<endbyte;j++){
        file->data_buf[index].buf[j] = buf[sp++];
    }
    //if there is only one cluster, write and return. else continue to write other clusters
    if(startCluster==endCluster)
        return sp;
    else{
        if(get_fat_entry_value(currentClus,&nextClust) == 1)
            return 0xffffffff;
            //alloc a new empty cluster
        if(nextClust > fat_info.total_data_clusters)
        {
            if(fs_alloc(&newEmptyClust)==1)
                return 0xffffffff;
            if(fs_modify_fat(currentClus,newEmptyClust)==1)
                return 0xffffffff;
            if(fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(newEmptyClust)) == 1)
                return 0xffffffff;
        }        
        currentClus = nextClust;
    }
    i = currentClus;
    //write inner clusters
    for(;i<endCluster;i++){   
        if(index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&(file->clock_head),LOCAL_DATA_BUF_NUM)==1)
            return 0xffffffff;
        file->data_buf[index].state = 3;
        for(j = 0;j< bytesPerCluster;j++)
            file->data_buf[index].buf[j] = buf[sp++];
        if(get_fat_entry_value(currentClus,&nextClust) == 1)
            return 0xffffffff;
            //alloc a new empty cluster
        if(nextClust > fat_info.total_data_clusters)
        {
            if(fs_alloc(&newEmptyClust)==1)
                return 0xffffffff;
            if(fs_modify_fat(currentClus,newEmptyClust)==1)
                return 0xffffffff;
            if(fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(newEmptyClust)) == 1)
                return 0xffffffff;
        }        
        currentClus = nextClust;
    }
    //write the endCluster;
    if(index = fs_read_4k(file->data_buf,fs_dataclus2sec(currentClus),&(file->clock_head),LOCAL_DATA_BUF_NUM) == 1)
        return 0xffffffff;
    file->data_buf[index].state = 3;
    for(i = 0;i<endByte;i++)
        file->data_buf[index].buf[j] = buf[sp++];

    return sp;    

}

/* lseek */
void fs_lseek(FILE *file, u32 new_loc) {
    u32 filesize = file->entry.attr.size;

    if (new_loc < filesize)
        file->loc = new_loc;
    else
        file->loc = filesize;
}

/* find an empty directory entry */
u32 fs_find_empty_entry(u32 *empty_entry, u32 index) {
    u32 i;
    u32 next_clus;
    u32 sec;

    while (1) {
        for (sec = 1; sec <= fat_info.BPB.attr.sectors_per_cluster; sec++) {
            /* Find directory entry in current cluster */
            for (i = 0; i < 512; i += 32) {
                /* If entry is empty */
                if ((*(dir_buf[index].buf + i) == 0) || (*(dir_buf[index].buf + i) == 0xE5)) {
                    *empty_entry = i;
                    goto after_fs_find_empty_entry;
                }
            }

            if (sec < fat_info.BPB.attr.sectors_per_cluster) {
                index = fs_read_512(dir_buf, dir_buf[index].cur + sec, &dir_clock_head, DIR_DATA_BUF_NUM);
                if (index == 0xffffffff)
                    goto fs_find_empty_entry_err;
            } else {
                /* Read next cluster of current directory */
                if (get_fat_entry_value(dir_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1, &next_clus) == 1)
                    goto fs_find_empty_entry_err;

                /* need to alloc a new cluster */
                if (next_clus > fat_info.total_data_clusters + 1) {
                    if (fs_alloc(&next_clus) == 1)
                        goto fs_find_empty_entry_err;

                    if (fs_modify_fat(fs_sec2dataclus(dir_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1), next_clus) == 1)
                        goto fs_find_empty_entry_err;

                    *empty_entry = 0;

                    if (fs_clr_512(dir_buf, &dir_clock_head, DIR_DATA_BUF_NUM, fs_dataclus2sec(next_clus)) == 1)
                        goto fs_find_empty_entry_err;
                }

                index = fs_read_512(dir_buf, fs_dataclus2sec(next_clus), &dir_clock_head, DIR_DATA_BUF_NUM);
                if (index == 0xffffffff)
                    goto fs_find_empty_entry_err;
            }
        }
    }

after_fs_find_empty_entry:
    return index;
fs_find_empty_entry_err:
    return 0xffffffff;
}

/* create an empty file with attr */
u32 fs_create_with_attr(u8 *filename, u8 attr) {
    u32 i;
    u32 l1 = 0;
    u32 l2 = 0;
    u32 empty_entry;
    u32 clus;
    u32 index;
    FILE file_creat;
    /* If file exists */
    if (fs_open(&file_creat, filename) == 0)
        goto fs_creat_err;

    for (i = 255; i >= 0; i--)
        if (file_creat.path[i] != 0) {
            l2 = i;
            break;
        }

    for (i = 255; i >= 0; i--)
        if (file_creat.path[i] == '/') {
            l1 = i;
            break;
        }

    /* If not root directory, find that directory */
    if (l1 != 0) {
        for (i = l1; i <= l2; i++)
            file_creat.path[i] = 0;

        if (fs_find(&file_creat) == 1)
            goto fs_creat_err;

        /* If path not found */
        if (file_creat.dir_entry_pos == 0xFFFFFFFF)
            goto fs_creat_err;

        clus = get_start_cluster(&file_creat);
        /* Open that directory */
        index = fs_read_512(dir_buf, fs_dataclus2sec(clus), &dir_clock_head, DIR_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_creat_err;

        file_creat.dir_entry_pos = clus;
    }
    /* otherwise, open root directory */
    else {
        index = fs_read_512(dir_buf, fs_dataclus2sec(2), &dir_clock_head, DIR_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_creat_err;

        file_creat.dir_entry_pos = 2;
    }

    /* find an empty entry */
    index = fs_find_empty_entry(&empty_entry, index);
    if (index == 0xffffffff)
        goto fs_creat_err;

    for (i = l1 + 1; i <= l2; i++)
        file_creat.path[i - l1 - 1] = filename[i];

    file_creat.path[l2 - l1] = 0;
    to_next_slash(file_creat.path);

    dir_buf[index].state = 3;

    /* write path */
    for (i = 0; i < 11; i++)
        *(dir_buf[index].buf + empty_entry + i) = currentFileName[i];

    /* write file attr */
    *(dir_buf[index].buf + empty_entry + 11) = attr;

    /* other should be zero */
    for (i = 12; i < 32; i++)
        *(dir_buf[index].buf + empty_entry + i) = 0;

    if (fs_fflush() == 1)
        goto fs_creat_err;

    return 0;
fs_creat_err:
    return 1;
}

u32 fs_create(u8 *filename) {
    return fs_create_with_attr(filename, 0x20);
}

void get_filename(u8 *entry, u8 *buf) {
    u32 i;
    u32 l1 = 0, l2 = 8;

    for (i = 0; i < 11; i++)
        buf[i] = entry[i];

    if (buf[0] == '.') {
        if (buf[1] == '.')
            buf[2] = 0;
        else
            buf[1] = 0;
    } else {
        for (i = 0; i < 8; i++)
            if (buf[i] == 0x20) {
                buf[i] = '.';
                l1 = i;
                break;
            }

        if (i == 8) {
            for (i = 11; i > 8; i--)
                buf[i] = buf[i - 1];

            buf[8] = '.';
            l1 = 8;
            l2 = 9;
        }

        for (i = l1 + 1; i < l1 + 4; i++) {
            if (buf[l2 + i - l1 - 1] != 0x20)
                buf[i] = buf[l2 + i - l1 - 1];
            else
                break;
        }

        buf[i] = 0;

        if (buf[i - 1] == '.')
            buf[i - 1] = 0;
    }
}
