/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/mmu.h"
#include <bitmap.h>
#include <string.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

struct swap_table {
  struct bitmap *used_map; /* Bitmap of free swap slots. */
  struct lock lock;        /* Mutual exclusion. */
};

static struct swap_table swap_tbl;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
  /* TODO: Set up the swap_disk. */
  swap_disk = disk_get (1, 1);

  /* setup swap table */
  int swap_size = disk_size (swap_disk) / 8;
  int block_size = bitmap_buf_size (swap_size);

  void *bitmap_block = malloc (block_size);
  void *res = bitmap_create_in_buf (swap_size, bitmap_block, block_size);
  ASSERT (swap_size == bitmap_size (bitmap_block));
  ASSERT (res == bitmap_block);
  swap_tbl.used_map = res;
  lock_init (&swap_tbl.lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &anon_ops;
  page->type = type;

  struct anon_page *anon_page = &page->anon;
  anon_page->init = page->uninit.init;
  anon_page->aux = page->uninit.aux;
  anon_page->swap_slot = -1;
  anon_page->is_swapped_out = false;

  // ! 별도의 initializer를 실행하고 싶지않다면 false를 리턴하자
  // ! RAX에 뭐든 들어갈 것이기 때문에 웬만하면 true 리턴함...

  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  lock_acquire (&swap_tbl.lock);
  // todo: exception 처리하기
  char buf[512];
  size_t swap_slot = anon_page->swap_slot;

  for (int i = 0; i < 8; i++) {
    disk_read (swap_disk, swap_slot * 8 + i, buf);
    memcpy (kva + i * 512, buf, 512);
  }

  bitmap_reset (swap_tbl.used_map, swap_slot);
  anon_page->is_swapped_out = false;

  lock_release (&swap_tbl.lock);

  return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
  struct anon_page *anon_page = &page->anon;
  lock_acquire (&swap_tbl.lock);
  // todo: exception 처리하기
  size_t swap_slot = bitmap_scan_and_flip (swap_tbl.used_map, 0, 1, false);
  void *va = page->va;

  for (int i = 0; i < 8; i++)
    disk_write (swap_disk, swap_slot * 8 + i, va + 512 * i);

  anon_page->swap_slot = swap_slot;
  anon_page->is_swapped_out = true;

  page->frame = NULL;
  pml4_clear_page (page->pml4, page->va);

  lock_release (&swap_tbl.lock);

  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
  struct anon_page *anon_page = &page->anon;
  void *aux = anon_page->aux;
  struct frame *frame_p = page->frame;

  if (frame_p != NULL)
    free (frame_p);
  if (aux != NULL)
    free (aux);
}
