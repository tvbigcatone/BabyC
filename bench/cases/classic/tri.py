def t(n):
    if n<3: return n
    return t(n-1)+t(n-2)+t(n-3)%7
print(t(22))
