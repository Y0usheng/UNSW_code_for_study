#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */

// global variables
int cur_of_index = 0;

// Add into the file descriptor table
int add_fd_table()
{
    int fd = -1;
    // begin from 3 because the table 0,1,2 already have been used
    for (int i = 3; i < OPEN_MAX; i++)
    {
        if (curproc->filetable[i] == NULL)
        {
            fd = i;
            break;
        }
    }

    return fd;
}

// open() function
int sys_open(userptr_t filename, int flag, mode_t mode, int *ret_val)
{

    struct vnode *v;
    struct file *f;
    size_t act_len;

    // check if the flag is vaild
    int m = flag & O_ACCMODE;
    if (m != O_RDONLY && m != O_WRONLY && m != O_RDWR)
    {
        // kprintf("Failed 0");
        *ret_val = -1;
        return EINVAL;
    }

    // check if the filename is null
    if (filename == NULL)
    {
        *ret_val = -1;
        return EFAULT;
    }

    char fname[NAME_MAX];

    // copies a filename into kernel-space and check if the copy success; also if the filename is null
    int r = copyinstr(filename, fname, NAME_MAX, &act_len);
    if (r)
    {
        // kprintf("Failed 1");
        *ret_val = -1;
        return r;
    }

    // open file and store virtual node in file struct
    int re = vfs_open(fname, flag, mode, &v);
    if (re)
    {
        // kprintf("Failed 2");
        *ret_val = -1;
        return re;
    }

    // add the file node to the file descriptor table and return the vaild index of the fd.
    int fd = add_fd_table();
    if (fd == -1)
    {
        // kprintf("Failed 4");
        *ret_val = -1;
        vfs_close(v); // vfs_close for add fail because file table is full
        return EMFILE;
    }
    else
    {
        *ret_val = fd;
    }

    // create the new file
    f = kmalloc(sizeof(struct file));
    if (f == NULL)
    {
        // kprintf("Failed 5");
        *ret_val = -1;
        vfs_close(v); // vfs_close for NULL file
        return ENOMEM;
    }

    f->file_vnode = v;
    f->file_offset = 0;
    f->file_mode = m; // set mode can be only read or write;
    f->ref_count = 1;

    // add the file node to the open file table and check if full
    if (cur_of_index > OPEN_MAX)
    {
        // kprintf("Failed 3");
        *ret_val = -1;
        vfs_close(v);  // vfs_close for add fail because file table is full
        return ENFILE; // open file overload !
    }

    opentable[cur_of_index] = f;
    cur_of_index++;

    // Inserting the file node into file table
    curproc->filetable[fd] = f;

    return 0;
}

// close() function
int sys_close(int fd, int *ret_val)
{
    struct file *f;

    // check if the fd is valid
    if (fd < 0 || fd >= OPEN_MAX)
    {
        *ret_val = -1;
        return EBADF;
    }
    if (curproc->filetable[fd] == NULL)
    {
        *ret_val = -1;
        return EBADF;
    }

    f = curproc->filetable[fd];
    f->ref_count--;

    if (f->ref_count == 0)
    {
        vfs_close(f->file_vnode);
        kfree(f);
        // clean both file tables
        opentable[cur_of_index] = NULL;
        curproc->filetable[fd] = NULL;
        cur_of_index--;
    }

    *ret_val = 0;
    return 0;
}

// read() function
int sys_read(int fd, void *buf, size_t size, int *ret_val)
{

    struct file *f = curproc->filetable[fd];

    // check if the fd is valid
    if (fd < 0 || fd >= OPEN_MAX)
    {
        *ret_val = -1;
        return EBADF;
    }
    if (f == NULL)
    {
        *ret_val = -1;
        return EBADF;
    }

    // check if the buf is valid
    if (buf == NULL)
    {
        *ret_val = -1;
        return EFAULT;
    }

    // check if the buffer len is valid
    if (size <= 0)
    {
        *ret_val = -1;
        return EINVAL;
    }

    // check if the mode can be read
    if (f->file_mode != O_RDONLY && f->file_mode != O_RDWR)
    {
        *ret_val = -1;
        return EBADF;
    }

    // All checks pass, beginning read the file, first we need to set the variables
    struct iovec iov;
    struct uio u;

    // reading data from a user buffer to the file
    uio_uinit(&iov, &u, buf, size, f->file_offset, UIO_READ);

    int result = VOP_READ(f->file_vnode, &u);
    if (result)
    {
        *ret_val = -1;
        return result;
    }

    f->file_offset = u.uio_offset;
    size_t rem_size = size - u.uio_resid;

    *ret_val = rem_size;
    return 0;
}

