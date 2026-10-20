#include <stdio.h>
char *sp(char *p){ return p+1; }
int main(){
 char s[8]="hello";
 int t=*sp(s);          /* gcc: 101 ('e') ; BabyC IR legge 4 byte: 1869376613 */
 printf("%d\n",t);
 return 0;
}
