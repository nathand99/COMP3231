#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

#include <proc.h>
/**
 * Create a pointer to a new file pointer
 * 
 * Initialising the pos to 0 because it is
 * always read from the start of file
 * 
 * This funciton has to be called in a protected way
 * as in caller has to guarantee that flag is valid
*/
FP *newFP(int flags) {
    FP *fp = kmalloc(sizeof *fp);
    KASSERT(fp != NULL);
    fp->pos = 0;
    fp->pos_mutex = sem_create("pos_mutex", 1);
    //setting the flags
    flags = flags & O_ACCMODE;
    fp->read = 1;
    fp->write = 1;
    if (flags == O_RDONLY) {
        fp->write = 0;
    } else if(flags == O_WRONLY) {
        fp->read = 0;
    }

    return fp;
}

/* Free the entire FP structure */
void freeFP(FP *fp) {
    KASSERT(fp != NULL);
    sem_destroy(fp->pos_mutex);
    kfree(fp);
}

/**
 * Create a new open file pointer
 * 
 * ref_count is initialised to 1
 * as there is only 1 pointer refer to
 * it at first, as more pointer refers
 * to it (from dup2() or fork()) it will go up
 * 
 * There should be only one process accessing the 
 * ref_count at a time 
*/
OP *newOP(FP *fp, struct vnode *vnode) {
    OP *op = kmalloc(sizeof *op);
    KASSERT(op != NULL);
    op->count_mutex = sem_create("countMutex", 1);
    op->ref_count = 1;
    op->fp = fp;
    op->vnode = vnode;
    return op;
}

/* Free the entire OP structure */
void freeOP(OP *op) {
    KASSERT(op != NULL);
    sem_destroy(op->count_mutex);
    freeFP(op->fp);
    kfree(op);
}

/* Increase the ref_count safely */
void inc_ref_count(OP *op) {
    KASSERT(op != NULL);
    P(op->count_mutex);
    op->ref_count++;
    V(op->count_mutex);
}

/* Decrease the ref_count safely and return the ref_count */
int dec_ref_count(OP *op) {
    KASSERT(op != NULL);
    P(op->count_mutex);
    op->ref_count--;
    int x = op->ref_count;
    V(op->count_mutex);
    return x;
}

/** 
 * Open 
 * 
 * Open or create a file. The Openning part is handle by the 
 * VFS layer so we simply store relevant data and control access.
 * 
 * Returns the negate of errno on error
 * Returns fd on success   
 */
int sys_open(const char *filename, int flags, mode_t mode) {

    int result;

    /* Handling the copy */
    char path[NAME_MAX];
    size_t size;
    result = copyinstr((userptr_t)filename, path, NAME_MAX, &size);
    if (result) {
        /* Negate every error */
        return -result;
    }
    
    /* Pass the arguments to vfs_open */
    struct vnode *vnode;
    result = vfs_open(path, flags, mode, &vnode);
    if(result) {
        return -result;
    }

    /* Open file succcesful */
    FP *fp = newFP(flags);
    OP *op = newOP(fp, vnode);

    /* Finds the lowest possible index in the process table */
    int fd = findLowest();
    /* Return -EMFILE to indictate process table is full */
    if (fd == OPEN_MAX) return -EMFILE;

    /* Store the op into the process table */
    curproc->openFileTable[fd] = op;

    /* Return the file descriptor */
    return fd;
}

/**
 * Helper function 
 * Checks if the fd is valid
*/
static int checkFD(int fd) {
    struct proc *proc = curproc;
    KASSERT(proc != NULL);
    if(fd < 0 || proc->openFileTable[fd] == NULL || fd >= OPEN_MAX) return 1;
    return 0;
}

/**
 * Close 
 * 
 * Close a file safely
 * 
 * Returns errno on error
 * Returns 1 on success 
*/
int sys_close(int fd) {

    /* check if the fd is valid */
    if(checkFD(fd)) return EBADF;
    
    /* Setup */
    struct proc *proc = curproc;
	KASSERT(proc != NULL);
    OP *op = proc->openFileTable[fd];
    struct vnode* vnode = op->vnode; 
    
    /* Decrease both ref_count */
    int ref_count = dec_ref_count(op);
    vfs_close(vnode);

    /* Free the pointer */
    if (ref_count == 0) freeOP(op);
    
    /* Make the array[fd] NULL for a new pointer */
    proc->openFileTable[fd] = NULL;
    /* Set the new lowest index if applicable */
    if(proc->lowestIndex>fd) proc->lowestIndex = fd;
    return 0;
}

