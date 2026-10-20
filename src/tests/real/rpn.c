/* Tiny integer RPN: 3 4 + 2 *  => 14 */
#include <stdio.h>
int stack[32];
int top;
void push(int v) { stack[top++] = v; }
int pop(void) { return stack[--top]; }
int main()
{
    top = 0;
    push(3);
    push(4);
    push(pop() + pop());
    push(2);
    {
        int b = pop();
        int a = pop();
        push(a * b);
    }
    printf("%d\n", pop());
    return 0;
}
