#ifndef _ZJUNIX_FS_EXT2_H
#define _ZJUNIX_FS_EXT2_H

#include <zjunix/type.h>
#include <zjunix/fs/fscache.h>

#define LOCAL_DATA_BUF_NUM 4
#define DIR_BUF_NUM 4
#define SECTOR_SIZE 512
#define EXT2_BLOCK_SIZE 4096
#define EXT_BUF_NUM 2

struct __attribute__((__packed__)) superblock_attr{
    u32 inode_count;
    u32 block_count;
    u32 reserved_block_count;
    u32 free_blocks;
    u32 free_inodes;
    u32 first_block;
    u32 log_related_low;
    u32 log_related_high;
    u32 block_per_group;
    u32 fragment_per_group;
    u32 inode_per_group;
    u32 last_mount_time;
    u32 last_write_time;
    u16 mount_count;
    u16 max_mount;
    u16 magic;
    u16 fs_stage;
    u16 err_behav;
    u16 minor_rev;
    u32 last_checked;
    u32 checked_interval;
    u32 OS_type;
    u32 major_rev;
    u16 reserved_block_uid;
    u16 reserved_block_gid;
    u32 first_nonreserved_inode;
    u16 inode_size;
    u16 block_group;
    u8 meaningless[419];
};

union superblock_info{
    u8 data[512];
    struct superblock_attr attr;
};

struct __attribute__((__packed__)) BGD_attr{
    u32 block_bitmap_pos;
    u32 inode_bitmap_pos;
    u32 inode_table_pos;
    u16 free_block;
    u16 free_inode;
    u16 directory;
    u8 padding[14];
};

union block_group_description{
    u8 data[19];
    struct BGD_attr attr;
};

struct __attribute__((__packed__)) inode_attr{
    u16 st_mode;
    u16 user_No;
    u32 size;
    u32 atime;
    u32 ctime;
    u32 mtime;
    u32 dtime;
    u16 Group;
    u16 Link_number;
    u32 block_count;        //512Bytes counts 1 block
    u32 flags;
    u32 OS_info;
    u32 Block[15];
    u8 padding[28];
    u8 padding2[128];
};

union inode_info{
    u8 data[256];
    struct inode_attr attr;
};

struct ext2_info{
    u32 base_addr;
    u32 sector_per_block;
    u32 group_number;
    u32 block_size;
    u32 inode_size;
    u32 inode_per_group;
    u32 block_per_group;
    u32 first_block;
    u32 block_bit_map_block;
    u32 inode_bit_map_block;
    u32 inode_table_block;
    union superblock_info superblock;
    union block_group_description BGD;
};

typedef struct ext_file{
    //file absolute path
    u8 path [512];
    //fp
    u32 loc;
    u8 type;
    //block and offset of father dir
    u32 father_dir_block;
    u32 father_dir_offset;
    //inode group and block and offset in inode_size
    u32 inode_group;
    u32 inode_block;
    u32 inode_offset;       //Warning: here, offset counts from 0, In ext2 inode struct, the inode number conut from 1
    //inode information
    union inode_info inode;
    //clokc head and buffer
    u32 clock_head;
    BUF_4K data_buf[LOCAL_DATA_BUF_NUM];

}EFILE;

u32 init_ext();
u32 ext_cat(u8 *path);
u32 init_ext();
u32 ext_read(EFILE *file,u8 * buf, u32 count);
u32 ext_open(EFILE *file, u8 * filename);
u32 ext_find(EFILE* file);
u32 ext_close(EFILE* file);
u32 ext_rm(u8 *filename);
unsigned long ext_ls(u8* path);
u32 ext_mv(u8* parm);
u32 ext_create_with_attr(u8 *filename, u8 type);

#endif