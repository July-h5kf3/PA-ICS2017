#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

enum {
  TK_NOTYPE = 256, TK_EQ,TK_NUM,

  /* TODO: Add more token types */

};

static struct rule {
  char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"==", TK_EQ},         // equal
  {"\\-",'-'},
  {"\\*",'*'},
  {"/",'/'},
  {"\\(",'('},
  {"\\)",')'},
  {"[0-9]+",TK_NUM},
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]) )

static regex_t re[NR_REGEX];

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static int precedence(int type)
{
  switch (type)
  {
    case TK_EQ: return 1;
    case '+':  return 2;
    case '-':  return 2;
    case '*':  return 3;
    case '/':  return 3;
    default:   return -1;
  }
}

Token tokens[32];
int nr_token;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);
        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          // default: TODO();
          case TK_NUM:
          {
            tokens[nr_token].type = rules[i].token_type;
            Assert(substr_len < sizeof(tokens[nr_token].str),"token too long");
            
            memcpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            
            nr_token++;
            break;
          }
          case TK_NOTYPE:
            break;
          default:
          {
            tokens[nr_token++].type = rules[i].token_type;
          }
        }
        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

bool check_parentheses(int p,int q)
{
  if (tokens[p].type != '(' || tokens[q].type != ')') return false;

  int top = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') top++;
    else if (tokens[i].type == ')') {
      if (top == 0) return false;
      top--;
      if (top == 0 && i < q) return false;
    }
  }

  return top == 0;
}

uint32_t eval(int p,int q)
{
  if(p > q)
  {
    panic("Bad Expression");
  }
  else if (p == q)
  {
    // printf("qwq");
    return atoi(tokens[p].str);
  }
  else if(check_parentheses(p,q) == true)
  {
    return eval(p + 1,q - 1);
  }
  else
  {
    Token* dominant_op = NULL;
    int position = 0;
    int num_left = 0;
    for(int i = p;i <= q;i++)
    {
      // printf("%d th token is %c\n",i,tokens[i].type);
      if(num_left == 0 && (tokens[i].type == '+' || tokens[i].type == '-' || tokens[i].type == '*' || tokens[i].type == '/'))
      {
        if(dominant_op == NULL || precedence(tokens[i].type) <= precedence(dominant_op->type))
        {
          dominant_op = &tokens[i];
          position = i;  
        }
      }
      if(tokens[i].type == '(') num_left++;
      else if(tokens[i].type == ')')num_left--;
    }
    uint32_t val1 = eval(p,position - 1);
    uint32_t val2 = eval(position + 1,q);
    if(dominant_op == NULL) panic("illegal expression!");
    switch (dominant_op->type)
    {
      case '+':return val1 + val2;
      case '-':return val1 - val2;
      case '*':return val1 * val2;
      case '/':return val1 / val2;
      default:assert(0);
    }
  }

}

uint32_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    printf("segment fail!");
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  // TODO();
  int p = 0;
  int q = nr_token - 1;

  return eval(p,q);
}
