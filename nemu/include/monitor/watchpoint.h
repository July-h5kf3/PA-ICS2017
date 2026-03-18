#ifndef __WATCHPOINT_H__
#define __WATCHPOINT_H__
#define NR_WP 32
#include "common.h"

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  /* TODO: Add more members if necessary */
  char* Address;
  int last_value;

} WP;

void init_wp_pool(void);

WP* new_wp();
void free_wp(WP* wp);
bool check_wp(void);
void printWP(void);
void delPoint(int N);

#endif
