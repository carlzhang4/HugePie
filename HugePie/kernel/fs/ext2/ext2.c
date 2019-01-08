#include "ext2.h"
#include <driver/vga.h>
#include <driver/ps2.h>
#include <zjunix/log.h>
#include "../fat/utils.h"

struct ext2_info ext_info;
extern u8 MBR_buf[512];
u8 currentFileName[12];
BUF_4K ext2_block_buf[EXT_BUF_NUM];

u32 ext2_buf_clock_head = 0;

u32 init_ext2_info()
{
    //in MBR, Partition starts from 462 and +8 is the start sector
    ext_info.base_addr = get_u32(MBR_buf + 462 + 8);
    //read SuperBlock
    if(read_block(ext_info.superblock.data,ext_info.base_addr+2,1) == 1)
        return 1;
    log(LOG_OK,"SuperBlock read");

    //read BGD
    u8 BGD_temp[512];
    if(read_block(BGD_temp,ext_info.base_addr + (ext_info.superblock.attr.first_block+1)*(EXT2_BLOCK_SIZE/SECTOR_SIZE),1) == 1)
        return 1; 
    kernel_memcpy(ext_info.BGD.data,BGD_temp,sizeof(ext_info.BGD.data));    
    log(LOG_OK,"BGD read");

    //update necessary information for ext_info
    ext_info.block_per_group = ext_info.superblock.attr.block_per_group;
    ext_info.block_size = EXT2_BLOCK_SIZE; 
    ext_info.inode_per_group = ext_info.superblock.attr.inode_per_group;
    ext_info.inode_size = ext_info.superblock.attr.inode_size;
    ext_info.first_block = ext_info.superblock.attr.first_block;  
    ext_info.group_number = (ext_info.superblock.attr.block_count - ext_info.first_block - 1) / ext_info.block_per_group + 1;
    ext_info.block_bit_map_block = ext_info.BGD.attr.block_bitmap_pos;
    ext_info.inode_bit_map_block = ext_info.BGD.attr.inode_bitmap_pos;
    ext_info.inode_table_block = ext_info.BGD.attr.inode_table_pos;
    ext_info.sector_per_block = EXT2_BLOCK_SIZE/SECTOR_SIZE;

    return 0;
}

void init_ext_buf() {
    int i = 0;
    for (i = 0; i < EXT_BUF_NUM; i++) {
        ext2_block_buf[i].cur = 0xffffffff;
        ext2_block_buf[i].state = 0;
    }
}

u32 init_ext(){
    if(init_ext2_info()==1){
        log(LOG_FAIL,"Init ext2 info failed\n");
        return 1;
    }
    init_ext_buf();
    return 0;
}

// find the offset of next slash and get the filename/subdir between two slashes
u32 eto_next_slash(u8 *f)
{
    u32 i,j,k;
    for(i = 0;i<11;i++) 
        currentFileName[i] = 0;
    currentFileName[11] = 0;
    j = 0;
    k = 0;    
    for(i = 0;(*(f+i)!=0) && (*(f+i) != '/');i++)
    ;
    for(j = 0; j < 12 && (*(f+j)!= 0) && j < i;j++,k++){
            currentFileName[k] = (u8)(*(f+j));
    }
    return i;

}
// if s1 == s2, return 0, else return 1;
u32 efilename_cmp(u8 *s1, u8 *s2)
{
    u32 i;
    for(i = 0; i<11;i++){
        if(s1[i] != s2[i])
            return 1;
        if(s1[i] == 0 && s2[i]==0)
            break;
    }
    return 0;
}

