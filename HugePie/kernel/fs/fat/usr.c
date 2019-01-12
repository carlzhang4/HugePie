#include <driver/vga.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include "fat.h"
#include "utils.h"

u8 mk_dir_buf[32];
u8 mk_dir_buf_2[64];
FILE file_create;

/* remove directory entry */
u32 fs_rm(u8 *filename) {
    u32 clus;
    u32 next_clus;
    FILE rm_file;

    if (fs_open(&rm_file, filename) == 1)
        return 1;

    /* Mark 0xE5 */
    rm_file.entry.data[0] = 0xE5;

    /* Release all allocated block */
    clus = get_start_cluster(&rm_file);

    while (clus != 0 && clus <= fat_info.total_data_clusters + 1) {
        if (get_fat_entry_value(clus, &next_clus) == 1)
            return 1;

        if (fs_modify_fat(clus, 0) == 1)
            return 1;

        clus = next_clus;
    }

    if (fs_close(&rm_file) == 1)
        return 1;

    return 0;

}



/* move directory entry */
u32 fs_mv(u8 *src, u8 *dest) {
    u32 i;
    FILE mk_dir;

    /* if src not exists */
    if (fs_open(&mk_dir, src) == 1){
        log(LOG_FAIL,"open src failed");
        return 1;
    }

    /* create dest */
    if (fs_create_with_attr(dest, mk_dir.entry.data[11]) == 1){
        log(LOG_FAIL,"create des failed");
        return 1;
    }

    /* copy directory entry */
    for (i = 0; i < 32; i++)
        mk_dir_buf[i] = mk_dir.entry.data[i];

    /* new path */
    for (i = 0; i < 11; i++)
        mk_dir_buf[i] = mk_dir.entry.data[i];

    if (fs_open(&file_create, dest) == 1){
        log(LOG_FAIL,"open dest failed");
        return 1;
    }
    /* copy directory entry to dest */
    for (i = 11; i < 32; i++)
        file_create.entry.data[i] = mk_dir_buf[i];

    if (fs_close(&file_create) == 1)
        return 1;

    /* mark src directory entry 0xE5 */
    mk_dir.entry.data[0] = 0xE5;

    if (fs_close(&mk_dir) == 1)
        return 1;

    return 0;
}

u32 mv(u8* parm)
{
    u32 i,j;
    u8 parm1[256], parm2[256];
    for(i = 0;parm[i]!= ' ';i++)
        parm1[i] = parm[i];
    parm1[i] = 0;
    for(;parm[i]==' ';i++)
        ;
    for(j = 0;parm[i]!= 0;i++,j++)
        parm2[j] = parm[i];
    return fs_mv(parm1, parm2);
}
/* mkdir, create a new file and write . and .. */
u32 fs_mkdir(u8 *filename) {
    u32 i;
    FILE mk_dir;
    FILE file_creat;

    if (fs_create_with_attr(filename, 0x10) == 1)
        goto fs_mkdir_err;

    if (fs_open(&mk_dir, filename) == 1)
        goto fs_mkdir_err;

    mk_dir_buf[0] = '.';
    for (i = 1; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x10;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    /*if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;

    fs_lseek(&mk_dir, 0);*/


    mk_dir_buf[20] = mk_dir.entry.data[20];
    mk_dir_buf[21] = mk_dir.entry.data[21];
    mk_dir_buf[26] = mk_dir.entry.data[26];
    mk_dir_buf[27] = mk_dir.entry.data[27];

    /*if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;*/
    kernel_memcpy(mk_dir_buf_2,mk_dir_buf,32);
    mk_dir_buf[0] = '.';
    mk_dir_buf[1] = '.';

    for (i = 2; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x10;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    set_u16(mk_dir_buf + 20, (file_creat.dir_entry_pos >> 16) & 0xFFFF);
    set_u16(mk_dir_buf + 26, file_creat.dir_entry_pos & 0xFFFF);

    /*if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;*/
    kernel_memcpy(mk_dir_buf_2+32,mk_dir_buf,32);

    if (fs_write(&mk_dir, mk_dir_buf_2, 64) == 0xffffffff)
        goto fs_mkdir_err; 

    for (i = 28; i < 32; i++)
        mk_dir.entry.data[i] = 0;

    if (fs_close(&mk_dir) == 1)
        goto fs_mkdir_err;

    return 0;
fs_mkdir_err:
    return 1;
}

u32 fs_cat(u8 *path) {
    u8 filename[12];
    FILE cat_file;

    /* Open */
    if (0 != fs_open(&cat_file, path)) {
        log(LOG_FAIL, "File %s open failed", path);
        return 1;
    }
    //log(LOG_OK, "Open file %s successed",path);
    /* Read */
    u32 file_size = get_entry_filesize(cat_file.entry.data);
   // log(LOG_OK,"filesize = %d",file_size);
    u8 *buf = (u8 *)kmalloc(file_size + 1);
    fs_read(&cat_file, buf, file_size);
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);
    fs_close(&cat_file);
    kfree(buf);
    return 0;
}