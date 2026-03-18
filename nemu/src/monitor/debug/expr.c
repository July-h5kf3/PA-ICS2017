#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

enum {
  TK_NOTYPE = 256, TK_EQ,TK_NUM,TK_NEG,TK_NEQ,TK_AND,TK_OR,TK_HEX,TK_REG,TK_REF,

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
  {"0[xX][0-9A-Fa-f]+",TK_HEX},
  {"[0-9]+",TK_NUM},
  {"!=",TK_NEQ},
  {"&&",TK_AND},
  {"\\|\\|",TK_OR},
  {"!",'!'},
  {"\\$([Ee][Aa][Xx]|[Ee][Cc][Xx]|[Ee][Dd][Xx]|[Ee][Bb][Xx]|[Ee][Ss][Pp]|[Ee][Bb][Pp]|[Ee][Ss][Ii]|[Ee][Dd][Ii]|[Aa][Xx]|[Cc][Xx]|[Dd][Xx]|[Bb][Xx]|[Ss][Pp]|[Bb][Pp]|[Ss][Ii]|[Dd][Ii]|[Aa][Ll]|[Cc][Ll]|[Dd][Ll]|[Bb][Ll]|[Aa][Hh]|[Cc][Hh]|[Dd][Hh]|[Bb][Hh])$",TK_REG},
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

static bool is_binary_op(int t)
{
  return t == '+' || t == '-' || t == '*' || t == '/' || t == TK_EQ
  || t == TK_NEQ || t == TK_AND || t == TK_OR;
}



static int precedence(int type)
{
  switch (type)
  {
    case TK_EQ: return 3;
    case TK_NEQ: return 3;
    case TK_AND: return 2;
    case TK_OR: return 1;
    case '+':  return 4;
    case '-':  return 4;
    case '*':  return 5;
    case '/':  return 5;
    default:   return -1;
  }
}

Token tokens[32];
int nr_token;

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
          case TK_HEX:
          {
            tokens[nr_token].type = rules[i].token_type;
            Assert(substr_len < sizeof(tokens[nr_token].str),"token too long");

            memcpy(tokens[nr_token].str,substr_start,substr_len);
            tokens[nr_token].str[substr_len] = '\0';

            nr_token++;
            break;
          }
          case TK_REG:
          {
            tokens[nr_token].type = rules[i].token_type;
            Assert(substr_len < sizeof(tokens[nr_token].str),"token too long");

            for (int j = 0; j < substr_len; j++) {
              tokens[nr_token].str[j] = tolower((unsigned char)substr_start[j]);
            }
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
  for(int i = 0;i < nr_token;i++)
  {
    if(tokens[i].type == '-' && (i == 0 || is_binary_op(tokens[i - 1].type) || tokens[i - 1].type == '('))
      {
        tokens[i].type = TK_NEG;
      }
    
  }

  return true;
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
    switch (tokens[p].type)
    {
      case TK_NUM:
      {
        return strtol(tokens[p].str,NULL,10);
      }
      case TK_HEX:
      {
        return strtol(tokens[p].str,NULL,16);
      }
      case TK_REG:
      {
        char *reg = tokens[p].str + 1;
        switch (reg[0]) {
          case 'e':
            switch (reg[1]) {
              case 'a': return cpu.eax;
              case 'c': return cpu.ecx;
              case 'd': return cpu.edx;
              case 'b': return cpu.ebx;
              case 's': return reg[2] == 'p' ? cpu.esp : cpu.esi;
              case 'p': return cpu.ebp;
              default: break;
            }
            break;
          case 'a':
            return reg[1] == 'x' ? reg_w(R_AX) : reg_b(R_AL);
          case 'c':
            return reg[1] == 'x' ? reg_w(R_CX) : reg_b(R_CL);
          case 'd':
            return reg[1] == 'x' ? reg_w(R_DX) : reg_b(R_DL);
          case 'b':
            if (reg[1] == 'x') return reg_w(R_BX);
            if (reg[1] == 'p') return reg_w(R_BP);
            return reg_b(R_BL);
          case 's':
            return reg[1] == 'p' ? reg_w(R_SP) : reg_w(R_SI);
          default:
            break;
        }

        if (reg[1] == 'h') {
          switch (reg[0]) {
            case 'a': return reg_b(R_AH);
            case 'c': return reg_b(R_CH);
            case 'd': return reg_b(R_DH);
            case 'b': return reg_b(R_BH);
            default: break;
          }
        }

        panic("Unknown register: %s", tokens[p].str);
      }
      default:
        panic("Unknown type!");
    }
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
      if(num_left == 0 && is_binary_op(tokens[i].type))
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
    if(dominant_op == NULL) 
    {
      if(tokens[p].type == TK_NEG) return -eval(p+1,q);
      if(tokens[p].type == '!') return !eval(p+1,q);
      panic("illegal expression!");
    }
    uint32_t val1 = eval(p,position - 1);
    uint32_t val2 = eval(position + 1,q);
    switch (dominant_op->type)
    {
      case '+':return val1 + val2;
      case '-':return val1 - val2;
      case '*':return val1 * val2;
      case '/':return val1 / val2;
      case TK_EQ:return val1 == val2;
      case TK_NEQ: return val1 != val2;
      case TK_AND: return val1 && val2;
      case TK_OR:  return val1 || val2;
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
