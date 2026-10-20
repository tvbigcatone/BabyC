#include <stdio.h>
#include <string.h>
#define L 30000
char t[L+1];
int hist[32];
int main(){
  int i,x=3,len=0,words=0,longest=0,chk=0;
  for(i=0;i<L;i++){ x=(x*1103+12345)%32768; if(((x>>4)%6)==0) t[i]=' '; else t[i]='a'+(x%26); }
  t[L]=0;
  for(i=0;i<=L;i++){
    if(t[i]!=' ' && t[i]!=0) len++;
    else {
      if(len>0){ words++; if(len>longest) longest=len; if(len>31) hist[31]++; else hist[len]++; }
      len=0;
    }
  }
  for(i=0;i<32;i++) chk=(chk*31+hist[i])%1000003;
  printf("%d %d %d %d\n",words,longest,chk,(int)strlen(t));
  return 0;
}
