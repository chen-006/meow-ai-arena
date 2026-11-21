# python mkvar.py 源 目标 "旧串" "新串" [旧串2 新串2 ...]  —— 每个旧串必须恰好出现一次
import sys
src=open(sys.argv[1],encoding='utf-8',newline='').read()
a=sys.argv[3:]
for i in range(0,len(a),2):
    assert src.count(a[i])==1, ('count', a[i], src.count(a[i]))
    src=src.replace(a[i],a[i+1])
open(sys.argv[2],'w',encoding='utf-8',newline='').write(src)
