/* Anti-regression: Body IR null p->arr[i] must fail cleanly */
typedef struct { char a[4]; } S;
int geta(S *p)
{
    return p->a[0];
}
void main()
{
    geta(0);
}
