#include "monitor/monitor.h"
#include "monitor/expr.h"
#include "monitor/watchpoint.h"
#include "nemu.h"

#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

void cpu_exec(uint64_t);

/* We use the `readline' library to provide more flexibility to read from stdin. */
char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_q(char *args) {
  return -1;
}

static int cmd_si(char *args) {
  int num = 0;
  if(args == NULL)num = 1; 
  else num = atoi(args);
  cpu_exec(num);
  return 0;
}

static int cmd_info(char *args) {
  // CPU_state cpu;
  if(args[0] == 'r')
  {
    printf("EAX  0x%08x  %u\n", (uint32_t)cpu.eax, (uint32_t)cpu.eax);
    printf("ECX  0x%08x  %u\n", (uint32_t)cpu.ecx, (uint32_t)cpu.ecx);
    printf("EDX  0x%08x  %u\n", (uint32_t)cpu.edx, (uint32_t)cpu.edx);
    printf("EBX  0x%08x  %u\n", (uint32_t)cpu.ebx, (uint32_t)cpu.ebx);
    printf("ESP  0x%08x  %u\n", (uint32_t)cpu.esp, (uint32_t)cpu.esp);
    printf("EBP  0x%08x  %u\n", (uint32_t)cpu.ebp, (uint32_t)cpu.ebp);
    printf("ESI  0x%08x  %u\n", (uint32_t)cpu.esi, (uint32_t)cpu.esi);
    printf("EDI  0x%08x  %u\n", (uint32_t)cpu.edi, (uint32_t)cpu.edi);
    printf("AL   0x%02x  %u\n", (uint8_t)reg_b(R_AL), (uint8_t)reg_b(R_AL));
    printf("AH   0x%02x  %u\n", (uint8_t)reg_b(R_AH), (uint8_t)reg_b(R_AH));
    printf("BL   0x%02x  %u\n", (uint8_t)reg_b(R_BL), (uint8_t)reg_b(R_BL));
    printf("BH   0x%02x  %u\n", (uint8_t)reg_b(R_BH), (uint8_t)reg_b(R_BH));
    printf("CL   0x%02x  %u\n", (uint8_t)reg_b(R_CL), (uint8_t)reg_b(R_CL));
    printf("CH   0x%02x  %u\n", (uint8_t)reg_b(R_CH), (uint8_t)reg_b(R_CH));
    printf("DL   0x%02x  %u\n", (uint8_t)reg_b(R_DL), (uint8_t)reg_b(R_DL));
    printf("DH   0x%02x  %u\n", (uint8_t)reg_b(R_DH), (uint8_t)reg_b(R_DH));
    printf("AX   0x%04x  %u\n", (uint16_t)reg_w(R_AX), (uint16_t)reg_w(R_AX));
    printf("CX   0x%04x  %u\n", (uint16_t)reg_w(R_CX), (uint16_t)reg_w(R_CX));
    printf("DX   0x%04x  %u\n", (uint16_t)reg_w(R_DX), (uint16_t)reg_w(R_DX));
    printf("BX   0x%04x  %u\n", (uint16_t)reg_w(R_BX), (uint16_t)reg_w(R_BX));
    printf("SP   0x%04x  %u\n", (uint16_t)reg_w(R_SP), (uint16_t)reg_w(R_SP));
    printf("BP   0x%04x  %u\n", (uint16_t)reg_w(R_BP), (uint16_t)reg_w(R_BP));
    printf("SI   0x%04x  %u\n", (uint16_t)reg_w(R_SI), (uint16_t)reg_w(R_SI));
    printf("DI   0x%04x  %u\n", (uint16_t)reg_w(R_DI), (uint16_t)reg_w(R_DI));
  }
  return 0;
}
static int cmd_x(char *args) {
  char *n = strtok(args," ");
  int num = atoi(n);
  char* expr = n + strlen(n) + 1;
  long base = strtol(expr,NULL,16);
  printf("0x%lx: ",base);
  for(int i = base;i < base + num * 4;i += 4)
  {
    uint32_t data = vaddr_read(i,4);
  }
  return 0;
}

static int cmd_help(char *args);

static struct {
  char *name;
  char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display informations about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si","Excute Single inst",cmd_si},
  {"info","Print infomation",cmd_info},
  {"x","Print Memory location",cmd_x},

  /* TODO: Add more commands */

};

#define NR_CMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void ui_mainloop(int is_batch_mode) {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  while (1) {
    char *str = rl_gets();
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef HAS_IOE
    extern void sdl_clear_event_queue(void);
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}
