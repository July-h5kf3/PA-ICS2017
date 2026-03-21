#include "monitor/watchpoint.h"
#include "monitor/expr.h"

static WP wp_pool[NR_WP];
static WP *head, *free_;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = &wp_pool[i + 1];
    wp_pool[i].Address[0] = '\0';
    wp_pool[i].last_value = 0;
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
    wp->Address[0] = '\0';
    wp->last_value = 0;
    free_ = wp;
}

bool check_wp(void)
{
  for (WP *i = head; i; i = i->next) {
    bool success = true;
    uint32_t value = expr(i->Address, &success);
    if (!success || value == i->last_value) {
      continue;
    }
    printf("The No.%d WatchPoint's value has been changed!\n", i->NO);
    i->last_value = value;
    return true;
  }
  return false;
}

void printWP(void)
{
  printf("Num  What  LastValue\n");
  for(WP* i = head;i;i = i->next)
  {
    printf("%d  %s  %d\n",i->NO,i->Address,i->last_value);
  }
}
void delPoint(int N)
{
  for(WP* i = head;i;i = i->next)
  {
    if(i->NO == N)
    {
      free_wp(i);
      return;
    }
  }
  Assert(0,"The WP did not exist!");
}
