#include "ps.h"
#include <driver/ps2.h>
#include <driver/sd.h>
#include <driver/vga.h>
#include <zjunix/bootmm.h>
#include <zjunix/buddy.h>
#include <zjunix/fs/fat.h>
#include <zjunix/fs/ext2.h>
#include <zjunix/slab.h>
#include <zjunix/time.h>
#include <zjunix/utils.h>
#include <zjunix/pc.h>
#include <zjunix/mail.h>
#include <zjunix/log.h>
#include "../usr/ls.h"
#include "exec.h"
#include "myvi.h"

char ps_buffer[64];
int ps_buffer_index;

void test_syscall4() {
    asm volatile(
        "li $a0, 0x00ff\n\t"
        "li $v0, 4\n\t"
        "syscall\n\t"
        "nop\n\t");
}

void test_proc() {
    unsigned int timestamp;
    unsigned int currTime;
    unsigned int data;
    asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(timestamp));
    data = timestamp & 0xff;
    while (1) {
        asm volatile("mfc0 %0, $9, 6\n\t" : "=r"(currTime));
        if (currTime - timestamp > 100000000) {
            timestamp += 100000000;
            *((unsigned int *)0xbfc09018) = data;
        }
    }
}

int proc_demo_create(int level, int fixed) {
    // int asid = pc_peek();
    // if (asid < 0) {
    //     kernel_puts("Failed to allocate pid.\n", 0xfff, 0);
    //     return 1;
    // }
    unsigned int init_gp;
    asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
    pc_create(test_proc, "test", level, fixed);
    return 0;
}

void test_read()
{
    if(pc_register_mailbox()!= 0)
        while(1);
    char message[MAIL_LENGTH];
    int src;
    while(1)
    {
        if(pc_read_mail(&src, message) == 0)
        {
            kernel_printf("\n Process %d: Receive from process %d : %s\n", get_curr_pcb()->pid, src, message);
        }
    }
}

void test_send() 
{
    // log(LOG_START, "Begin create read\n");
    int pid = pc_create(test_read, "Read test", 0, 0);
    // log(LOG_END, "END create read\n");
    if(pid < 0)
        while(1);
    char message[100];
    kernel_memcpy(message, "Hello, I'm process ", 20);
    // message[0] = 'H'; message[1] = 
    int i = 19;
    int current = get_curr_pcb()->pid;
    int temp = 100;
    // log(LOG_START, "Begin send\n");
    while(temp != 0)
    {
        message[i++] = current/temp + '0';
        current = current%temp;
        temp = temp/10;
    }
    message[i] = 0;

    while(pc_send_mail(pid, message) != 0);
    // log(LOG_END, "END send\n");
    while(1);
}

int test_communication()
{
    int pid = pc_create(test_send, "Send test", 0, 0);

    if(pid < 0)
        return -1;
    else
        return 0;
}

void ps() {
    kernel_printf("Press any key to enter shell.\n");
    kernel_getchar();
    char c;
    ps_buffer_index = 0;
    ps_buffer[0] = 0;
    kernel_clear_screen(31);
    kernel_puts("PowerShell\n", 0xfff, 0);
    kernel_puts("HugePie>", 0xfff, 0);
    while (1) {
        c = kernel_getchar();
        if (c == '\n') {
            ps_buffer[ps_buffer_index] = 0;
            if (kernel_strcmp(ps_buffer, "exit") == 0) {
                ps_buffer_index = 0;
                ps_buffer[0] = 0;
                kernel_printf("\nPowerShell exit.\n");
            } else
                parse_cmd();
            ps_buffer_index = 0;
            kernel_puts("HugePie>", 0xfff, 0);
        } else if (c == 0x08) {
            if (ps_buffer_index) {
                ps_buffer_index--;
                kernel_putchar_at(' ', 0xfff, 0, cursor_row, cursor_col - 1);
                cursor_col--;
                kernel_set_cursor();
            }
        } else {
            if (ps_buffer_index < 63) {
                ps_buffer[ps_buffer_index++] = c;
                kernel_putchar(c, 0xfff, 0);
            }
        }
    }
}

