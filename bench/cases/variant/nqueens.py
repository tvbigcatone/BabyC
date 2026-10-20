N=9
cols=[0]*N; d1=[0]*(2*N); d2=[0]*(2*N)
def solve(r):
    cnt=0
    if r==N: return 1
    for c in range(N):
        if cols[c] or d1[r+c] or d2[r-c+N]: continue
        cols[c]=1; d1[r+c]=1; d2[r-c+N]=1
        cnt+=solve(r+1)
        cols[c]=0; d1[r+c]=0; d2[r-c+N]=0
    return cnt
print(solve(0))
