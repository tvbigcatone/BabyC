#include <stdio.h>
#include <stdlib.h>
struct Node { int value; };
void main() {
  struct Node *n = (struct Node *)malloc(sizeof(struct Node));
  void *raw;
  struct Node *again;
  int *p = 0;
  n->value = 7;
  raw = n;
  again = raw;
  if (again != NULL) printf("%d\n", again->value);
  if (NULL != again) printf("ok\n");
  if (p == NULL) printf("null\n");
  free(n);
}
