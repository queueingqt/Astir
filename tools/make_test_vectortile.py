import sys, math, importlib.util
spec = importlib.util.spec_from_file_location("mk", "tools/make_test_pmtiles.py")
mk = importlib.util.module_from_spec(spec); spec.loader.exec_module(mk)

def varint(v):
    out=bytearray()
    while True:
        b=v&0x7F; v>>=7; out.append(b|(0x80 if v else 0))
        if not v: return bytes(out)
def tag(f,w): return varint((f<<3)|w)
def blob(f,b): return tag(f,2)+varint(len(b))+b
def zz(v): return (v<<1)^(v>>31)

def px(lon,lat,z,tx,ty,extent=4096):
    n=1<<z; x=(lon+180.0)/360.0*n
    r=math.radians(lat); y=(1.0-math.log(math.tan(r)+1/math.cos(r))/math.pi)/2.0*n
    return int(round((x-tx)*extent)), int(round((y-ty)*extent))

def layer(name, feats_spec, z, tx, ty):
    """feats_spec: list of (geomtype, {tagk:tagv}, [(lon,lat),...])"""
    keys=[]; values=[]; feats=[]
    for gtype, tags, pts in feats_spec:
        tbytes=bytearray()
        for k,v in tags.items():
            if k not in keys: keys.append(k)
            if v not in values: values.append(v)
            tbytes += varint(keys.index(k)) + varint(values.index(v))
        geom=bytearray(); cx=cy=0
        if gtype==1:                       # point
            x,y=px(pts[0][0],pts[0][1],z,tx,ty)
            geom += varint((1<<3)|1) + varint(zz(x-cx)) + varint(zz(y-cy))
        else:
            x,y=px(pts[0][0],pts[0][1],z,tx,ty)
            geom += varint((1<<3)|1) + varint(zz(x-cx)) + varint(zz(y-cy)); cx,cy=x,y
            geom += varint(((len(pts)-1)<<3)|2)   # (count << 3) | command
            for lon,lat in pts[1:]:
                x,y=px(lon,lat,z,tx,ty); geom += varint(zz(x-cx))+varint(zz(y-cy)); cx,cy=x,y
            if gtype==3: geom += varint((1<<3)|7)
        feats.append(blob(2,bytes(tbytes)) + tag(3,0)+varint(gtype) + blob(4,bytes(geom)))
    out = blob(1, name.encode())
    for f in feats: out += blob(2,f)
    for k in keys: out += blob(3,k.encode())
    for v in values: out += blob(4, blob(1,v.encode()))
    out += tag(5,0)+varint(4096)
    return blob(3,out)

z=10
tx=int((-118.35+180.0)/360.0*(1<<z))
r=math.radians(34.02); ty=int((1.0-math.log(math.tan(r)+1/math.cos(r))/math.pi)/2.0*(1<<z))

tile  = layer("water", [(3,{"class":"lake"},
        [(-118.44,33.95),(-118.20,33.95),(-118.20,34.00),(-118.44,34.00),(-118.44,33.95)])], z,tx,ty)
tile += layer("landcover", [(3,{"class":"wood"},
        [(-118.44,34.10),(-118.30,34.10),(-118.30,34.16),(-118.44,34.16),(-118.44,34.10)])], z,tx,ty)
tile += layer("landuse", [(3,{"class":"park"},
        [(-118.26,34.03),(-118.16,34.03),(-118.16,34.08),(-118.26,34.08),(-118.26,34.03)])], z,tx,ty)
tile += layer("place", [(1,{"class":"city","name":"Testville"}, [(-118.30,34.05)]),
                        (1,{"class":"town","name":"Smalltown"}, [(-118.18,33.95)])], z,tx,ty)
tile += layer("transportation", [(2,{"highway":"motorway"},
        [(-118.46,34.04),(-118.30,34.05),(-118.14,34.06)])], z,tx,ty)

mk.build("/tmp/all.pmtiles", {(z,tx,ty): tile})
