#include "monitor/watchpoint.h"
#include "monitor/expr.h"

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head, *free_;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = &wp_pool[i + 1];
  }
  wp_pool[NR_WP - 1].next = NULL;

  head = NULL;
  free_ = wp_pool;
}

/* TODO: Implement the functionality of watchpoint */
WP* new_wp()
{
  if(free_ == NULL)
  Assert(0,"There is no free WP!");
  WP* cnt = free_;
  WP* tmp = free_->next;
  free_ = tmp;
  if(head != NULL)cnt->next = head;
  else cnt->next = NULL;
  head = cnt;
  return cnt;
}

void free_wp(WP* wp)
{
    if(head == NULL)
    Assert(0,"There is no WP now!");
    if(head == wp)head = wp->next;
    else
    {
      WP* DelP = NULL;
      for(WP* i = head;i;i = i->next)
      {
        if(i->next == wp)
        {
          DelP = i;
          break;
        }
      }
      if(DelP == NULL)Assert(0,"WP not found!");
      DelP->next = wp->next;
    }
    wp->next = free_;
    free_ = wp;
}

