/* Anti-regression: Body IR null p->member must fail cleanly */
typedef struct { int x; } S;
int getx(S *p)
{
    return p->x;
}
void main()
{
    getx(0);
}