u32 ext_find(EFILE* file)
{
    u32 index;
    u8 *f = file->path;
    u32 next_slash;
    u32 blocks_of_this_file = 0;
    u32 i,j,k;
    u32 currentInodeTableBlcok = ext_info.inode_table_block;
    u32 currentInodeOffset = 1;
    // inode buffer is root dir at first
    union inode_info inode_current;
    u8 buf_512[512];
    u32 found_in_this_block;    //found file in this block
    int found_in_this_dir;      //found file in one of the block of the dir 
    if(*f++ != '/')
        return 1;
    while(1){
        //read current inode
        read_block(buf_512, ext_info.base_addr + currentInodeTableBlcok * ext_info.sector_per_block + (currentInodeOffset*ext_info.inode_size)/SECTOR_SIZE,1);
        kernel_memcpy(inode_current.data,buf_512 + ext_info.inode_size*(currentInodeOffset%(SECTOR_SIZE/ext_info.inode_size)),ext_info.inode_size);

        next_slash = eto_next_slash(f);
        blocks_of_this_file = inode_current.attr.size / EXT2_BLOCK_SIZE;
        
        u32 inode_num;
        u16 record_len;
        u8 file_type,name_len;
        u8 filename[12];
        //check all block of this dir inode
        found_in_this_block = 0;
        for(i = 0;i<blocks_of_this_file && i < 12;i++){    //we just consider immediate address here
            index = 0;
            read_block(ext2_block_buf[0].buf,ext_info.base_addr + inode_current.attr.Block[i] * ext_info.sector_per_block,8);

            //search every entry this block
            for(j = 0;j<EXT2_BLOCK_SIZE;){
                //get the information of this dir entry
                kernel_memset(filename,0,sizeof(filename));
                inode_num = get_u32(ext2_block_buf[index].buf+j);
                record_len = get_u16(ext2_block_buf[index].buf+j + 4);
                name_len = *(ext2_block_buf[index].buf+j + 6);
                file_type = *(ext2_block_buf[index].buf+j + 7);
                for(k = 0;k<name_len;k++)
                    filename[k] = *(ext2_block_buf[index].buf+j+8+k);
                j+=record_len;

                //if found that file of currentFileName
                if(efilename_cmp(currentFileName,filename)==0){
                    read_block(buf_512, ext_info.base_addr + currentInodeTableBlcok * ext_info.sector_per_block + ((inode_num-1)*ext_info.inode_size)/SECTOR_SIZE,1);
                    kernel_memcpy(inode_current.data,buf_512 + ext_info.inode_size*((inode_num-1)%(SECTOR_SIZE/ext_info.inode_size)),ext_info.inode_size);
                    kernel_memcpy(&file->inode.data,&inode_current.data,sizeof(inode_current.data));
                    file->inode_offset = inode_num-1;
                    file->inode_block = currentInodeTableBlcok;
                    found_in_this_block = 1;
                    break;
                }                    
            }
            //if found in this dir, we need to go to next slash of path and find again
            if(found_in_this_block == 1){
                found_in_this_dir  = 1;
                break;
            } 
            //else we need to fetch next block of the dir inicated by this inode
            else{
                //if this is the last block of this dir and didn't find the file, then can't find the file
                if(inode_current.attr.Block[i] == 0){
                    file->inode_offset = 0xffffffff;
                    return 0;
                }
                continue;
            }
        }
        if(found_in_this_dir == 0){
            file->inode_offset = 0xffffffff;
            return 0;
        }
        else{
            //if come to end of the path
            if(f[next_slash] == 0){
                return 0;        
            }
            //if didn't come to end of path and didn't point to a subdir 
            else if(file_type != 2){
                log(LOG_FAIL,"didn't come to end of path and didn't point to a subdir");
                return 1;
            }
            //find next name between slash
            else{
                currentInodeOffset = inode_num;
                currentInodeTableBlcok = currentInodeTableBlcok; //assume that we won't come to inode table in other block group
                f = f + next_slash + 1;
            }
        }
    }
}

u32 ext_open(EFILE *file, u8 * filename)
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
    if(ext_find(file) == 1){
        log(LOG_FAIL,"ext_open:error when finding the file %s",filename);
        return 1;
    }
    //if doesn't exist
    if(file->inode_offset == 0xffffffff)
        return 1;
    return 0;
}

