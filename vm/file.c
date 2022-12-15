/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "userprog/syscall.h"

#include <round.h>

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;
  page->type = type;

  struct file_page *file_page = &page->file;

  file_page->init = page->uninit.init;
  file_page->aux = page->uninit.aux;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
  struct file_page *file_page UNUSED = &page->file;
  printf ("FILE SWAP IN!\n");
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
  struct file_page *file_page = &page->file;
  struct list *mapped_pages = &thread_current ()->spt.mapped_pages;

  // printf ("============ Before Traverse ============\n");
  // for (struct list_elem *cur = list_begin (mapped_pages);
  //      cur != list_end (mapped_pages); cur = list_next (cur)) {

  //   struct page *f = list_entry (cur, struct page, mmap_elem);
  //   printf ("%d\n", f->mmap_length);
  // }

  // printf ("<1> is dirty: %d\n", pml4_is_dirty (page->pml4, page->va));
  // pml4_set_dirty (page->pml4, page->va, true);
  // printf ("<2> is dirty: %d\n", pml4_is_dirty (page->pml4, page->va));
  // do_munmap (page->va);

  // page->frame = NULL;
  // pml4_clear_page (page->pml4, page->va);
  // todo: exception 처리하기
  // printf ("FILE SWAP OUT!\n");
  return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
  struct file_page *file_page = &page->file;
  void *aux = file_page->aux;
  struct frame *frame_p = page->frame;

  if (frame_p != NULL)
    free (frame_p);
  if (aux != NULL)
    free (aux);
}

static bool
mmap_lazy_load (struct page *page, void *aux) {
  struct list *mapped_pages = &thread_current ()->spt.mapped_pages;
  struct load_seg_args *args = aux;
  struct file *file = args->file;
  off_t ofs = args->ofs;
  size_t page_read_bytes = args->page_read_bytes;
  size_t page_zero_bytes = args->page_zero_bytes;
  size_t read_bytes = args->read_bytes;
  bool has_lock = false;

  file_seek (file, ofs);

  lock_acquire (&file_lock);
  off_t res = file_read (file, page->frame->kva, page_read_bytes);
  page->mmap_length = read_bytes;
  lock_release (&file_lock);
  memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
  list_push_back (mapped_pages, &page->mmap_elem);

  return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file,
         off_t offset) {
  ASSERT (pg_ofs (addr) == 0);
  ASSERT (offset % PGSIZE == 0);

  size_t read_bytes = length;
  size_t zero_bytes = ROUND_UP (length, PGSIZE) - length;
  void *cur = addr;

  struct supplemental_page_table *spt = &thread_current ()->spt;
  for (size_t cur_ = 0; cur_ < length; cur_ += PGSIZE)
    if (spt_find_page (spt, addr + cur_))
      return NULL;

  while (read_bytes > 0 || zero_bytes > 0) {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct load_seg_args *aux = malloc (sizeof (struct load_seg_args));

    if (aux == NULL)
      return NULL;
    // clang-format off
    *aux = (struct load_seg_args){
      .file = file,
      .ofs = offset,
      .page_read_bytes = page_read_bytes,
      .page_zero_bytes = page_zero_bytes,
      .read_bytes = read_bytes
      };
    // clang-format on

    if (!vm_alloc_page_with_initializer (VM_FILE, cur, writable, mmap_lazy_load,
                                         aux)) {
      return NULL;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    offset += PGSIZE;
    cur += PGSIZE;
  }

  return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
  struct supplemental_page_table *spt = &thread_current ()->spt;
  struct page *page_p = spt_find_page (spt, addr);
  void *cur = addr;
  long remain_length;
  size_t write_bytes;

  remain_length = page_p->mmap_length;
  while (remain_length > 0) {
    write_bytes = remain_length > 0 && remain_length < PGSIZE   //
                      ? remain_length
                      : PGSIZE;

    if (VM_TYPE (page_p->type) == VM_FILE) {
      struct load_seg_args *args = (struct load_seg_args *) (page_p->file.aux);
      struct file *file = args->file;
      off_t ofs = args->ofs;

      if (pml4_is_dirty (thread_current ()->pml4, cur))
        file_write_at (file, cur, write_bytes, ofs);
    }

    cur += PGSIZE;
    if (page_p->operations->type == VM_FILE)
      list_remove (&page_p->mmap_elem);

    spt_remove_page (spt, page_p);
    page_p = spt_find_page (spt, cur);

    remain_length -= PGSIZE;
  }
}
