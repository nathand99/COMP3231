/*
 * Declarations for file handle and file table management.
 * 
 * Overview of the file structure:
 * -Process
 *   - Every process has a fixed size array containing pointers to open
 *     files struct
 *   - The file descriptor(fd) would be the index of the array
 *   - The same pointer can be shared among different processes
 * 
 * Instead of having a global open file structure, a openfile struct pointer 
 * is in place.The same pointer is then shared among different processes. 
 * A ref_count is in place to track how many processes are referencing 
 * this pointer.
 *  
 * fork() needs to copy the entire open file table from parents over 
 * to child and increase the ref_count in each node so that both parents 
 * and child share the same openfile pointer 
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <synch.h>

#define OPEN_MAX __OPEN_MAX
#define NAME_MAX __NAME_MAX
/** 
 * File pointer structure 
 * 
 * newFP     - Create a pointer to a new file pointer
 * freeFP    - Free the entire structure
 */
typedef struct filePointer{
    off_t            pos;          /* Position of the file pointer */
    struct semaphore *pos_mutex;   /* Semaphore for concurrent access of file position */   
    unsigned int     read;         /* Opening flags */
    unsigned int     write;        /* Opening flags */  
} FP;

FP *newFP(int flag);
void freeFP(FP *fp);


/** 
 * Open File structure 
 * 
 * newOP           - Create a new open file pointer
 * freeOP          - Free the entire structure
 * 
 * inc_ref_count   - Increase the ref_count safely
 * dec_ref_count   - Decrease the ref_count safely and return the ref_count
*/
typedef struct openfile {
    struct semaphore *count_mutex; /* Semaphore for concurrent access of ref_count */   
    int              ref_count;    /* Count how many open file pointers refer to this struct */
    FP               *fp;          /* Pointer to a file pointer */
    struct vnode     *vnode;       /* Pointer to a vnode */
} OP;

OP *newOP(FP *fp, struct vnode *vnode);
void freeOP(OP *op);

void inc_ref_count(OP *op);
int dec_ref_count(OP *op);


/**
 * User-level File functions.
 * 
 * open     - Open or create a file 
 * close    - Close a file
 * 
 * read     - Read data from a file
 * write    - Write data to a file
 * 
 * lseek    - seek to a position in a file
 * dup2     - clone file handles
 */
int sys_open(const char *filename, int flags, mode_t mode); 
int sys_close(int fd);

ssize_t sys_read(int fd, void *buf, size_t buflen);
ssize_t sys_write(int fd, const void *buf, size_t nbytes);

off_t sys_lseek(int fd, off_t pos, int whence); //whence is where you start
int sys_dup2(int oldfd, int newfd);


#endif /* _FILE_H_ */
