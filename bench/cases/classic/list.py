class Node:
    __slots__=('val','next')
    def __init__(self,val,nxt): self.val=val; self.next=nxt
sum_=0
for r in range(20):
    head=None
    for i in range(5000):
        head=Node(i*3+r,head)
    p=head
    while p is not None:
        sum_=(sum_+p.val)%1000003; p=p.next
    head=None
print(sum_)