u32 ext_read(EFILE *file,u8 * buf, u32 count)
{
    u32 index,i,j;
    u32 sp = 0;
    u32 StartBlock,StartByte;
    u32 EndBlock,EndByte;
    u32 blockStartByte,blockEndByte;
    u32 bytesPerBlock = ext_info.sector_per_block * SECTOR_SIZE;

    //if count exceeds the file, count will be filesize-loc
    if(file->loc + count > file->inode.attr.size)
        count = file->inode.attr.size - file->loc;
    //if count=0, return 0
    if(count == 0)
        return 0;
    //if the file is empty
    if(file->inode.attr.size == 0)
        return 0;

    StartBlock = file->loc / bytesPerBlock;
    kernel_printf("StartBlock = %d\n",StartBlock);
    StartByte = file->loc % bytesPerBlock;
    EndBlock = (file->loc + count - 1) / bytesPerBlock;
    EndByte = (file->loc +  count - 1) % bytesPerBlock;

    for(i=StartBlock;i<=EndBlock;i++){
        if(i==StartBlock)
            blockStartByte = StartByte;
        else blockStartByte = 0;
        if(i == EndBlock)
            blockEndByte = EndByte;
        else blockEndByte = bytesPerBlock-1;
        //immediate address
        if(i < 12){
            index = 1;
            read_block(ext2_block_buf[index].buf,ext_info.base_addr + file->inode.attr.Block[i] * ext_info.sector_per_block,8);
        }
        //1 level index
        else if(i >= 12 && i < 1036){

        }
        //2 level index
        else if (i >= 1036 && i < 1049612){

        }
        //3 level index
        else if(i >= 1049612){

        }
        else 
            return 0xffffffff;
        for(j=blockStartByte;j<blockEndByte;j++){
            buf[sp++] = ext2_block_buf[index].buf[j];
        }
    }
    return sp;
}

u32 alloc_new_block(){
    u8 bitmapByte;
    u32 i,emptyBlockNo,flag = 0;
    read_block(ext2_block_buf[3].buf,ext_info.base_addr + ext_info.sector_per_block * ext_info.BGD.attr.inode_bitmap_pos,8);
    ext2_block_buf[3].cur = ext_info.base_addr + ext_info.sector_per_block * ext_info.BGD.attr.inode_bitmap_pos;
    for(i=0;i<ext_info.inode_per_group/8;i++){
        bitmapByte = ext2_block_buf[3].buf[i];
        if(bitmapByte & 0x01 == 0){
            emptyBlockNo = emptyBlockNo | 0x01;
            emptyBlockNo = i*8+0;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x02 == 0){
            emptyBlockNo = emptyBlockNo | 0x02;
            emptyBlockNo = i*8+1;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x04 == 0){
            emptyBlockNo = emptyBlockNo | 0x04;
            emptyBlockNo = i*8+2;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x08 == 0){
            emptyBlockNo = emptyBlockNo | 0x08;
            emptyBlockNo = i*8+3;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x10 == 0){
            emptyBlockNo = emptyBlockNo | 0x10;
            emptyBlockNo = i*8+4;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x20 == 0){
            emptyBlockNo = emptyBlockNo | 0x20;
            emptyBlockNo = i*8+5;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x40 == 0){
            emptyBlockNo = emptyBlockNo | 0x40;
            emptyBlockNo = i*8+6;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x80 == 0){
            emptyBlockNo = emptyBlockNo | 0x80;
            emptyBlockNo = i*8+7;
            flag = 1;
            break;
        }
    }
    if(flag == 0) return 0xffffffff;
    write_block(ext2_block_buf[3].buf,ext2_block_buf[3].cur,8);
    return emptyBlockNo;
}


