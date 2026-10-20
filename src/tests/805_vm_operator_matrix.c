/* Differential opcode matrix for the compact VM. Keep this body call-free so
 * attempts/runs prove the arithmetic and branch instructions execute in VM. */
int main(void)
{
    int a, b, q, r, neg, logical, inv;
    unsigned int u, v, andv, orv, xorv, left, right;
    a = 19;
    b = 4;
    q = a / b;
    r = a % b;
    neg = -a;
    logical = !0;
    inv = ~a;
    u = 0xF0F0U;
    v = 0x0FF0U;
    andv = u & v;
    orv = u | v;
    xorv = u ^ v;
    left = v << 2;
    right = u >> 4;
    if (q != 4 || r != 3 || q * 10 + r != 43) return 1;
    if (neg != -19 || logical != 1 || inv != -20) return 2;
    if (andv != 0x00F0U || orv != 0xFFF0U || xorv != 0xFF00U) return 3;
    if (left != 0x3FC0U || right != 0x0F0FU) return 4;
    if (!(a > b) || !(a >= b) || !(b < a) || !(b <= a)) return 5;
    if (a == b || a != 19 || !(a != b)) return 6;
    if ((a & 1) != 1 || (a | 4) != 23 || (a ^ 3) != 16) return 7;
    return 0;
}
