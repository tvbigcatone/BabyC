#include <stdio.h>
int add(int a,int b){return a+b;}
int sub(int a,int b){return a-b;}
int mul(int a,int b){return a*b;}
int (*ops[3])(int,int);
int apply(int i,int x,int y){ return (*ops[i])(x,y); }
int main(){
  int i,acc; ops[0]=&add; ops[1]=&sub; ops[2]=&mul; acc=1;
  for(i=0;i<3;i++) acc=apply(i,acc,i+2);
  printf("%d\n",acc); printf("%d\n",(*ops[2])(6,7)); return 0;
}