u32 alloc_new_inode(){
    u8 bitmapByte;
    u32 i,emptyInodeOffset,flag=0;
    read_block(ext2_block_buf[3].buf,ext_info.base_addr + ext_info.sector_per_block * ext_info.BGD.attr.block_bitmap_pos,8);
    ext2_block_buf[3].cur = ext_info.base_addr + ext_info.sector_per_block * ext_info.BGD.attr.inode_bitmap_pos;
    for(i=0;i<ext_info.inode_per_group/8;i++){
        bitmapByte = ext2_block_buf[3].buf[i];
        if(bitmapByte & 0x01 == 0){
            bitmapByte = bitmapByte | 0x01;
            emptyInodeOffset = i*8+0;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x02 == 0){
            bitmapByte = bitmapByte | 0x02; 
            emptyInodeOffset = i*8+1;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x04 == 0){
            bitmapByte = bitmapByte | 0x04;
            emptyInodeOffset = i*8+2;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x08 == 0){
            bitmapByte = bitmapByte | 0x08;
            emptyInodeOffset = i*8+3;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x10 == 0){
            bitmapByte = bitmapByte | 0x10;
            emptyInodeOffset = i*8+4;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x20 == 0){
            bitmapByte = bitmapByte | 0x20;
            emptyInodeOffset = i*8+5;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x40 == 0){
            bitmapByte = bitmapByte | 0x40;
            emptyInodeOffset = i*8+6;
            flag = 1;
            break;
        }
        if(bitmapByte & 0x80 == 0){
            bitmapByte = bitmapByte | 0x80;
            emptyInodeOffset = i*8+7;
            flag = 1;
            break;
        }
    }
    if(flag == 0) return 0xffffffff;
    write_block(ext2_block_buf[3].buf,ext2_block_buf[3].cur,8);
    return emptyInodeOffset;
}

u32 ext_write(EFILE* file, u8* buf ,u32 count){
    u32 index,i,j;
    u32 sp = 0;
    u32 StartBlock,StartByte;
    u32 EndBlock,EndByte;
    u32 blockStartByte,blockEndByte;
    u32 bytesPerBlock = ext_info.sector_per_block * SECTOR_SIZE;
    u32 new_empty_block;
    StartBlock = (file->loc) / bytesPerBlock;
    StartByte = file->loc % bytesPerBlock;
    EndBlock = (file->loc + count - 1) / bytesPerBlock;
    EndByte = (file->loc +  count - 1) % bytesPerBlock;

    //if count=0, return 0
    if(count == 0)
        return 0;
    for(i=0;i<StartBlock;i++){
        //alloc a new block if blokc[i]==0
        if(file->inode.attr.Block[i]==0){
            new_empty_block = alloc_new_block();
            if(new_empty_block == 0xffffffff) return 1;
            file->inode.attr.Block[i] = new_empty_block;
        }
    }

    for(i=StartBlock;i<EndBlock;i++){
        if(i==StartBlock)
            blockStartByte = StartByte;
        else blockStartByte = 0;
        if(i == EndBlock)
            blockEndByte = EndByte;
        else blockEndByte = bytesPerBlock-1;
        //immediate address
        if(i < 12){
            if(file->inode.attr.Block[i]==0)
            new_empty_block = alloc_new_block();
            if(new_empty_block==0xffffffff) return 1;
            file->inode.attr.Block[i] = new_empty_block;    
            index = 1;
            read_block(ext2_block_buf[index].buf,ext_info.base_addr + file->inode.attr.Block[i] * ext_info.sector_per_block,8);
            ext2_block_buf[index].cur = ext_info.base_addr + file->inode.attr.Block[i] * ext_info.sector_per_block;
        }
        //1 level index
        else if(i >= 12 && i < 1036){

        }
        //2 level index
        else if (i >= 1036 && i < 1049612){

        }
        //3 level index
        else if(i >= 1049612){

        }
        else 
            return 0xffffffff;
        for(j=blockStartByte;j<blockEndByte;j++){
            ext2_block_buf[index].buf[j] = buf[sp++];
        }
        write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    }
    return sp;
}

 


