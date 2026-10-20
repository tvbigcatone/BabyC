#include <stdio.h>
#define L 5000
char buf[L+1];
int main(){
  int r,i,j,vow,h=0;
  char t;
  for(r=0;r<20;r++){
    for(i=0;i<L;i++) buf[i]='a'+((i*7+r)%26);
    buf[L]=0;
    vow=0;
    for(i=0;buf[i]!=0;i++){
      char c=buf[i];
      if(c=='a'||c=='e'||c=='i'||c=='o'||c=='u') vow++;
    }
    i=0; j=L-1;
    while(i<j){ t=buf[i]; buf[i]=buf[j]; buf[j]=t; i++; j--; }
    for(i=0;i<L;i++) h=(h*33+buf[i])%1000003;
    h=(h+vow)%1000003;
  }
  printf("%d\n",h);
  return 0;
}
