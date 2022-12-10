/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
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
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
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
  struct load_seg_args *args = aux;
  struct file *file = args->file;
  off_t ofs = args->ofs;
  size_t page_read_bytes = args->page_read_bytes;
  size_t page_zero_bytes = args->page_zero_bytes;
  bool has_lock = false;

  // printf ("<1>------------------\n");

  file_seek (file, ofs);

  // printf ("<2>---------%d------\n", page_read_bytes);

  lock_acquire (&file_lock);
  off_t res = file_read (file, page->frame->kva, page_read_bytes);
  // hex_dump (0, page->frame->kva, 800, true);
  // if (res != (int) page_read_bytes) {
  //   lock_release (&file_lock);
  //   printf ("<3>-----%d-------------\n", res);
  //   return false;
  // }
  lock_release (&file_lock);
  memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);
  // printf ("<4>------------------\n");

  return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file,
         off_t offset) {
  // printf ("===============\n");
  // printf ("file: 0x%lx, ofs: %d, upage: %lx\n", file, ofs, upage);
  // printf ("read_bytes: %d, zero_bytes: %d, writable: %d\n", read_bytes,
  //         zero_bytes, writable);
  // printf ("===============\n");

  ASSERT (pg_ofs (addr) == 0);
  ASSERT (offset % PGSIZE == 0);

  size_t read_bytes = length;
  size_t zero_bytes = ROUND_UP (length, PGSIZE) - length;

  while (read_bytes > 0 || zero_bytes > 0) {
    /* Do calculate how to fill this page.
     * We will read PAGE_READ_BYTES bytes from FILE
     * and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    struct load_seg_args *aux = malloc (sizeof (struct load_seg_args));

    // clang-format off
    *aux = (struct load_seg_args){
      .file = file,
      .ofs = offset,
      .page_read_bytes = page_read_bytes,
      .page_zero_bytes = page_zero_bytes
      };
    // clang-format on

    if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable,
                                         mmap_lazy_load, aux)) {
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    offset += PGSIZE;
    addr += PGSIZE;
  }
  // printf ("###########\n");
  return true;
}

/* Do the munmap */
void
do_munmap (void *addr) {}