/* create an empty file with attr */
u32 ext_create_with_attr(u8 *filename, u8 attr) {
    u32 i;
    u32 l1 = 0;
    u32 l2 = 0;
    u32 empty_entry;
    u32 clus;
    u32 index;
    EFILE file_creat;
    /* If file exists */
    if (ext_open(&file_creat, filename) == 0)
        return 1;

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

        if (ext_find(&file_creat) == 1)
            return 1;

        /* If path not found */
        if (file_creat.inode_offset == 0xFFFFFFFF)
            return 1;

    }
    /* otherwise, open root directory */
    else {
        u8 buf_512[512];
        u8 root_dir_inode[256];

        read_block(buf_512, ext_info.base_addr + ext_info.inode_table_block * ext_info.sector_per_block + (1*ext_info.inode_size)/SECTOR_SIZE,1);
        kernel_memcpy(file_creat.inode.data,buf_512 + ext_info.inode_size*(1%(SECTOR_SIZE/ext_info.inode_size)),ext_info.inode_size);


        file_creat.inode_offset = 1;
    }
    u32 block_No = 0;
    //calculate new entry length
    for(i=0;i<12;i++){
        if(currentFileName[i]==0)
            break;
    }
    if(i == 12) i--;
    u32 name_len = i>4?i:4; 
    u32 entry_len = name_len + 8;
    u32 last_entry_begin;
    u32 last_entry_len;
    while(1){
        index = 2;
        read_block(ext2_block_buf[index].buf,ext_info.base_addr + file_creat.inode.attr.Block[block_No] * ext_info.sector_per_block,8);  
        ext2_block_buf[index].cur = ext_info.base_addr + file_creat.inode.attr.Block[block_No] * ext_info.sector_per_block;

        u32 current_name_len, current_entry_len;
        //go to the current last_entry of this dir
        for(i=0;;){
            current_entry_len = get_u16(ext2_block_buf[2].buf+i+4);
            current_name_len = ext2_block_buf[2].buf[i+6];
            if(i + current_entry_len == ext_info.block_size){
                current_entry_len = current_name_len + 8;
                set_u32(ext2_block_buf[2].buf+i+4,current_entry_len);
                i+=current_entry_len;
                break;
            }
        }
        last_entry_begin = i;
        last_entry_len = ext_info.block_size - i;
        if(last_entry_len >= entry_len)
            break;
        else{
            block_No++;
            if(block_No >= 12){
                log(LOG_FAIL,"Too large dir");
                return 1;
            }
            //if there is no more blocks, alloc a new block
            if(file_creat.inode.attr.Block[block_No] == 0){
                file_creat.inode.attr.Block[block_No] = alloc_new_block();
            }
        }
    }
    //now index 2 is the content of the first block of this dir
        
    u32 empty_inode_offset;
    u8 bitmapByte;
    //find an empty inode
    empty_inode_offset = alloc_new_inode();

    //generate a dir entry
    u8 entry[32];
    set_u32(entry,empty_inode_offset);
    set_u16(entry+4,last_entry_len);
    entry[6] = name_len;
    entry[7] = attr;
    for(i=0;i<name_len;i++){
        entry[i+8] = currentFileName[i];
    }
    for(i=8+name_len;i<32;i++){
        entry[i] = 0;
    }
    //copy the entry to buffer
    for(i = 0;i<32;i++)
        ext2_block_buf[2].buf[i+last_entry_begin] = entry[i];
        //padding
    for(i=last_entry_begin+32;i<ext_info.block_size; i++)
        ext2_block_buf[2].buf[i+last_entry_begin] = 0;
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    return 0;
}

u32 ext_close(EFILE* file)
{
    u32 i;
    u32 index = 0;
    u8 buf_512[512];
    union inode_info close_inode;
    //read the sector of inode, modify the inode in this block and writeback the sector
    read_block(buf_512, ext_info.base_addr + file->inode_block * ext_info.sector_per_block + (file->inode_offset*ext_info.inode_size)/SECTOR_SIZE,1);
    kernel_memcpy(buf_512 + ext_info.inode_size*(file->inode_offset%(SECTOR_SIZE/ext_info.inode_size)),file->inode.data,ext_info.inode_size);    
    write_block(buf_512,ext_info.base_addr + file->inode_block * ext_info.sector_per_block + (file->inode_offset*ext_info.inode_size)/SECTOR_SIZE,1);

}