// write() function
int sys_write(int fd, const void *buf, size_t size, int *ret_val)
{

    struct file *f = curproc->filetable[fd];

    // check if the fd is vaild
    if (fd < 0 || fd >= OPEN_MAX)
    {
        *ret_val = -1;
        return EBADF;
    }
    if (f == NULL)
    {
        *ret_val = -1;
        return EBADF;
    }

    // check if the buf is valid
    if (buf == NULL)
    {
        *ret_val = -1;
        return EFAULT;
    }

    // check if the mode is write mode
    if (f->file_mode != O_WRONLY && f->file_mode != O_RDWR)
    {
        *ret_val = -1;
        return EBADF;
    }

    // check if the buffer len is valid
    if (size <= 0)
    {
        *ret_val = -1;
        return EINVAL;
    }

    // All checks pass, beginning write the file, first we need to set the variables
    struct iovec iov;
    struct uio u;

    // Attempt to allocate kernel memory
    char *copy_buf = (char *)kmalloc(size);
    if (copy_buf == NULL)
    {
        *ret_val = -1;
        return ENOMEM;
    }

    // Attempt to copy string from user space to kernel space
    // here using the copyin because the copyinstr() has the using limitation
    // copyinstr() only is using for null-terminated strings
    // but actually i do not know why i am using copyinstr() my code will be halt
    int r = copyin((const_userptr_t)buf, copy_buf, size);
    if (r)
    {
        kfree(copy_buf);
        *ret_val = -1;
        return r;
    }

    // writing data from a kernel buffer to the file
    uio_kinit(&iov, &u, copy_buf, size, f->file_offset, UIO_WRITE);

    int result = VOP_WRITE(f->file_vnode, &u);
    if (result)
    {
        *ret_val = -1;
        return result;
    }

    f->file_offset = u.uio_offset;
    size_t rem_size = size - u.uio_resid;

    *ret_val = rem_size;

    kfree(copy_buf);
    return 0;
}

// lseek() function
int sys_lseek(int fd, off_t pos, int whence, off_t *ret_val)
{
    struct file *f = curproc->filetable[fd];

    if (fd < 0 || fd >= OPEN_MAX)
    {
        *ret_val = -1;
        return EBADF;
    }

    if (f == NULL)
    {
        *ret_val = -1;
        return EBADF;
    }

    // Check if is seekable
    if (!VOP_ISSEEKABLE(f->file_vnode))
    {
        *ret_val = -1;
        return ESPIPE;
    }

    off_t new_offset;
    struct stat file_stat;

    switch (whence)
    {
    case SEEK_SET:
        if (pos < 0)
        {
            *ret_val = -1;
            return EINVAL;
        }
        new_offset = pos;
        break;
    case SEEK_CUR:
        new_offset = f->file_offset + pos;
        if (new_offset < 0)
        {
            *ret_val = -1;
            return EINVAL;
        }
        break;
    case SEEK_END:
        if (VOP_STAT(f->file_vnode, &file_stat))
        {
            *ret_val = -1;
            return -1;
        }
        new_offset = file_stat.st_size + pos;
        if (new_offset < 0)
        {
            *ret_val = -1;
            return EINVAL;
        }
        break;

    default:
        *ret_val = -1;
        return EINVAL; // Set errno to
    }

    // update file offset
    f->file_offset = new_offset;
    *ret_val = new_offset;
    return 0;
}

// dup2() function
int sys_dup2(int oldfd, int newfd, int *ret_val)
{
    // check if both of oldfd and newfd are valid
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX)
    {
        *ret_val = -1;
        return EBADF;
    }

    // check if the oldfile is in filetable
    if (curproc->filetable[oldfd] == NULL)
    {
        *ret_val = -1;
        return EBADF;
    }

    // when oldfd = newfd, nothing change
    if (oldfd == newfd)
    {
        *ret_val = oldfd;
        return 0;
    }

    // check if the newfile is in filetable
    if (curproc->filetable[newfd] != NULL)
    {
        int ret;
        int result = sys_close(newfd, &ret);
        if (result)
        {
            *ret_val = -1;
            return EBADF;
        }
    }

    // All checks done, copy begin
    curproc->filetable[newfd] = curproc->filetable[oldfd];
    curproc->filetable[newfd]->ref_count++;

    *ret_val = newfd;

    return 0;
}

// initial and set the file descriptors 0 (stdin), 1 (stdout) and 2 (stderr)
int initial_filetable()
{
    struct file *stdin, *stdout, *stderr;
    stdin = kmalloc(sizeof(struct file));
    stdout = kmalloc(sizeof(struct file));
    stderr = kmalloc(sizeof(struct file));
    if (stdin == NULL || stdout == NULL || stderr == NULL)
        return ENOMEM;

    int ret;

    struct vnode *vn_stdin, *vn_stdout, *vn_stderr;
    char con0[5] = "con:";
    char con1[5] = "con:";
    char con2[5] = "con:";

    // 0 (stdin)
    curproc->filetable[0] = stdin;
    curproc->filetable[0]->ref_count = 1;
    curproc->filetable[0]->file_mode = O_RDONLY;
    int r = vfs_open(con0, O_RDONLY, 0, &vn_stdin);
    if (r) // error check
        return r;
    curproc->filetable[0]->file_vnode = vn_stdin;
    curproc->filetable[0]->file_offset = 0;

    // 1 (stdout)
    curproc->filetable[1] = stdout;
    curproc->filetable[1]->ref_count = 1;
    curproc->filetable[1]->file_mode = O_WRONLY;
    int re = vfs_open(con1, O_WRONLY, 0, &vn_stdout);
    if (re) // error check
    {
        sys_close(0, &ret);
        return re;
    }
    curproc->filetable[1]->file_vnode = vn_stdout;
    curproc->filetable[1]->file_offset = 0;

    // 2 (stderr)
    curproc->filetable[2] = stderr;
    curproc->filetable[2]->ref_count = 1;
    curproc->filetable[2]->file_mode = O_WRONLY;
    int res = vfs_open(con2, O_WRONLY, 0, &vn_stderr);
    if (res) // error check
    {
        sys_close(0, &ret);
        sys_close(1, &ret);
        return res;
    }
    curproc->filetable[2]->file_vnode = vn_stderr;
    curproc->filetable[2]->file_offset = 0;

    return 0;
}
