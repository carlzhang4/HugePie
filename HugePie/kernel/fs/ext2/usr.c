#include <driver/vga.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include "ext2.h"


u32 ext_cat(u8 *path) {
    u8 filename[12];
    EFILE cat_file;

    /* Open */
    if (0 != ext_open(&cat_file, path)) {
        log(LOG_FAIL, "File %s open failed", path);
        return 1;
    }
    /* Read */
    u32 file_size = cat_file.inode.attr.size;
    u8 *buf = (u8 *)kmalloc(file_size + 1);
    ext_read(&cat_file, buf, file_size);
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);
    ext_close(&cat_file);
    kfree(buf);
    return 0;
}