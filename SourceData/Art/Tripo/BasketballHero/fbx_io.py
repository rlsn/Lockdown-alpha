"""Binary FBX reader/writer retaining property types and untouched payloads."""
import struct,zlib,io
from pathlib import Path
def read(path):
 f=io.BytesIO(Path(path).read_bytes());header=f.read(23);version=struct.unpack('<I',f.read(4))[0]
 wide=version>=7500;fmt='<QQQB' if wide else '<IIIB';size=25 if wide else 13
 def prop():
  t=f.read(1).decode();types={'Y':'h','C':'?','I':'i','F':'f','D':'d','L':'q'}
  if t in types:
   s='<'+types[t];return t,struct.unpack(s,f.read(struct.calcsize(s)))[0]
  if t in 'SR':
   b=f.read(struct.unpack('<I',f.read(4))[0]);return t,b.decode(errors='strict') if t=='S' else b
  if t in 'fdilbc':
   n,enc,sz=struct.unpack('<III',f.read(12));b=f.read(sz)
   if enc:b=zlib.decompress(b)
   return t,list(struct.unpack('<'+str(n)+{'f':'f','d':'d','i':'i','l':'q','b':'?','c':'b'}[t],b))
  raise ValueError(t)
 def node():
  end,num,nbytes,nlen=struct.unpack(fmt,f.read(size))
  if not end:return None
  name=f.read(nlen).decode();pv=[prop() for _ in range(num)];children=[];null=False
  while f.tell()<end:
   c=node()
   if c is None:null=True;break
   children.append(c)
  f.seek(end);return {'name':name,'types':[x[0] for x in pv],'props':[x[1] for x in pv],'children':children,'null':null}
 nodes=[]
 while True:
  n=node()
  if n is None:break
  nodes.append(n)
 return {'header':header,'version':version,'nodes':nodes,'footer':f.read()}
def write(data,path):
 f=io.BytesIO();f.write(data['header']);f.write(struct.pack('<I',data['version']));wide=data['version']>=7500;fmt='<QQQB' if wide else '<IIIB';size=25 if wide else 13
 def prop(t,v):
  f.write(t.encode());types={'Y':'h','C':'?','I':'i','F':'f','D':'d','L':'q'}
  if t in types:f.write(struct.pack('<'+types[t],v));return
  if t in 'SR':
   b=v.encode() if t=='S' else bytes(v);f.write(struct.pack('<I',len(b)));f.write(b);return
  if t in 'fdilbc':
   b=struct.pack('<'+str(len(v))+{'f':'f','d':'d','i':'i','l':'q','b':'?','c':'b'}[t],*v);b=zlib.compress(b);f.write(struct.pack('<III',len(v),1,len(b)));f.write(b);return
  raise ValueError(t)
 def node(n):
  start=f.tell();f.write(bytes(size));name=n['name'].encode();f.write(name);ps=f.tell()
  assert len(n['types'])==len(n['props'])
  for t,v in zip(n['types'],n['props']):prop(t,v)
  plen=f.tell()-ps
  for c in n['children']:node(c)
  if n['null'] or n['children']:f.write(bytes(size))
  end=f.tell();f.seek(start);f.write(struct.pack(fmt,end,len(n['props']),plen,len(name)));f.seek(end)
 for n in data['nodes']:node(n)
 f.write(bytes(size));f.write(data['footer']);Path(path).write_bytes(f.getvalue())
def child(n,name):return next(c for c in n['children'] if c['name']==name)
