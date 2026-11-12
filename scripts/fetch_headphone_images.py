from pathlib import Path, PurePosixPath
import concurrent.futures as cf
import datetime, hashlib, json, os, re, struct, threading, time, urllib.request, zlib

REPO=Path(__file__).resolve().parents[1]
DEST=REPO/'Tools/EGoTouchSettings/Assets/Headphones'
WORK=REPO/'build/headphone-asset-import'
ORIGIN='https://smarthome-drcn.dbankcdn.cn/device/guide/AAM001/'
IDS=['000027','000129','000135','000141','000145','000169','000153','000155','000149','00016D','000163','000174','000167','000173','00015D','000139','00014F']
MAIN=['iv_device_logo.png','device_icon.png','mediacontroller_icon.png','ota_device_icon.png','ota_device.png']
LEFT=['find_left_icon.png','find_left.png','left_ear.png','left_ear_icon.png']
RIGHT=['find_right_icon.png','find_right.png','right_ear.png','right_ear_icon.png']
CASE=['find_box_icon.png','find_box.png','box_icon.png','charging_case.png','charging_box.png','case_icon.png','case.png','img_box.png','img_case.png','box.png']
MAX_ZIP=64*1024*1024
MAX_PNG=16*1024*1024
MAX_PIXELS=16_777_216
LOCK=threading.Lock()
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None
HTTP=urllib.request.build_opener(NoRedirect())
def log(text):
    with LOCK:print(text,flush=True)
def request(url,start=None,end=None):
    assert url.startswith(ORIGIN) and re.fullmatch(r'https://smarthome-drcn\.dbankcdn\.cn/device/guide/AAM001/[0-9A-F]{6}/[0-9A-F]{6}(?:_[0-9A-F]{2})?\.(?:json|zip)',url)
    headers={'Accept-Encoding':'identity','User-Agent':'OpenEGoHub-AssetImport/0.5.17'}
    if start is not None:headers['Range']=f'bytes={start}-{end}'
    for attempt in range(3):
        try:
            with HTTP.open(urllib.request.Request(url,headers=headers),timeout=30) as r:
                if start is None:
                    assert r.status==200
                    data=r.read(512*1024+1);assert len(data)<=512*1024
                else:
                    assert r.status==206, f'Range request returned HTTP {r.status}'
                    m=re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)',r.headers.get('Content-Range',''))
                    assert m and int(m[1])==start and int(m[2])==end, r.headers.get('Content-Range')
                    assert int(m[3])<=MAX_ZIP
                    data=r.read(end-start+2);assert len(data)==end-start+1
                return data,dict(r.headers)
        except Exception:
            if attempt==2:raise
            time.sleep(0.25*(attempt+1))
def head_size(url):
    for attempt in range(3):
        try:
            with HTTP.open(urllib.request.Request(url,method='HEAD',headers={'Accept-Encoding':'identity'}),timeout=30) as r:
                assert r.status==200
                n=int(r.headers['Content-Length']);assert 22<=n<=MAX_ZIP
                return n,r.headers.get('ETag')
        except Exception:
            if attempt==2:raise
            time.sleep(0.25*(attempt+1))
def safe_name(name):
    s=name.replace('\\','/')
    return bool(s) and len(s)<=1024 and not s.startswith('/') and ':' not in s and not any(ord(c)<32 or ord(c)==127 for c in s) and all(p not in ('.','..') for p in s.split('/'))
def config(model):
    url=f'{ORIGIN}{model}/{model}.json';data,_=request(url)
    d=json.loads(data.decode('utf-8-sig'));assert d.get('modelId')==model
    packages=[]
    for color,resource in d.get('resourceMap',{}).items():
        if not re.fullmatch('[0-9A-F]{2}',color):continue
        assert resource.get('resourceKey',color)==color
        assert resource.get('resourceRemark')==f'{model}_{color}.zip'
        kb=resource.get('resourceFileSizeKb');assert isinstance(kb,int) and 0<kb<=MAX_ZIP//1024
        packages.append(dict(modelId=model,colorId=color,key=f'{model}_{color}',marketingName=d.get('marketingName',''),resourceDescription=resource.get('resourceDesc',''),resourceVersion=resource.get('resourceVersion'),configUrl=url,archiveUrl=f'{ORIGIN}{model}/{model}_{color}.zip',declaredSizeKb=kb))
    return packages