void parse_cmd() {
    unsigned int result = 0;
    char dir[32];
    char c;
    kernel_putchar('\n', 0, 0);
    char sd_buffer[8192];
    int i = 0;
    char *param;
    for (i = 0; i < 63; i++) {
        if (ps_buffer[i] == ' ') {
            ps_buffer[i] = 0;
            break;
        }
    }
    if (i == 63)
        param = ps_buffer;
    else
        param = ps_buffer + i + 1;
    if (ps_buffer[0] == 0) {
        return;
    } else if (kernel_strcmp(ps_buffer, "clear") == 0) {
        kernel_clear_screen(31);
    } else if (kernel_strcmp(ps_buffer, "echo") == 0) {
        kernel_printf("%s\n", param);
    } else if (kernel_strcmp(ps_buffer, "gettime") == 0) {
        char buf[10];
        get_time(buf, sizeof(buf));
        kernel_printf("%s\n", buf);
    } else if (kernel_strcmp(ps_buffer, "syscall4") == 0) {
        test_syscall4();
    } else if (kernel_strcmp(ps_buffer, "sdwi") == 0) {
        for (i = 0; i < 512; i++)
            sd_buffer[i] = i;
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwi\n", 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "sdr") == 0) {
        sd_read_block(sd_buffer, 7, 1);
        for (i = 0; i < 512; i++) {
            kernel_printf("%d ", sd_buffer[i]);
        }
        kernel_putchar('\n', 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "sdwz") == 0) {
        for (i = 0; i < 512; i++) {
            sd_buffer[i] = 0;
        }
        sd_write_block(sd_buffer, 7, 1);
        kernel_puts("sdwz\n", 0xfff, 0);
    } else if (kernel_strcmp(ps_buffer, "mminfo") == 0) {
        bootmap_info("bootmm");
        buddy_info();
    } else if (kernel_strcmp(ps_buffer, "mmtest") == 0) {
        kernel_printf("kmalloc : %x, size = 1KB\n", kmalloc(1024));
    } else if (kernel_strcmp(ps_buffer, "ps") == 0) {
        result = print_proc();
        kernel_printf("ps return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "kill") == 0) {
        int pid = param[0] - '0';
        kernel_printf("Killing process %d\n", pid);
        result = pc_kill(pid);
        kernel_printf("kill return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "time") == 0) {
        unsigned int init_gp;
        asm volatile("la %0, _gp\n\t" : "=r"(init_gp));
        pc_create(system_time_proc, "time", PRI_QUEUE_NUM-1, 1);
    } else if (kernel_strcmp(ps_buffer, "showproc") == 0){
        if(param[0]>='0' && param[0]<='9')
        {
            int result = print_certain_proc(param[0]-'0');
            kernel_printf("Show return with %d\n", result);
        }
        else
        {
            kernel_printf("Invalid parameters\n");
        }
    }
      else if (kernel_strcmp(ps_buffer, "proc") == 0) {
        int level, fixed;
        if(param[0]>='0' && param[0]<='9')
            level = param[0] - '0';
        else
        {
            kernel_printf("Invalid parameters\n");
            return;
        }
        if(param[1]==' '&&(param[2]>='0'||param[2]<='9'))
        {
            fixed = param[2] - '0';
            result = proc_demo_create(level, fixed);
            kernel_printf("proc return with %d\n", result);
        }
        else
        {
            kernel_printf("Invalid parameters\n");
            return;
        }
    } else if(kernel_strcmp(ps_buffer, "testcomm") == 0)
        {
            kernel_printf("testcomm return with %d\n", test_communication());
            return;
        }
      else if(kernel_strcmp(ps_buffer, "sendm") == 0)
        {
            if(param[0]>='0' && param[0]<='9' && param[1] == ' ')
            {
                int pid = param[0] - '0';
                param += 2;
                char message[MAIL_LENGTH];
                int length = 64-(param-ps_buffer);
                kernel_memcpy(message, param, length);

                kernel_printf("sendm return with %d\n", pc_send_mail(pid, message));
            }
            else
                kernel_printf("Invalid parameters\n");
            return;
        }
    else if (kernel_strcmp(ps_buffer, "cat") == 0) {
        result = fs_cat(param);
        kernel_printf("cat return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "ecat") == 0) {
        result = ext_cat(param);
        kernel_printf("ecat return with %d\n", result);
    }else if (kernel_strcmp(ps_buffer, "ls") == 0) {
        result = ls(param);
        kernel_printf("ls return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "vi") == 0) {
        result = myvi(param);
        kernel_printf("vi return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "mkdir") == 0) {
        result = fs_mkdir(param);
        kernel_printf("mkdir return with %d\n", result);
    }  else if (kernel_strcmp(ps_buffer, "rm") == 0) {
        result = fs_rm(param);
        kernel_printf("rm return with %d\n", result);
    }  else if (kernel_strcmp(ps_buffer, "mv") == 0) {
        result = mv(param);
        kernel_printf("mv return with %d\n", result);
    }else if (kernel_strcmp(ps_buffer, "exec") == 0) {
        result = exec(param);
        kernel_printf("exec return with %d\n", result);
    } else if (kernel_strcmp(ps_buffer, "mp") == 0) {//malloc pages
        int size;
        size = param[0] - '0';
        kernel_printf("apply malloc %d pages\n", size);
        result = (int)kmalloc(size*4096);
        kernel_printf("malloc return with %x\n", result);
        buddy_info();
    } else if (kernel_strcmp(ps_buffer, "fp") == 0) {//free pages
        int addr = 0;
        void * obj;
        int num[8];
        int i=0;
        while(i<8){
            if(param[i]>'a'){
                addr = addr * 16 + (param[i] - 'a' + 10);
            }
            else{
                addr = addr * 16 + (param[i] - '0');
            }
            i++;
        }
        
        kernel_printf("free page addr:%x pages\n", addr);
        obj = (void*)addr;
        kfree(obj);
        buddy_info();
    }
    else {
        kernel_puts(ps_buffer, 0xfff, 0);
        kernel_puts(": command not found\n", 0xfff, 0);
    }
}
