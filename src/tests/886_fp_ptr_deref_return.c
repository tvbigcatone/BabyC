#include <stdio.h>
double sumd(double *a, int n){ double s=0.0; int i; for(i=0;i<n;i++) s+=a[i]; return s; }
double first(double *a){ return a[1]; }
double deref(double *a){ return *a; }
int main(){ double a[3]; a[0]=1.5; a[1]=2.5; a[2]=3.5; printf("%f %f %f\n", first(a), deref(a), sumd(a,3)); return 0; }