def central(package):
    p=dict(package);size,etag=head_size(p['archiveUrl']);p['archiveBytes']=size;p['archiveETag']=etag
    expected=p['declaredSizeKb']*1024
    p['declaredSizeDriftBytes']=size-expected
    if abs(size-expected)>max(64*1024,expected//100):
        p['metadataWarning']='Official fixed-URL archive size differs from JSON; actual bounds, ZIP CRC and PNG checks remain enforced.'
        log(f"METADATA_DRIFT {p['key']} actual={size} declared={expected}")
    start=max(0,size-65557);tail,_=request(p['archiveUrl'],start,size-1)
    at=tail.rfind(b'PK\x05\x06');assert at>=0,'missing ZIP EOCD'
    x=struct.unpack_from('<4s4H2IH',tail,at);assert x[1]==x[2]==0 and x[3]==x[4] and 0<x[4]<=2048
    assert at+22+x[7]==len(tail),'trailing ZIP data'
    cdsize,cdpos=x[5:7];assert 0<cdsize<=4*1024*1024 and cdpos+cdsize<=size
    if cdpos>=start:cd=tail[cdpos-start:cdpos-start+cdsize]
    else:cd,_=request(p['archiveUrl'],cdpos,cdpos+cdsize-1)
    entries=[];pos=0;total=0
    for index in range(x[4]):
        assert cd[pos:pos+4]==b'PK\x01\x02'
        z=struct.unpack_from('<4s6H3I5H2I',cd,pos)
        name=cd[pos+46:pos+46+z[10]].decode('utf-8' if z[3]&0x800 else 'cp437')
        assert safe_name(name), f'unsafe entry {name!r}'
        total+=z[9];assert total<=128*1024*1024
        if name.lower().endswith('.png') and not name.endswith('/'):
            assert not z[3]&1,'encrypted ZIP'
            assert z[4] in (0,8),'unsupported compression'
            assert 0<z[9]<=MAX_PNG and 0<z[8]<=MAX_PNG
            assert z[16]+30+z[10]+z[8]<=size
            entries.append(dict(entry=name,method=z[4],crc=z[7],compressedBytes=z[8],bytes=z[9],localOffset=z[16],index=index))
        pos+=46+z[10]+z[11]+z[12]
    assert pos==len(cd)
    p['zipEntryCount']=x[4];p['entries']=entries
    return p
def local_prefix(package,entry):
    start=entry['localOffset'];end=min(package['archiveBytes']-1,start+4095)
    d,_=request(package['archiveUrl'],start,end)
    assert d[:4]==b'PK\x03\x04'
    x=struct.unpack_from('<4s5H3I2H',d);assert not x[2]&1 and x[3]==entry['method']
    offset=30+x[-2]+x[-1]
    if offset>len(d):d,_=request(package['archiveUrl'],start,start+offset-1)
    name=d[30:30+x[-2]].decode('utf-8' if x[2]&0x800 else 'cp437');assert name==entry['entry']
    entry['dataOffset']=start+offset
    comp=d[offset:offset+entry['compressedBytes']]
    raw=comp if entry['method']==0 else zlib.decompressobj(-15).decompress(comp,65536)
    assert raw[:8]==b'\x89PNG\r\n\x1a\n' and raw[12:16]==b'IHDR'
    w,h=struct.unpack_from('>II',raw,16);entry['width']=w;entry['height']=h
    animated=False;pos=8
    while pos+12<=len(raw):
        length=struct.unpack_from('>I',raw,pos)[0];kind=raw[pos+4:pos+8]
        if kind==b'acTL':animated=True;break
        if kind==b'IDAT':break
        pos+=12+length
    entry['animated']=animated
    return comp
def validate_png(raw):
    assert 45<=len(raw)<=MAX_PNG and raw[:8]==b'\x89PNG\r\n\x1a\n'
    pos=8;saw_header=False;saw_data=False;saw_end=False
    while pos<len(raw):
        assert pos+12<=len(raw)
        size=struct.unpack_from('>I',raw,pos)[0];kind=raw[pos+4:pos+8];end=pos+12+size
        assert end<=len(raw)
        assert zlib.crc32(raw[pos+4:pos+8+size])&0xffffffff==struct.unpack_from('>I',raw,pos+8+size)[0]
        if kind==b'acTL':return False
        if kind==b'IHDR':assert not saw_header and pos==8 and size==13;saw_header=True
        if kind==b'IDAT':assert saw_header and not saw_end;saw_data=True
        if kind==b'IEND':assert size==0 and saw_header and saw_data and end==len(raw);saw_end=True
        pos=end
    assert saw_end
    return True
def rank(entry,names):
    base=entry['entry'].replace('\\','/').rsplit('/',1)[-1].lower()
    return (names.index(base),len(entry['entry']),entry['entry']) if base in names else None
def best(entries,names):
    found=[(rank(e,names),e) for e in entries if rank(e,names) is not None]
    return min(found,key=lambda pair:pair[0])[1] if found else None
def relative_path(package,entry):
    name=entry['entry'].replace('\\','/')
    if name.startswith(package['key']+'/'):name=name[len(package['key'])+1:]
    assert safe_name(name)
    relative=PurePosixPath(package['key'])/name
    target=(DEST/Path(*relative.parts)).resolve()
    assert target.is_relative_to(DEST.resolve())
    return relative.as_posix(),target
def reusable(package):
    checkpoint=WORK/(package['key']+'.json')
    if not checkpoint.is_file():return None
    previous=json.loads(checkpoint.read_text(encoding='utf-8'))
    if previous.get('resourceVersion')!=package['resourceVersion'] or previous.get('failures'):return None
    for image in previous.get('images',[]):
        path=DEST/Path(*PurePosixPath(image['path']).parts)
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=image['sha256']:return None
    return previous
def process(package):
    reuse=reusable(package)
    if reuse:return reuse
    p=central(package)
    excluded=[];failures=[];candidates=[];prefixes={}
    for entry in p['entries']:
        try:
            prefix=local_prefix(p,entry)
            low=entry['entry'].lower()
            if entry['animated'] or any(token in low for token in ('animation','animated','spritesheet','sprite_sheet')):
                excluded.append(dict(entry=entry['entry'],reason='animation',width=entry['width'],height=entry['height']));continue
            if not(0<entry['width']<=4096 and 0<entry['height']<=4096 and entry['width']*entry['height']<=MAX_PIXELS):
                excluded.append(dict(entry=entry['entry'],reason='dimension_limit',width=entry['width'],height=entry['height']));continue
            candidates.append(entry);prefixes[entry['entry']]=prefix
        except Exception as exc:failures.append(dict(entry=entry['entry'],stage='PNG header',error=str(exc)))
    primaries={part:best(candidates,names) for part,names in [('main',MAIN),('left',LEFT),('right',RIGHT),('case',CASE)]}
    if primaries['case'] is None:primaries['case']=primaries['main']
    primary_names={e['entry'] for e in primaries.values() if e is not None}
    images=[];paths=set()
    for entry in candidates:
        if max(entry['width'],entry['height'])<640 and entry['entry'] not in primary_names:
            excluded.append(dict(entry=entry['entry'],reason='small_icon',width=entry['width'],height=entry['height']));continue
        try:
            comp=prefixes[entry['entry']]
            if len(comp)<entry['compressedBytes']:
                remaining,_=request(p['archiveUrl'],entry['dataOffset']+len(comp),entry['dataOffset']+entry['compressedBytes']-1);comp+=remaining
            assert len(comp)==entry['compressedBytes']
            if entry['method']==0:raw=comp
            else:
                inflater=zlib.decompressobj(-15);raw=inflater.decompress(comp,entry['bytes']+1)
                assert inflater.eof and not inflater.unused_data and not inflater.unconsumed_tail
            assert len(raw)==entry['bytes'] and zlib.crc32(raw)&0xffffffff==entry['crc']
            if not validate_png(raw):
                excluded.append(dict(entry=entry['entry'],reason='APNG animation'));continue
            relative,target=relative_path(p,entry)
            assert relative.casefold() not in paths,'case-insensitive path collision'
            paths.add(relative.casefold());target.parent.mkdir(parents=True,exist_ok=True)
            digest=hashlib.sha256(raw).hexdigest()
            if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest()!=digest:
                temporary=target.with_name(target.name+'.download-tmp');temporary.write_bytes(raw);os.replace(temporary,target)
            images.append(dict(path=relative,entry=entry['entry'],width=entry['width'],height=entry['height'],bytes=len(raw),sha256=digest,compressedBytes=entry['compressedBytes'],zipCompression=entry['method'],roles=[part for part,e in primaries.items() if e is not None and e['entry']==entry['entry']]))
        except Exception as exc:failures.append(dict(entry=entry['entry'],stage='PNG download/validation',error=str(exc)))
    public={k:v for k,v in p.items() if k!='entries'}
    public.update(images=images,primaryPaths={part:next((i['path'] for i in images if e is not None and i['entry']==e['entry']),None) for part,e in primaries.items()},excluded=excluded,failures=failures)
    (WORK/(p['key']+'.json')).write_text(json.dumps(public,ensure_ascii=False,indent=2),encoding='utf-8')
    log(f"IMPORTED {p['key']} images={len(images)} bytes={sum(i['bytes'] for i in images)} excluded={len(excluded)} failures={len(failures)}")
    return public
def main():
    DEST.mkdir(parents=True,exist_ok=True);WORK.mkdir(parents=True,exist_ok=True)
    packages=[];failures=[]
    with cf.ThreadPoolExecutor(max_workers=4) as pool:
        for model,result in zip(IDS,pool.map(config,IDS)):packages.extend(result)
        log(f'CONFIGS models={len(IDS)} colorPackages={len(packages)}')
        inventory=list(pool.map(central,packages))
        count=sum(len(p['entries']) for p in inventory);compressed=sum(e['compressedBytes'] for p in inventory for e in p['entries']);raw=sum(e['bytes'] for p in inventory for e in p['entries'])
        log(f'INVENTORY pngEntries={count} maximumCompressedBytes={compressed} maximumPngBytes={raw} (includes animations/small icons before filtering)')
        Path('work/headphone-asset-inventory.json').write_text(json.dumps(inventory,ensure_ascii=False,indent=2),encoding='utf-8')
        results=[]
        futures={pool.submit(process,p):p for p in packages}
        for future in cf.as_completed(futures):
            p=futures[future]
            try:results.append(future.result())
            except Exception as exc:
                error=dict(modelId=p['modelId'],colorId=p['colorId'],archiveUrl=p['archiveUrl'],error=str(exc));failures.append(error);log('FAILED '+json.dumps(error,ensure_ascii=True))
    results.sort(key=lambda p:(p['modelId'],p['colorId']))
    images=[i for p in results for i in p['images']]
    manifest=dict(schemaVersion=1,sourceRepository='https://github.com/Nshpiter/HuaweiPods',sourceCommit='dc5b9d14e9030be95b178699d674d1a3217e1e6d',officialSource='https://smarthome-drcn.dbankcdn.cn/device/guide/AAM001/',generatedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),selection='All static PNGs with width>=640 or height>=640, plus best available main/left/right/case images. APNG, animation-named resources and small icons excluded.',modelCount=len(IDS),colorPackageCount=len(packages),downloadedPackageCount=len(results),imageCount=len(images),totalPngBytes=sum(i['bytes'] for i in images),packages=results,failures=failures)
    temp=DEST/'Manifest.json.tmp';temp.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');os.replace(temp,DEST/'Manifest.json')
    detail_failures=sum(len(p['failures']) for p in results)
    log(f"COMPLETE packages={len(results)}/{len(packages)} images={len(images)} bytes={manifest['totalPngBytes']} packageFailures={len(failures)} fileFailures={detail_failures}")
    return int(bool(failures or detail_failures))
if __name__=='__main__':raise SystemExit(main())
