#include <stdio.h>
#include <stdint.h>
static int32_t **g_a;
static int32_t **g_b;
int main() {
  int32_t ***m[2][2];
  int32_t ***v[2] = {0};
  m[0][0] = &g_a;
  m[1][1] = &g_b;
  printf("%d %d %d\n", m[0][0] != 0, m[1][1] != 0, v[0] == 0);
  return 0;
}
