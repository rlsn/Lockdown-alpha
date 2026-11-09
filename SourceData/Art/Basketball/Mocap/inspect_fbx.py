import struct,zlib,json,sys
from pathlib import Path
def read_fbx(path):
    f=open(path,'rb');f.seek(23);version=struct.unpack('<I',f.read(4))[0]
    wide=version>=7500;fmt='<QQQB' if wide else '<IIIB';size=25 if wide else 13
    def prop():
        t=f.read(1).decode()
        types={'Y':'h','C':'?','I':'i','F':'f','D':'d','L':'q'}
        if t in types:
            fmt='<'+types[t];return struct.unpack(fmt,f.read(struct.calcsize(fmt)))[0]
        if t in 'SR':
            b=f.read(struct.unpack('<I',f.read(4))[0]);return b.decode(errors='replace') if t=='S' else list(b)
        if t in 'fdilbc':
            n,enc,sz=struct.unpack('<III',f.read(12));b=f.read(sz)
            if enc:b=zlib.decompress(b)
            return list(struct.unpack('<'+str(n)+{'f':'f','d':'d','i':'i','l':'q','b':'?','c':'b'}[t],b))
        raise ValueError(t)
    def node():
        end,num,nbytes,nlen=struct.unpack(fmt,f.read(size))
        if not end:return None
        name=f.read(nlen).decode();props=[prop() for _ in range(num)];children=[]
        while f.tell()<end:
            c=node()
            if c is None:break
            children.append(c)
        f.seek(end);return {'name':name,'props':props,'children':children}
    nodes=[]
    while True:
        n=node()
        if not n:break
        nodes.append(n)
    return nodes
if __name__=='__main__':
    nodes=read_fbx(sys.argv[1]);Path(sys.argv[1]+'.json').write_text(json.dumps(nodes))
    for top in nodes:
        if top['name'] in ['GlobalSettings','Connections']:print(top)
        if top['name']=='Objects':
            for o in top['children']:
                if o['name']=='Model':print(o)
