#include <driver/vga.h>
#include <driver/ps2.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include "ext2.h"
#include "../fat/utils.h"

extern struct ext2_info ext_info;
extern BUF_4K ext2_block_buf[EXT_BUF_NUM];

u32 ext_cat(u8 *path) {
    EFILE catFile;

    /* Open */
    if (0 != ext_open(&catFile, path)) {
        log(LOG_FAIL, "File %s open failed", path);
        return 1;
    }

    //kernel_printf("catFile.inode = %d\n",catFile.inode);

    /* Read */
    u32 file_size = catFile.inode.attr.size;
    //kernel_printf("fileSize = %d\n",file_size);
    u8 *buf = (u8 *)kmalloc(file_size + 1);
    ext_read(&catFile, buf, file_size);
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);
    ext_close(&catFile);
    kfree(buf);
    return 0;
}

/* remove directory entry */
u32 ext_rm(u8 *filename)
{
    u32 i;
    EFILE rm_file;
    //open file
    if (ext_open(&rm_file, filename) == 1){
        log(LOG_FAIL,"The file %s dosen't exist.",filename);
        return 1;
    }

    //清除块位图相应的位
    //log(LOG_START,"clean the bitmap bit");
    u32 block_count = rm_file.inode.attr.block_count/ext_info.sector_per_block;
    u32 block_num;
    u32 index = 0;
    u8 bitmapByte;
    //read block bitmap
    read_block(ext2_block_buf[index].buf,ext_info.base_addr + ext_info.BGD.attr.block_bitmap_pos*ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + ext_info.BGD.attr.block_bitmap_pos*ext_info.sector_per_block;   
    kernel_getchar();

  /*  kernel_printf("block bitmap before modify:\n");
    for(i = 0;i<EXT2_BLOCK_SIZE/4;i++){
        kernel_printf("%d ",ext2_block_buf[index].buf[i]);
    }
    kernel_printf("\n");
    kernel_getchar();*/

    for(i=0;i<block_count;i++){
        block_num = rm_file.inode.attr.Block[i]; 
        bitmapByte = ext2_block_buf[index].buf[block_num/8];
       // kernel_printf("bitmapByte = %d\n",bitmapByte);
        //kernel_printf("block_num = %d\n",block_num);
        //kernel_printf("mask = %d\n",(((0xfe << (block_num%8))&0xff)  | ((( 0x00ff << (block_num%8))>>8)&0xff) ));
        bitmapByte = bitmapByte & (((0xfe << (block_num%8))&0xff)  | ((( 0x00ff << (block_num%8))>>8)&0xff) );
       // kernel_printf("bitmapByte = %d\n",bitmapByte);
       // kernel_getchar();
        ext2_block_buf[index].buf[block_num/8] = bitmapByte;
    }

  /*  kernel_printf("block bitmap after modify:\n");
    for(i = 0;i<EXT2_BLOCK_SIZE/4;i++){
        kernel_printf("%d ",ext2_block_buf[index].buf[i]);
    }
    kernel_printf("\n");
    kernel_getchar();*/

    //write back block bitmap
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    //log(LOG_END,"clean bitmap bit");
    //log(LOG_START,"clean the inode table");

    //清除inode表相应的inode
    //read inode table block
    read_block(ext2_block_buf[index].buf,ext_info.base_addr + rm_file.inode_block * ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + rm_file.inode_block * ext_info.sector_per_block;
    kernel_memset(ext2_block_buf[index].buf + ext_info.inode_size*rm_file.inode_offset%(SECTOR_SIZE/ext_info.inode_size),0,ext_info.inode_size);
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    //log(LOG_END,"clean inode table");

    //log(LOG_START,"clean the inode bitmap bit");

    //清除inode位图相应的位
    read_block(ext2_block_buf[index].buf,ext_info.base_addr + ext_info.BGD.attr.inode_bitmap_pos*ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + ext_info.BGD.attr.inode_bitmap_pos*ext_info.sector_per_block;
    u32 inode_num = rm_file.inode_offset;
/*
    kernel_printf("inode bitmap before modify:\n");
    for(i = 0;i<EXT2_BLOCK_SIZE/4;i++){
        kernel_printf("%d ",ext2_block_buf[index].buf[i]);
    }
    kernel_printf("\n");*/

   // kernel_printf("bitmapByte = %d\n",bitmapByte);
   // kernel_printf("inode_num = %d\n",inode_num);
    u32 mask = (((0xfe << (inode_num%8))&0xff)  | ((( 0x00ff << (inode_num%8))>>8)&0xff) );
   // kernel_printf("mask = %d\n",(((0xfe << (inode_num%8))&0xff)  | ((( 0x00ff << (inode_num%8))>>8)&0xff) ));
    bitmapByte = ext2_block_buf[index].buf[inode_num/8];
    bitmapByte = bitmapByte & mask;
    ext2_block_buf[index].buf[inode_num/8] = bitmapByte;
    //kernel_printf("bitmapByte = %d\n",bitmapByte);
    //kernel_getchar();

  /*  kernel_printf("inode bitmap after modify:\n");
    for(i = 0;i<EXT2_BLOCK_SIZE/4;i++){
        kernel_printf("%d ",ext2_block_buf[index].buf[i]);
    }
    kernel_printf("\n");*/
    

    //write back inode bitmap
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
   // kernel_printf("ext2_block_buf[index].cur = %d\n",ext2_block_buf[index].cur);
    //log(LOG_END,"clean inode bitmap");

    //log(LOG_START,"modify record");

    //从父目录块中清除目录项并将之后的项向前移动

   // kernel_printf("ext_info.base_addr + rm_file.father_dir_block * ext_info.sector_per_block = %d\n",ext_info.base_addr + rm_file.father_dir_block * ext_info.sector_per_block);
    read_block(ext2_block_buf[index].buf,ext_info.base_addr + rm_file.father_dir_block * ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + rm_file.father_dir_block * ext_info.sector_per_block;
   // kernel_printf("ext2_block_buf[index].cur = %d\n",ext2_block_buf[index].cur);
    
   /* kernel_printf("dir block befor rm\n");
    for(i=0;i<256;i++){
        kernel_printf("%c ",ext2_block_buf[index].buf[i]);
    }
    kernel_getchar();*/
    
    u32 j,k,last_entry_offset,last2_entry_offset,last_entry_len,last2_entry_len,last;
    u16 record_len,rm_record_len;
    u8 name_len;
    rm_record_len = get_u16(ext2_block_buf[index].buf+4+rm_file.father_dir_offset);
 
    for(j = 0;j<EXT2_BLOCK_SIZE;){
        //get the information of every dir entry
        inode_num = get_u32(ext2_block_buf[index].buf+j);
        record_len = get_u16(ext2_block_buf[index].buf+j + 4);
        name_len = *(ext2_block_buf[index].buf+j + 6);
        if(j+record_len == EXT2_BLOCK_SIZE){
            last_entry_offset = j;
            last_entry_len = record_len;
            last2_entry_offset = last;
        }
        //the entry is the last entry
        last = j;
        j+=record_len;                 
    }
   /* kernel_printf("rm_record_len = %d\n",rm_record_len);
    kernel_printf("last_record_len = %d\n",last_entry_len);
    kernel_printf("last_record_offset = %d\n",last_entry_offset);
   */

    //if this is the last entry
    if(last_entry_offset==rm_file.father_dir_offset){
        //last 2 entry length + last length
        last2_entry_len = get_u16(ext2_block_buf[index].buf+last2_entry_offset+4);
        set_u16(ext2_block_buf[index].buf+last2_entry_offset+4,last2_entry_len+last_entry_len);
    }
    //else change the length of last record
    else{
        set_u16(ext2_block_buf[index].buf+last_entry_offset+4,last_entry_len+rm_record_len);
    }
    //move latter records forward and set 0 for padding
    u8* memcpy_dest = ext2_block_buf[index].buf+rm_file.father_dir_offset;
    u8* memcpy_source = ext2_block_buf[index].buf+rm_file.father_dir_offset+rm_record_len;
    u32 memcpy_len = EXT2_BLOCK_SIZE-(rm_file.father_dir_offset+rm_record_len);

   /* kernel_printf("memcpy_dest = %d\n",rm_file.father_dir_offset);
    kernel_printf("memcpy_source = %d\n",rm_file.father_dir_offset+rm_record_len);
    kernel_printf("memcpy_len = %d\n",memcpy_len);
*/
    kernel_memcpy(memcpy_dest,memcpy_source,memcpy_len);
    kernel_memset(ext2_block_buf[index].buf+EXT2_BLOCK_SIZE-rm_record_len,0,rm_record_len);
    
    /*kernel_printf("dir block befor rm\n");
    for(i=0;i<256;i++){
        kernel_printf("%c ",ext2_block_buf[index].buf[i]);
    }
    kernel_getchar();*/
    //kernel_printf("ext2_block_buf[index].cur = %d\n",ext2_block_buf[index].cur);    
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    //log(LOG_END,"clean the inode bitmap bit");
    return 0;
}

u32 ext_ls(u8* path)
{
    EFILE ls_file;
    u8 rootpath[3];
    rootpath[0] = '/';
    rootpath[1] = '.';
    rootpath[2] = 0;

    /* Open */
    if(path[0] == '/' && path[1]==0){
        if (0 != ext_open(&ls_file, rootpath)) {
            log(LOG_FAIL, "File %s open dir", rootpath);
            return 1;
        }
    }    
    else {
        if (0 != ext_open(&ls_file, path)) {
            log(LOG_FAIL, "File %s open dir", path);
            return 1;
        }
    }
    //log(LOG_OK,"Open dir successful");
    //read inode of this dir
    u8 buf_512[512];
    union inode_info dir_inode;
    read_block(buf_512, ext_info.base_addr + ls_file.inode_block * ext_info.sector_per_block + (ls_file.inode_offset *ext_info.inode_size)/SECTOR_SIZE,1);
    kernel_memcpy(dir_inode.data,buf_512 + ext_info.inode_size*(ls_file.inode_offset%(SECTOR_SIZE/ext_info.inode_size)),ext_info.inode_size);

    u32 blocks_of_this_file = dir_inode.attr.size / EXT2_BLOCK_SIZE;
    u32 i,j,k,index;
    u32 inode_num;
    u16 record_len;
    u8 file_type,name_len;
    u8 filename[100];
    //kernel_printf("blocks_of_this_file = %d\n",blocks_of_this_file);
    for(i = 0;i<blocks_of_this_file && i < 12;i++){    //we just consider immediate address here
        index = 0;
        if(dir_inode.attr.Block[i] == 0) continue;
        read_block(ext2_block_buf[0].buf,ext_info.base_addr + dir_inode.attr.Block[i] * ext_info.sector_per_block,8);
        //traverse every entry this block
        for(j = 0;j<EXT2_BLOCK_SIZE;){
            //get the information of this dir entry
            kernel_memset(filename,0,sizeof(filename));
            inode_num = get_u32(ext2_block_buf[index].buf+j);
            record_len = get_u16(ext2_block_buf[index].buf+j + 4);
            name_len = *(ext2_block_buf[index].buf+j + 6);
            file_type = *(ext2_block_buf[index].buf+j + 7);
            for(k = 0;k<name_len;k++)
                filename[k] = *(ext2_block_buf[index].buf+j+8+k);
            filename[name_len] = 0;
            if(file_type == 2)
                kernel_printf("%s/\n",filename);
            else    
                kernel_printf("%s\n",filename);
            j+=record_len;             
        }  
    }    
    return 0;
}

u32 ext_mv(u8* parm)
{
    u32 i,j;
    u8 src[256], dest[256];
    for(i = 0;parm[i]!= ' ';i++)
        src[i] = parm[i];
    src[i] = 0;
    for(;parm[i]==' ';i++)
        ;
    for(j = 0;parm[i]!= 0;i++,j++)
        dest[j] = parm[i];
    dest[j] = 0;
    //kernel_printf("src = %s, des = %s\n",src,dest);

    EFILE mv_file,new_file;
    u32 index = 0;
    u32 index2 = 1;
    /* if src not exists */
    if (ext_open(&mv_file, src) == 1){
        log(LOG_FAIL,"src file %s doesn't exist",src);
        return 1;
    }
   // log(LOG_OK,"src file opened");
   // kernel_printf("mv_file.path = %s\n",mv_file.path);
    //kernel_printf("mv_file.type = %d\n",mv_file.type);

    /* create dest */
    if (ext_create_with_attr(dest, mv_file.type) == 1)
        return 1;
    //log(LOG_OK,"dest file created");
    if(ext_open(&new_file,dest) == 1){
        log(LOG_FAIL,"dest file open");
        return 1;
    }
    //log(LOG_OK,"dest file opened");    

    //log(LOG_START,"copy inode");

    /* copy inode data */
    //kernel_printf("src: inode block = %d, inode_offset = %d\n",mv_file.inode_block,mv_file.inode_offset);
   // kernel_printf("dest: inode block = %d, inode_offset = %d\n",new_file.inode_block,new_file.inode_offset);

   // kernel_printf("src = %d\n",memcpy_source);
   // kernel_printf("dest = %d\n",memcpy_dest);

    kernel_memcpy(new_file.inode.data,mv_file.inode.data,ext_info.inode_size);

    /*kernel_printf("source\n");
    for(i=0;i<256;i++){
        kernel_printf("%d ",*(ext2_block_buf[index].buf+i+memcpy_source));
    }
    kernel_printf("\n");
    kernel_getchar();
    
    kernel_printf("dest\n");
    for(i=0;i<256;i++){
        kernel_printf("%d ",*(ext2_block_buf[index2].buf+i+memcpy_dest));
    }
    kernel_printf("\n");
    kernel_getchar();*/

    kernel_memset(mv_file.inode.data,0,ext_info.inode_size);
    //kernel_printf("cur2 = %d\n",ext2_block_buf[index2].cur);
    //kernel_printf("cur = %d\n",ext2_block_buf[index].cur);
    //kernel_getchar();
    
    //log(LOG_END,"copy inode");

   // log(LOG_START,"rm src");
    
    //delete source inode

    //inode bitmap
    read_block(ext2_block_buf[index].buf,ext_info.base_addr + ext_info.BGD.attr.inode_bitmap_pos*ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + ext_info.BGD.attr.inode_bitmap_pos*ext_info.sector_per_block;
    u32 inode_num = mv_file.inode_offset;
    u32 bitmapByte;
    u32 mask = (((0xfe << (inode_num%8))&0xff)  | ((( 0x00ff << (inode_num%8))>>8)&0xff) );
    bitmapByte = ext2_block_buf[index].buf[inode_num/8];
    bitmapByte = bitmapByte & mask;
    ext2_block_buf[index].buf[inode_num/8] = bitmapByte;
    //write back inode bitmap
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    
    //从父目录块中清除目录项并将之后的项向前移动

    read_block(ext2_block_buf[index].buf,ext_info.base_addr + mv_file.father_dir_block * ext_info.sector_per_block,8);
    ext2_block_buf[index].cur = ext_info.base_addr + mv_file.father_dir_block * ext_info.sector_per_block;
   
    u32 last_entry_offset,last2_entry_offset,last_entry_len,last2_entry_len,last;
    u16 record_len,rm_record_len;
    u8 name_len;
    rm_record_len = get_u16(ext2_block_buf[index].buf+4+mv_file.father_dir_offset);
 
    for(j = 0;j<EXT2_BLOCK_SIZE;){
        //get the information of every dir entry
        inode_num = get_u32(ext2_block_buf[index].buf+j);
        record_len = get_u16(ext2_block_buf[index].buf+j + 4);
        name_len = *(ext2_block_buf[index].buf+j + 6);
        if(j+record_len == EXT2_BLOCK_SIZE){
            last_entry_offset = j;
            last_entry_len = record_len;
            last2_entry_offset = last;
        }
        //the entry is the last entry
        last = j;
        j+=record_len;                 
    }
  
    //if this is the last entry
    if(last_entry_offset==mv_file.father_dir_offset){
        //last 2 entry length + last length
        last2_entry_len = get_u16(ext2_block_buf[index].buf+last2_entry_offset+4);
        set_u16(ext2_block_buf[index].buf+last2_entry_offset+4,last2_entry_len+last_entry_len);
    }
    //else change the length of last record
    else{
        set_u16(ext2_block_buf[index].buf+last_entry_offset+4,last_entry_len+rm_record_len);
    }
    //move latter records forward and set 0 for padding
    u8* memcpy_dest1 = ext2_block_buf[index].buf+mv_file.father_dir_offset;
    u8* memcpy_source1 = ext2_block_buf[index].buf+mv_file.father_dir_offset+rm_record_len;
    u32 memcpy_len = EXT2_BLOCK_SIZE-(mv_file.father_dir_offset+rm_record_len);
  
    kernel_memcpy(memcpy_dest1,memcpy_source1,memcpy_len);
    kernel_memset(ext2_block_buf[index].buf+EXT2_BLOCK_SIZE-rm_record_len,0,rm_record_len);
    
    //kernel_printf("ext2_block_buf[index].cur = %d\n",ext2_block_buf[index].cur);    
    write_block(ext2_block_buf[index].buf,ext2_block_buf[index].cur,8);
    //log(LOG_END,"clean the inode bitmap bit");

    ext_close(&new_file);
    ext_close(&mv_file);
    return 0;

}
