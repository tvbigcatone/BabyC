#include <stdio.h>

/* C99 trailing comma after last enumerator */
typedef enum{
   gwCommReset       = 1,
   gwCommStatStop    = 2,
   gwCommProtPompa   = 3,
   gwCommMode        = 4,
   gwCommTimer       = 5,
}COMM_GATEWAY;

enum Color {
  Red = 10,
  Green = 20,
  Blue = 30,
};

typedef enum { Alone = 7, } Solo_t;

int main() {
  COMM_GATEWAY g = gwCommMode;
  enum Color c = Green;
  Solo_t s = Alone;
  printf("%d %d %d %d %d\n",
         (int)g, (int)gwCommReset, (int)gwCommTimer,
         (int)c, (int)s);
  return 0;
}
