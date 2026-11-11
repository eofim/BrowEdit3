import struct
def convert(d):
    assert d[:4]==b'\x1bLua' and d[4]==0x51
    assert d[8]==4
    out=bytearray(d[:12]); out[8]=8
    p=[12]
    def rd(n):
        b=d[p[0]:p[0]+n]; p[0]+=n; return b
    def u32(): return struct.unpack('<I',rd(4))[0]
    def string():
        n=u32()
        out.extend(struct.pack('<Q',n))
        if n: out.extend(rd(n))
    def copy(n): out.extend(rd(n))
    def func():
        string()
        copy(8+4)       # linedefined,lastline + nups,params,vararg,maxstack
        n=u32(); out.extend(struct.pack('<I',n)); copy(4*n)       # code
        n=u32(); out.extend(struct.pack('<I',n))
        for _ in range(n):
            t=rd(1)[0]; out.append(t)
            if t==0: pass
            elif t==1: copy(1)
            elif t==3: copy(8)
            elif t==4: string()
            else: raise Exception('const type %d'%t)
        n=u32(); out.extend(struct.pack('<I',n))
        for _ in range(n): func()
        n=u32(); out.extend(struct.pack('<I',n)); copy(4*n)       # lineinfo
        n=u32(); out.extend(struct.pack('<I',n))
        for _ in range(n): string(); copy(8)                       # locvars
        n=u32(); out.extend(struct.pack('<I',n))
        for _ in range(n): string()                                # upvalue names
    func()
    return bytes(out)
if __name__=='__main__':
    import sys
    open(sys.argv[2],'wb').write(convert(open(sys.argv[1],'rb').read()))
