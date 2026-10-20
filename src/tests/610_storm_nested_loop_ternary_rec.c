#include <stdio.h>
int fib(int n){ return (n<2)?n:fib(n-1)+fib(n-2); }
int walk(int *a,int n,int depth){
  int i,s; if(depth<=0) return 0; s=0;
  for(i=0;i<n;i++){ int t=(a[i]%2==0)?a[i]/2:a[i]*3+1; s+=t;
    if(i+1<n) s+=walk(a+i,1,depth-1); }
  return s;
}
int main(){ int a[5]; int i; for(i=0;i<5;i++) a[i]=i+1;
  printf("f %d\n",fib(8)); printf("w %d\n",walk(a,5,2)); return 0; }
