/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>

/*
 * Put your function declarations and data types here ...
 */

struct file
{
    struct vnode *file_vnode;
    int file_mode;
    int ref_count;
    off_t file_offset;
} file;

struct file *opentable[OPEN_MAX];

// helper function
int add_fd_table(void);

// syscall functions
int sys_open(userptr_t filename, int flag, mode_t mode, int *ret_val);
int sys_read(int fd, void *buf, size_t size, int *ret_val);
int sys_write(int fd, const void *buf, size_t size, int *ret_val);
int sys_lseek(int fd, off_t pos, int whence, off_t *ret_val);
int sys_close(int fd, int *ret_val);
int sys_dup2(int oldfd, int newfd, int *ret_val);

// set for standard file descriptors 0 (stdin), 1 (stdout) and 2 (stderr)
int initial_filetable(void);

#endif /* _FILE_H_ */
