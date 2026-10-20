#include <stdio.h>
#include <stdlib.h>
struct Node { int val; struct Node *next; };
int main(){
  int r,i,sum=0;
  struct Node *head;
  struct Node *n;
  struct Node *p;
  for(r=0;r<20;r++){
    head=NULL;
    for(i=0;i<5000;i++){
      n=malloc(sizeof(struct Node));
      n->val=i*3+r; n->next=head; head=n;
    }
    for(p=head;p!=NULL;p=p->next) sum=(sum+p->val)%1000003;
    while(head!=NULL){ p=head->next; free(head); head=p; }
  }
  printf("%d\n",sum);
  return 0;
}
