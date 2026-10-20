class Node:
    __slots__ = ('v', 'next')
    def __init__(self, v, next=None):
        self.v = v
        self.next = next
head = None
for i in range(2000):
    head = Node(i, head)
s = 0
for k in range(300):
    p = head
    while p is not None:
        s += p.v
        p = p.next
print(s)
