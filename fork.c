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

#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{
        // (let newproc be a pointer to the new child process and let newthr be the thread in newproc)
        vmarea_t *vma, *clone_vma;
        pframe_t *pf;
        mmobj_t *to_delete, *new_shadowed;

        /* the function argument must be non-NULL */
        KASSERT(regs != NULL); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        /* the parent process, which is curproc, must be non-NULL */
        KASSERT(curproc != NULL); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        /* the parent process must be in the running state and not in the zombie state */
        KASSERT(curproc->p_state == PROC_RUNNING); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        proc_t *newproc = NULL;
        newproc = proc_create("newproc");
        vmmap_t *new_vmmap = NULL;
        vmmap_clone(curproc->p_vmmap);

        new_vmmap->vmm_proc = newproc;
        newproc->p_vmmap = new_vmmap;

        list_iterate_begin(&new_vmmap->vmm_list, clone_vma, vmarea_t, vma_plink){
                vma = vmmap_lookup(curproc->p_vmmap, clone_vma->vma_start);

                if (clone_vma->vma_flags & MAP_PRIVATE){
                        vma->vma_obj->mmo_ops->ref(vma->vma_obj);
                        to_delete = shadow_create();
                        to_delete->mmo_shadowed = vma->vma_obj;
                        to_delete->mmo_un.mmo_bottom_obj = vma->vma_obj->mmo_un.mmo_bottom_obj;

                        new_shadowed = shadow_create();
                        new_shadowed->mmo_shadowed = vma->vma_obj;
                        new_shadowed->mmo_un.mmo_bottom_obj = vma->vma_obj->mmo_un.mmo_bottom_obj;


                        vma->vma_obj = to_delete;
                        clone_vma->vma_obj = new_shadowed;
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                else{
                         clone_vma->vma_obj = vma->vma_obj;
                        clone_vma->vma_obj->mmo_ops->ref(clone_vma->vma_obj);
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                dbg(DBG_PRINT, "(GRADING3A)\n");
                list_insert_tail(mmobj_bottom_vmas(vma->vma_obj), &(clone_vma->vma_olink));

        }list_iterate_end();

        kthread_t *newthr = kthread_clone(curthr);
        newthr->kt_proc = newproc;
        list_insert_tail(&newproc->p_threads, &newthr->kt_plink);


        /* new child process starts in the running state */
        KASSERT(newproc->p_state == PROC_RUNNING); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        /* new child process must have a valid page table */
        KASSERT(newproc->p_pagedir != NULL); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");
        /* thread in the new child process must have a valid kernel stack */
        KASSERT(newthr->kt_kstack != NULL); 
        dbg(DBG_PRINT, "(GRADING3A 7.a)\n");

        newthr->kt_ctx.c_eip = (uint32_t) userland_entry;
        newthr->kt_ctx.c_pdptr = newproc->p_pagedir;
        regs->r_eax = 0;
        newthr->kt_ctx.c_esp = fork_setup_stack(regs, newthr->kt_kstack);

        for(int i = 0; i < NFILES; i++){
                newproc->p_files[i] = curproc->p_files[i];
                if (newproc->p_files[i]){
                        fref(newproc->p_files[i]);
                        dbg(DBG_PRINT, "(GRADING3A 7)\n");
                }
                dbg(DBG_PRINT, "(GRADING3A 7)\n");
        }
        pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
        tlb_flush_all();

        newproc->p_start_brk = curproc->p_start_brk;
        newproc->p_brk = curproc->p_brk;
        sched_make_runnable(newthr);

        regs->r_eax = newproc->p_pid;
        dbg(DBG_PRINT, "(GRADING3A 7)\n");
        return newproc->p_pid;
        // NOT_YET_IMPLEMENTED("VM: do_fork");
        // return 0;
}
