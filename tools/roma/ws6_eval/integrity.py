"""integrity.py <sibling dir> <source model dir>: the section 9.5 byte-level checks (cameras identical, images identical but for point ids,
every observation id invalid) printed as JSON. parse() is also used by ws4_eval_basement.py."""
import sys, struct, json
def parse(b):
    n=struct.unpack_from('<Q',b,0)[0]; o=8; ids=[]; rest=bytearray(b)
    for i in range(n):
        o+=4+56+4; e=b.index(b'\0',o); o=e+1; np_=struct.unpack_from('<Q',b,o)[0]; o+=8
        for k in range(np_):
            ids.append(struct.unpack_from('<Q',b,o+16)[0]); rest[o+16:o+24]=b'\0'*8; o+=24
    assert o==len(b); return ids, bytes(rest)
def main():
    import evalcfg
    ap = evalcfg.parser(__doc__)
    ap.add_argument('sibling', help='densify output dir (images.bin, cameras.bin, densify.json)')
    ap.add_argument('source', help='the model it was densified from')
    a = evalcfg.parse(ap)
    sib, src = a.sibling, a.source
    for d, w in ((sib, 'sibling'), (src, 'source model')):
        for f in ('images.bin', 'cameras.bin'): evalcfg.check_path(f'{d}/{f}', f'positional <{w}>', 'file', f'{w} {f}')
    evalcfg.check_path(sib + '/densify.json', 'positional <sibling>', 'file', 'sibling densify.json')
    ia,ra=parse(open(sib+'/images.bin','rb').read()); ib,rb=parse(open(src+'/images.bin','rb').read())
    d=json.load(open(sib+'/densify.json'))
    print(json.dumps(dict(cameras_identical=open(sib+'/cameras.bin','rb').read()==open(src+'/cameras.bin','rb').read(),
      images_equal_but_ids=ra==rb, all_ids_invalid=all(i==2**64-1 for i in ia), n_obs=len(ia), src_linked=sum(i!=2**64-1 for i in ib), mask_keep=d['mask_keep'])))
if __name__ == '__main__':
    main()
