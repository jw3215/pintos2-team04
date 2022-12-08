#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

struct anon_page {
  vm_initializer *init;
  void *aux;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
