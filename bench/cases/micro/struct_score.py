class S:
    __slots__ = ('a', 'b', 'c')
    def __init__(self, a, b, c):
        self.a, self.b, self.c = a, b, c
def score(p):
    return p.a + p.b + p.c
g = S(3, 5, 7)
t = 0
for i in range(500000):
    t = t + score(g)
print(t)
