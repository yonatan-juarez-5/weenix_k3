/******************************************************************************/
/* Important Fall 2024 CSCI 402 usage information:                            */
/*                                                                            */
/* This fils is part of CSCI 402 kernel programming assignments at USC.       */
/*         53616c7465645f5fd1e93dbf35cbffa3aef28f8c01d8cf2ffc51ef62b26a       */
/*         f9bda5a68e5ed8c972b17bab0f42e24b19daa7bd408305b1f7bd6c7208c1       */
/*         0e36230e913039b3046dd5fd0ba706a624d33dbaa4d6aab02c82fe09f561       */
/*         01b0fd977b0051f0b0ce0c69f7db857b1b5e007be2db6d42894bf93de848       */
/*         806d9152bd5715e9                                                   */
/* Please understand that you are NOT permitted to distribute or publically   */
/*         display a copy of this file (or ANY PART of it) for any reason.    */
/* If anyone (including your prospective employer) asks you to post the code, */
/*         you must inform them that you do NOT have permissions to do so.    */
/* You are also NOT permitted to remove or alter this comment block.          */
/* If this comment block is removed or altered in a submitted file, 20 points */
/*         will be deducted.                                                  */
/******************************************************************************/

#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
        uint32_t res;
 
        if ((flags & MAP_FIXED))
        {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                if (USER_MEM_LOW > (unsigned int) addr || (USER_MEM_HIGH < (unsigned int) (addr) + len))
                {
                        dbg(DBG_PRINT, "(GRADING3D)\n");
                        return -EINVAL;
                }
                res = ADDR_TO_PN(addr);
        } else {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                res = 0;
        }
 
        if (!PAGE_ALIGNED(off) || len <= 0 || len > (USER_MEM_HIGH - USER_MEM_LOW))
        {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL; 
        }
 
        if (!((flags & MAP_SHARED) ^ (flags & MAP_PRIVATE)))
        {
                dbg(DBG_PRINT, "(GRADING3D)\n");
                return -EINVAL;
        }

        file_t *f = NULL;
        vmarea_t *vm = NULL;
        int result = 0;

        if ((flags & MAP_ANON) != MAP_ANON)
        {
                if (fd < 0 || fd > NFILES )
                {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        return -EBADF;
                }
 
                f = fget(fd); 
                if (f == NULL)
                {
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        return -EBADF;
                }

                if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && (f->f_mode == FMODE_READ))
                {       
                        dbg(DBG_PRINT, "(GRADING3B)\n");
                        fput(f);
                        return -EINVAL;
                }

                result = vmmap_map(curproc->p_vmmap, f->f_vnode, res, (len - 1) / PAGE_SIZE + 1, 
                prot, flags, off, VMMAP_DIR_HILO, &vm);
                        
                dbg(DBG_PRINT, "(GRADING3B)\n");
                fput(f);
                
        }
        else{
                dbg(DBG_PRINT, "(GRADING3B)\n");
                result = vmmap_map(curproc->p_vmmap, 0, res,  (len - 1) / PAGE_SIZE + 1, 
                prot, flags, off, VMMAP_DIR_HILO, &vm);
        }

        if (result < 0)
        {
                dbg(DBG_PRINT, "(GRADING3B)\n");
                return result;
        }

        *ret = PN_TO_ADDR(vm->vma_start);
     
        pt_unmap_range(curproc->p_pagedir, 
                (uintptr_t) PN_TO_ADDR(vm->vma_start),
               (uintptr_t) PN_TO_ADDR(vm->vma_start) + (uintptr_t) PAGE_ALIGN_UP(len)
                        );
    
        tlb_flush_range((uintptr_t) PN_TO_ADDR(vm->vma_start), (uint32_t) PAGE_ALIGN_UP(len) / PAGE_SIZE);
        /* page table must be valid after a memory segment is mapped into the address space */
        KASSERT(NULL != curproc->p_pagedir); 
        dbg(DBG_PRINT, "(GRADING3A 2.a)");
        return result;
        // NOT_YET_IMPLEMENTED("VM: do_mmap");
        // return -1;
}


/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
        unsigned int addr_val = ADDR_TO_PN(addr);
        if (USER_MEM_LOW > (unsigned int)addr){

        }
        if( (len>(USER_MEM_HIGH-USER_MEM_LOW)) || ((unsigned int)addr + len) > USER_MEM_HIGH || ((unsigned int)addr < USER_MEM_LOW) || (len <= 0) ){
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return -EINVAL; /* Invalid argument */
	}

        int res = vmmap_remove(curproc->p_vmmap, addr_val, (len-1)/PAGE_SIZE + 1);
        if (res < 0){
                dbg(DBG_PRINT, "(GRADING3A)\n");
                return res;
        }

        tlb_flush_all();
        dbg(DBG_PRINT, "(GRADING3A)\n");
        return 0;
        // NOT_YET_IMPLEMENTED("VM: do_munmap");
        // return -1;
}