/** 
 * Read 
 * 
 * Read a perviously opened file safely. 
 * 
 * Returns the negate of errno on error
 * Returns number of bytes read on success   
*/
ssize_t sys_read(int fd, void *buf, size_t buflen) {
    /* Check if the fd is valid */
    if(checkFD(fd)) return -EBADF;
    
    /* Setup */
    struct proc *proc = curproc;
	KASSERT(proc != NULL);
    OP *op = proc->openFileTable[fd];
    FP *fp = op->fp;

    /* Check if it is opened for read */
    if(fp->read == 0) return -EBADF;

    /* Critial region for read operation */
    struct semaphore *pos_mutex = fp->pos_mutex; 
    P(pos_mutex);

    /* Setup */
    struct iovec iov;
    struct uio uio;
    uio_uinit(&iov, &uio, (userptr_t)buf, buflen, fp->pos, UIO_READ);
    
    /* Read the file */
    struct vnode* vnode = op->vnode; 
    int result = VOP_READ(vnode, &uio);
    if(result) {
        V(pos_mutex);
        return -result;
    }

    /* Get the count of bytes read */
    size_t remain = getResid(&uio);
    size_t bytes_read = buflen - remain;

    /* Advance the fp position */
    fp->pos += bytes_read;

    /* End Critial region */
    V(pos_mutex);
    return bytes_read;
}

/** 
 * Write 
 * 
 * Write to a perviously opened file safely. 
 * 
 * Returns the negate of errno on error
 * Returns number of bytes written on success   
*/
ssize_t sys_write(int fd, const void *buf, size_t nbytes) {
    /* Check if the fd is valid */
    if(checkFD(fd)) return -EBADF;

    /* Setup */
    struct proc *proc = curproc;
	KASSERT(proc != NULL);
    OP *op = proc->openFileTable[fd];
    FP *fp = op->fp;

    /* Check if it is opened for write */
    if(fp->write == 0) return -EBADF;

    /* Critial region for write operation */
    struct semaphore *pos_mutex = fp->pos_mutex; 
    P(pos_mutex);

    /* Setup */
    struct iovec iov;
    struct uio uio;
    uio_uinit(&iov, &uio, (userptr_t)buf, nbytes, fp->pos, UIO_WRITE);
    
    /* Write to the file */
    struct vnode* vnode = op->vnode; 
    int result = VOP_WRITE(vnode, &uio);  
    if(result) {
        V(pos_mutex);
        return -result;
    }

    /* Get the count of bytes written */
    size_t remain = getResid(&uio);
    size_t bytes_written = nbytes - remain;

    /* Advance the fp position */
    fp->pos += bytes_written;

    /* End Critial region */
    V(pos_mutex);
    return bytes_written;
}

/**
 * lseek
 * 
 * Seek a file position. 
 * Advance the file pointer to the position.
 * 
 * Returns the negate of errno on error
 * Returns new position on success   
*/
off_t sys_lseek(int fd, off_t pos, int whence) {

    /* Check if the fd is valid */
    if(checkFD(fd)) return -EBADF;

    /* Check if the pos is valid */
    if(pos < 0) return -EINVAL;

    /* Setup */
    struct proc *proc = curproc;
	KASSERT(proc != NULL);
    OP *op = proc->openFileTable[fd];
    FP *fp = op->fp;
    struct vnode* vnode = op->vnode; 

    /* Check if fd supports seeking */
    if(!VOP_ISSEEKABLE(vnode)) return -ESPIPE;

    /* Setup */
    struct stat stat;
    int result = VOP_STAT(vnode, &stat); 
    if(result) return -result;

    struct stat *statpt = &stat;
    off_t eof = statpt->st_size;  


    /* Main Logic */
    struct semaphore *pos_mutex = fp->pos_mutex; 
    switch (whence) {
        /* Set the fp position to pos */
        case SEEK_SET:
            P(pos_mutex);
            fp->pos = pos;
            result = fp->pos;
            V(pos_mutex);
            break;
        
        /* Advance the fp position by pos */
        case SEEK_CUR:
            P(pos_mutex);
            fp->pos += pos;
            result = fp->pos;
            V(pos_mutex);
            break;
        
        /* Set fp position to EOF + pos */
        case SEEK_END:
            P(pos_mutex);
            fp->pos = eof + pos;
            result = fp->pos;
            V(pos_mutex);
            break;
        
        default:
            result = -EINVAL;
    }

    return result;
}

/**
 * dup2
 * 
 * Clone file handles. Note that no new pointers are created 
 * dup2 simply points the newfd to the same struct as the oldfd
 * 
 * Returns the negate of errno on error
 * Returns fd on success   
*/
int sys_dup2(int oldfd, int newfd) {
    /* Check if the fd is valid */
    if(checkFD(oldfd) || newfd < 0 || newfd >= OPEN_MAX) return -EBADF;
    if(oldfd == newfd) return newfd;

    /* Setup */
    struct proc *proc = curproc;
    OP *newop = proc->openFileTable[newfd];
    OP *oldop = proc->openFileTable[oldfd];
    struct vnode *vnode = proc->openFileTable[oldfd]->vnode;
    /* Check if the newfd is an opened file & close it */
    if (newop != NULL) {
        KASSERT(1==2);
        int result = sys_close(newfd);
        if (result) return -result;
    } 

    /* Increase both ref_counts */
    inc_ref_count(oldop);
    VOP_INCREF(vnode);
    
    /* Make them point to the same file */
    proc->openFileTable[newfd] = oldop;
    return newfd;
}


