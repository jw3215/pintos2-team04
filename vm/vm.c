/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/file.h"
#include "vm/anon.h"
#include "vm/inspect.h"

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);

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

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page (spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */

    /* TODO: Insert the page into the spt. */

    // !!! MALLOC !!!
    struct page *page_p = malloc (sizeof (struct page));
    bool success = false;

    if (page_p == NULL)
      goto err;

    switch (VM_TYPE (type)) {
    case VM_ANON:
      uninit_new (page_p, upage, init, VM_ANON, aux, anon_initializer);
      break;
    case VM_FILE:
      uninit_new (page_p, upage, init, VM_FILE, aux, file_backed_initializer);
      break;

    default:
      goto err;
    }

    page_p->writable = writable;
    page_p->type = VM_UNINIT;

    success = spt_insert_page (spt, page_p);

    if (!success) {
      free (page_p);
      goto err;
    }

    return true;
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
  struct page *page = NULL;
  struct page mock_page;
  mock_page.va = va;
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
  vm_dealloc_page (page);
  return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
  struct frame *victim UNUSED = vm_get_victim ();
  /* TODO: swap out the victim and return the evicted frame. */

  return NULL;
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
  if (kva == NULL)
    PANIC ("vm_get_frame() todo");

  frame = malloc (sizeof (frame));
  if (frame == NULL)
    PANIC ("vm_get_frame() todo");

  // !!! MALLOC !!!
  // todo: free frame struct
  // todo: swap out when page allocation is failed

  frame->kva = kva;
  frame->page = NULL;

  ASSERT (frame != NULL);
  ASSERT (frame->page == NULL);
  return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr, bool user UNUSED,
                     bool write UNUSED, bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
  struct page *page = NULL;
  page = spt_find_page (&spt->page_table, pg_round_down (addr));

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
  struct page *page_p = NULL;
  vm_alloc_page (VM_ANON, va, 1);

  page_p = spt_find_page (&thread_current ()->spt, va);

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
  success = pml4_set_page (t->pml4, page->va, frame->kva, page->writable);

  if (!success) {
    PANIC ("vm_do_claim_page() todo");
  }

  return swap_in (page, frame->kva);
  // // todo: swap_in 적용하기
  // if (page->type != VM_UNINIT) {
  //   return true;
  // } else {
  // return swap_in (page, frame->kva);
  // uninit_page의 swap_in()은 uninit_initialize()
  // }
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
  /* Project 3 - Virtual Memory */
  struct thread *t = thread_current ();
  hash_init (&t->spt.page_table, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
                              struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
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