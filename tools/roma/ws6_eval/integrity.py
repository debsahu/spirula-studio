import sys, struct, json, hashlib
def parse(b):
    n=struct.unpack_from('<Q',b,0)[0]; o=8; ids=[]; rest=bytearray(b)
    for i in range(n):
        o+=4+56+4; e=b.index(b'\0',o); o=e+1; np_=struct.unpack_from('<Q',b,o)[0]; o+=8
        for k in range(np_):
            ids.append(struct.unpack_from('<Q',b,o+16)[0]); rest[o+16:o+24]=b'\0'*8; o+=24
    assert o==len(b); return ids, bytes(rest)
if __name__ == '__main__':
  sib,src=sys.argv[1],sys.argv[2]
  a=open(sib+'/images.bin','rb').read(); b=open(src+'/images.bin','rb').read()
  ia,ra=parse(a); ib,rb=parse(b)
  d=json.load(open(sib+'/densify.json'))
  print(json.dumps(dict(cameras_identical=open(sib+'/cameras.bin','rb').read()==open(src+'/cameras.bin','rb').read(),
    images_equal_but_ids=ra==rb, all_ids_invalid=all(i==2**64-1 for i in ia), n_obs=len(ia), src_linked=sum(i!=2**64-1 for i in ib), mask_keep=d['mask_keep'])))
  