/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "string.h"
#include "vm/vm.h"
#include "vm/file.h"
#include "vm/anon.h"
#include "vm/inspect.h"
#include "threads/synch.h"
#include "userprog/process.h"

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);

#define MAX_STACK_SIZE (1 << 20)

struct frame_table {
  struct lock lock;
  struct list list;
  uint64_t *arr;
  int ptr;
};

struct frame_table frame_tbl;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
  vm_anon_init ();
  vm_file_init ();
#ifdef EFILESYS /* For project 4 */
  pagecache_init ();
#endif
  register_inspect_intr ();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
  frame_tbl.arr = calloc (get_pages_size (), sizeof (uint64_t));
  frame_tbl.ptr = 0;

  lock_init (&frame_tbl.lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type ofthe page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
  int ty = VM_TYPE (page->operations->type);
  switch (ty) {
  case VM_UNINIT:
    return VM_TYPE (page->uninit.type);
  default:
    return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
                                vm_initializer *init, void *aux) {

  ASSERT (VM_TYPE (type) != VM_UNINIT)

  struct thread *t = thread_current ();
  struct supplemental_page_table *spt = &t->spt;
  struct page *page_p = NULL;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page (spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */

    /* TODO: Insert the page into the spt. */

    // !!! MALLOC !!!
    page_p = malloc (sizeof (struct page));
    bool success = false;

    if (page_p == NULL)
      goto err;

    switch (VM_TYPE (type)) {
    case VM_ANON:
      uninit_new (page_p, upage, init, type, aux, anon_initializer);
      break;
    case VM_FILE:
      uninit_new (page_p, upage, init, type, aux, file_backed_initializer);
      break;

    default:
      PANIC ("unknown page type");
      goto err;
    }

    page_p->writable = writable;
    page_p->pml4 = thread_current ()->pml4;
    page_p->type = VM_UNINIT;

    success = spt_insert_page (spt, page_p);

    if (!success)
      goto err;

    return true;
  }
err:
  if (page_p != NULL)
    free (page_p);
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
  struct page *page = NULL;
  struct page mock_page;
  mock_page.va = pg_round_down (va);
  struct hash_elem *cur;

  if (cur = hash_find (&spt->page_table, &mock_page.elem)) {
    page = hash_entry (cur, struct page, elem);
  }

  return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
  int succ = false;

  // TODO: validation 추가하기

  if (hash_insert (&spt->page_table, &page->elem) == NULL) {
    succ = true;
  }

  return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
  void *res;
  res = hash_delete (&spt->page_table, &page->elem);
  vm_dealloc_page (page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  int pages_size = get_pages_size ();
  bool found = false;

  for (int i = 0; i < pages_size; i++) {
    victim = frame_tbl.arr[frame_tbl.ptr];

    if (!pml4_is_accessed (victim->page->pml4, victim->page->va)) {
      found = true;
    } else {
      pml4_set_accessed (victim->page->pml4, victim->page->va, false);
    }

    if (found) {
      frame_tbl.ptr += 1;
      frame_tbl.ptr %= pages_size;
      break;
    }

    frame_tbl.ptr += 1;
    frame_tbl.ptr %= pages_size;
  }

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
  bool success;
  struct frame *victim = vm_get_victim ();
  success = swap_out (victim->page);
  /* TODO: swap out the victim and return the evicted frame. */

  return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.
 * frame은 null pointer가 아닌 것이 보장된다.
 * */
static struct frame *
vm_get_frame (void) {
  struct frame *frame = NULL;
  void *kva = NULL;

  kva = palloc_get_page (PAL_USER);
  if (kva == NULL) {
    frame = vm_evict_frame ();
  } else {
    // !!! MALLOC !!!
    frame = malloc (sizeof (struct frame));
    if (frame == NULL)
      PANIC ("vm_get_frame() todo 2");
    /* insert frame table */
    frame->kva = kva;
    void *BASE = get_base ();

    int idx = (int) (frame->kva - BASE) / PGSIZE;

    ASSERT (0 <= idx && idx < get_pages_size ());
    frame_tbl.arr[idx] = frame;
  }

  frame->page = NULL;

  ASSERT (frame != NULL);
  ASSERT (frame->page == NULL);
  return frame;
}

bool
vm_alloc_stack_page (void *addr) {
  ASSERT (pg_ofs (addr) == 0);

  if (!vm_alloc_page (VM_ANON | VM_MARKER_0, addr, true))
    return false;

  if (!vm_claim_page (addr))
    return false;

  return true;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
  bool success;
  struct supplemental_page_table *spt = &thread_current ()->spt;
  void *cur = pg_round_down (addr);

  while (!spt_find_page (spt, cur)) {
    success = vm_alloc_stack_page (cur);
    if (!success)
      PANIC ("stack allocation fail!");

    cur += PGSIZE;
  }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user UNUSED,
                     bool write, bool not_present) {
  struct supplemental_page_table *spt = &thread_current ()->spt;
  struct page *page = spt_find_page (spt, addr);

  if (!not_present && write)
    return false;

  // clang-format off
  if (spt_find_page (spt, f->rsp) == NULL 
   && addr < USER_STACK && addr >= USER_STACK - MAX_STACK_SIZE) {
    vm_stack_growth (addr);
    return true;
  }
  // clang-format on

  if (page == NULL)
    return false;

  return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
  destroy (page);
  free (page);
}

/* Claim the page that allocate on VA. */
/* Eager Loading 기능 */
bool
vm_claim_page (void *va) {
  struct page *page_p = spt_find_page (&thread_current ()->spt, va);

  if (page_p == NULL)
    return false;

  return vm_do_claim_page (page_p);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
  struct frame *frame = vm_get_frame ();
  struct thread *t = thread_current ();
  bool success = false;

  /* Set links */
  frame->page = page;
  page->frame = frame;
  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  // printf ("%lx\n", page->va);
  success = pml4_set_page (t->pml4, page->va, frame->kva, page->writable);

  if (!success) {
    PANIC ("vm_do_claim_page() todo");
  }

  return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
  /* Project 3 - Virtual Memory */
  struct thread *t = thread_current ();
  hash_init (&t->spt.page_table, page_hash, page_less, NULL);
  list_init (&t->spt.mapped_pages);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
                              struct supplemental_page_table *src) {
  struct hash_iterator iter;
  struct page *parrent_page_p;
  bool success = false;

  hash_first (&iter, src);

  while (hash_next (&iter)) {
    parrent_page_p = hash_entry (hash_cur (&iter), struct page, elem);

    enum vm_type p_type = VM_TYPE (parrent_page_p->operations->type);
    void *va = parrent_page_p->va;
    void *p_aux = NULL;
    bool writable = parrent_page_p->writable;
    vm_initializer *init = NULL;

    void *aux = NULL;
    int TMP_SIZE = sizeof (struct load_seg_args);
    // todo: struct load_seg_args 이외 경우 대비하기

    switch (p_type) {
    case VM_UNINIT:
      init = parrent_page_p->uninit.init;
      p_aux = parrent_page_p->uninit.aux;
      break;
    case VM_ANON:
      init = parrent_page_p->anon.init;
      p_aux = parrent_page_p->anon.aux;
      break;
    case VM_FILE:
      init = parrent_page_p->file.init;
      p_aux = parrent_page_p->file.aux;
      break;

    default:
      PANIC ("unexpected page type!");
      goto err;
    }

    if (p_aux != NULL) {
      aux = malloc (TMP_SIZE);
      memcpy (aux, p_aux, TMP_SIZE);
    }
    // todo: stack 마킹은?
    success = vm_alloc_page_with_initializer (page_get_type (parrent_page_p),
                                              va, writable, init, aux);
    if (!success)
      goto err;

    if (p_type != VM_UNINIT) {
      success = vm_claim_page (va);
      if (!success)
        goto err;

      memcpy (va, parrent_page_p->frame->kva, PGSIZE);
    }
  }

  return true;
err:
  return false;
}

static void
clear_page_resource (struct hash_elem *e, void *aux) {
  struct page *page_p = hash_entry (e, struct page, elem);

  vm_dealloc_page (page_p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
  struct list *mapped_pages = &thread_current ()->spt.mapped_pages;

  if (list_begin (mapped_pages) != NULL)
    while (!list_empty (mapped_pages)) {
      struct list_elem *cur = list_begin (mapped_pages);
      struct page *page_p = list_entry (cur, struct page, mmap_elem);
      do_munmap (page_p->va);
    }

  hash_clear (&spt->page_table, clear_page_resource);
}

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, elem);
  return hash_bytes (&p->va, sizeof p->va);
}

bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  return a->va < b->va;
}