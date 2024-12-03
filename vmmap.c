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

#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        slab_obj_free(vmarea_allocator, vma);
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n",
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        /*
        KASSERT(0 <= size);
        if (0 == size) {
                size++;
                buf--;
                buf[0] = '\0';
        }
        */
        return osize - size;
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{
        vmmap_t *map = (vmmap_t*)slab_obj_alloc(vmmap_allocator);
        dbg(DBG_PRINT, "(GRADING3B 1)\n");
        list_init(&map->vmm_list);
        map->vmm_proc = NULL;
        dbg(DBG_PRINT, "(GRADING3B 1)\n");

        return map;
        // NOT_YET_IMPLEMENTED("VM: vmmap_create");
        // return NULL;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{
        /* function argument must not be NULL */
        KASSERT(NULL != map);
        dbg(DBG_PRINT, "(GRADING3A 3.a)\n");

        vmarea_t *vm = NULL;
        list_iterate_begin(&map->vmm_list, vm, vmarea_t, vma_plink){
                list_remove(&vm->vma_olink);
                list_remove(&vm->vma_plink);

                vm->vma_obj->mmo_ops->put(vm->vma_obj);
                vm->vma_obj = NULL;

                vmarea_free(vm);
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }list_iterate_end();
        slab_obj_free(vmmap_allocator, map);
        dbg(DBG_PRINT, "(GRADING3A 3)\n");
        // NOT_YET_IMPLEMENTED("VM: vmmap_destroy");
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{
        /* both function arguments must not be NULL */
        KASSERT(NULL != map && NULL != newvma); 
        dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
        /* newvma must be newly create and must not be part of any existing vmmap */
        KASSERT(NULL == newvma->vma_vmmap); 
        dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
        /* newvma must not be empty */
        KASSERT(newvma->vma_start < newvma->vma_end); 
        dbg(DBG_PRINT, "(GRADING3A 3.b)\n");
        /* addresses in this memory segment must lie completely within the user space */
        KASSERT(ADDR_TO_PN(USER_MEM_LOW) <= newvma->vma_start && ADDR_TO_PN(USER_MEM_HIGH) >= newvma->vma_end);
        dbg(DBG_PRINT, "(GRADING3A 3.b)\n");

        vmarea_t *itr=NULL, *curr=NULL;

        newvma->vma_vmmap = map;
        list_iterate_begin(&map->vmm_list, itr, vmarea_t, vma_plink){
                if (itr->vma_start >= newvma->vma_start){
                        dbg(DBG_PRINT, "(GRADING3B 3)\n");
                        curr = itr;
                        list_insert_before(&curr->vma_plink, &newvma->vma_plink);
                        return;
                }
                
        }list_iterate_end();

        dbg(DBG_PRINT, "(GRADING3B 3)\n");
        list_insert_before(&map->vmm_list, &newvma->vma_plink);

        // NOT_YET_IMPLEMENTED("VM: vmmap_insert");
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{
        
        // NOT_YET_IMPLEMENTED("VM: vmmap_find_range");
        // return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{
        
        /* the first function argument must not be NULL */
        KASSERT(NULL != map); 
        dbg(DBG_PRINT, "(GRADING3A 3.c)\n");

        vmarea_t* vma = NULL;
        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink){
                if (vfn >= vma->vma_start && vfn <vma->vma_end){
                        dbg(DBG_PRINT, "(GRADING3A 3)\n");
                        return;
                }                
        }list_iterate_end();

        dbg(DBG_PRINT, "(GRADING3A 3)\n");
        // NOT_YET_IMPLEMENTED("VM: vmmap_lookup");
        return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
        vmmap_t *clone  = vmmap_create();
        vmarea_t *vma = NULL;

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink){
                dbg(DBG_PRINT, "(GRADING3A)\n");
                vmarea_t *vm = vmarea_alloc();
                vm->vma_start = vma->vma_start;
                vm->vma_end = vma->vma_end;
                
                vm->vma_prot = vma->vma_prot;     /* permissions on mapping */
                vm->vma_flags = vma->vma_flags;    /* either MAP_SHARED or MAP_PRIVATE */
                vm->vma_vmmap = clone;    /* address space that this area belongs to */
                list_link_init(&vm->vma_plink);    /* link on process vmmap maps list */
                list_link_init(&vm->vma_olink);    /* link on the list of all vm_areas
                * having the same vm_object at the
                * bottom of their chain */
                list_insert_tail(&clone->vmm_list, &vm->vma_plink);
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }
        list_iterate_end();
        dbg(DBG_PRINT, "(GRADING3A)\n");
        return clone;
        // NOT_YET_IMPLEMENTED("VM: vmmap_clone");
        // return NULL;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
        /* function arguments (except first and last) describe the new memory segment to be created and added to the address space */
        
        /* must not add a memory segment into a non-existing vmmap */
        KASSERT(NULL != map); 
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

        /* number of pages of this memory segment cannot be 0 */
        KASSERT(0 < npages); 
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
        /* must specify whether the memory segment is shared or private */
        KASSERT((MAP_SHARED & flags) || (MAP_PRIVATE & flags)); 
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
        /* if lopage is not zero, it must be a user space vpn */
        KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_LOW) <= lopage)); 
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
        /* if lopage is not zero, the specified page range must lie completely within the user space */
        KASSERT((0 == lopage) || (ADDR_TO_PN(USER_MEM_HIGH) >= (lopage + npages)));
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");
        /* the off argument must be page aligned */
        KASSERT(PAGE_ALIGNED(off)); 
        dbg(DBG_PRINT, "(GRADING3A 3.d)\n");

        vmarea_t *vma = vmarea_alloc();
        if (lopage == 0){
                int res = vmmap_find_range(map, npages, dir);
                if (res == -1){
                        dbg(DBG_PRINT, "(GRADING3A 3)\n");
                        return res;
                }
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
                lopage = res;
        }
        else{
                if (!vmmap_is_range_empty(map, lopage, npages)){
                        dbg(DBG_PRINT, "(GRADING3A 3)\n");
                        vmmap_remove(map, lopage, npages);
                }
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }

        vma->vma_start = lopage;
        vma->vma_end = lopage +npages;
        vma->vma_off = ADDR_TO_PN(off);

        vma->vma_prot = prot;
        vma->vma_flags = flags;
        vma->vma_obj = NULL;

        list_link_init(&vma->vma_plink);
        list_link_init(&vma->vma_olink);

        mmobj_t *obj = NULL;
        if (file == NULL){
                obj = anon_create();
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }
        else{
                int res = file->vn_ops->mmap(file, vma, &obj);
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }

        if (flags & MAP_PRIVATE){
                mmobj_t *shdw_obj = shadow_create();
                mmobj_t *bottom_obj;

                shdw_obj->mmo_shadowed = obj;
                vma->vma_obj = shdw_obj;

                bottom_obj = obj;
                shdw_obj->mmo_un.mmo_bottom_obj = bottom_obj;
                list_insert_head(&bottom_obj->mmo_un.mmo_vmas, &vma->vma_olink);

                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }
        else{
                vma->vma_obj = vma;
                list_insert_head(&obj->mmo_un.mmo_vmas, &vma->vma_olink);
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }

        vmmap_insert(map, vma);
        if (new != NULL){
                *new = vma;
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }
        dbg(DBG_PRINT, "(GRADING3A 3)\n");
        return 0;
        // NOT_YET_IMPLEMENTED("VM: vmmap_map");
        // return -1;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages)
{
        vmarea_t *vm = NULL;
        unsigned int pages = lopage + npages;
        list_iterate_begin(&map->vmm_list, vm, vmarea_t, vma_plink){
                dbg(DBG_PRINT, "(GRADING3A 3)\n");

                if (lopage> vm->vma_start){
                        dbg(DBG_PRINT, "(GRADING3A 3)\n");
                        if (pages < vm->vma_end){
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                                vmarea_t* vm_area = vmarea_alloc();

                                vm_area->vma_off = vm->vma_off + pages - vm->vma_start;
                                vm_area->vma_start = pages;
                                vm_area->vma_end = vm->vma_end;

                                vm_area->vma_prot = vm->vma_prot;
                                vm_area->vma_flags = vm->vma_flags;
                                vm_area->vma_vmmap = map;
                                vm_area->vma_obj = vm->vma_obj;

                                vm->vma_end = lopage;

                                vmarea_t *temp_area = list_item(vm->vma_plink.l_next, vmarea_t, vma_plink);
                                list_insert_before(&temp_area->vma_plink, &vm_area->vma_plink);

                                mmobj_t *obj = vm->vma_obj, *shdw1 = shadow_create(), *shdw2 = shadow_create();

                                vm->vma_obj = shdw1;
                                vm_area->vma_obj = shdw2;
                                vm_area->vma_obj->mmo_un.mmo_bottom_obj = mmobj_bottom_obj(vm->vma_obj);

                                vm->vma_obj->mmo_un.mmo_bottom_obj = vm_area->vma_obj->mmo_un.mmo_bottom_obj;
                                vm->vma_obj->mmo_shadowed = obj;
                                vm_area->vma_obj->mmo_shadowed = vm->vma_obj->mmo_shadowed;
                                obj->mmo_ops->ref(obj);

                                list_insert_tail(mmobj_bottom_vmas(obj), &vm_area->vma_olink);
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                        }
                        else if((lopage < vm->vma_end) && (pages >= vm->vma_end)){
                                vm->vma_end = lopage;
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                        }
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                else{
                        if (pages >= vm->vma_end){
                                list_remove(&vm->vma_olink);
                                list_remove(&vm->vma_plink);

                                vm->vma_obj->mmo_ops->put(vm->vma_obj);
                                vm->vma_obj = NULL;

                                vmarea_free(vm);
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                        }
                        else if(pages < vm->vma_end && pages > vm->vma_start){
                                vm->vma_off = vm->vma_off + pages - vm->vma_start;
                                vm->vma_start = pages;
                                dbg(DBG_PRINT, "(GRADING3A)\n");
                        }
                        dbg(DBG_PRINT, "(GRADING3A)\n");
                }
                dbg(DBG_PRINT, "(GRADING3A)\n");
        }list_iterate_end();

        tlb_flush_all();
        pt_unmap_range(curproc->p_pagedir, (uintptr_t)PN_TO_ADDR(lopage), (uintptr_t)PN_TO_ADDR(pages));
        dbg(DBG_PRINT, "(GRADING3A)\n");
        return 0;

        // NOT_YET_IMPLEMENTED("VM: vmmap_remove");
        // return -1;
}

/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
        vmarea_t *vm = NULL;
        unsigned int pages = startvfn + npages, endvn = startvfn + npages;

        /* the specified page range must not be empty and lie completely within the user space */
        KASSERT((startvfn < endvfn) && (ADDR_TO_PN(USER_MEM_LOW) <= startvfn) && (ADDR_TO_PN(USER_MEM_HIGH) >= endvfn));
        dbg(DBG_PRINT, "(GRADING3A 3.e)\n");

        list_iterate_begin(&map->vmm_list, vm, vmarea_t, vma_plink){
                if (vm->vma_end > startvfn){
                        if(pages <= vm->vma_start){
                                dbg(DBG_PRINT, "(GRADING3A 3)\n");
                                continue;
                        }
                        else{
                                dbg(DBG_PRINT, "(GRADING3A 3)\n");
                                return 0;
                        }
                        dbg(DBG_PRINT, "(GRADING3A 3)\n");
                }
                dbg(DBG_PRINT, "(GRADING3A 3)\n");
        }list_iterate_end();
        dbg(DBG_PRINT, "(GRADING3A 3)\n");
        return 1;


        // NOT_YET_IMPLEMENTED("VM: vmmap_is_range_empty");
        // return 0;
}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{
        NOT_YET_IMPLEMENTED("VM: vmmap_read");
        return 0;
}

/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
        NOT_YET_IMPLEMENTED("VM: vmmap_write");
        return 0;
}
