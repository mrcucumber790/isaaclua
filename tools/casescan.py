import os,re,sys,collections
root=sys.argv[1]
EXT=r'(?:png|anm2|ogg|wav|lua|xml|fnt|stb|mp3)'
pat_str=re.compile(r'["\']([^"\'\n]{1,200}?\.'+EXT+r')["\']',re.I)
pat_req=re.compile(r'\b(include|require)\s*\(?\s*["\']([^"\']+)["\']')
pat_attr=re.compile(r'=\s*"([^"\n]{1,200}?\.'+EXT+r')"',re.I)
res=collections.Counter(); per={}
for mod in sorted(os.listdir(root)):
  md=os.path.join(root,mod)
  if not os.path.isdir(md): continue
  files=[]
  for dp,dn,fn in os.walk(md):
    for f in fn: files.append(os.path.relpath(os.path.join(dp,f),md).replace('\\','/'))
  exact=set('/'+f for f in files); low=collections.defaultdict(list)
  for f in files: low[('/'+f).lower()].append('/'+f)
  lowkeys=list(low)
  bad=[]
  def check(ref,src,kind):
    r=ref.replace('\\','/').lstrip('./').lstrip('/')
    if not r or '..' in r: return
    rl='/'+r.lower()
    hits=[k for k in lowkeys if k.endswith(rl)]
    if not hits: return
    if any(f.endswith('/'+r) for k in hits for f in low[k]): return
    bad.append((kind,src,ref,low[hits[0]][0]))
  for f in files:
    p=os.path.join(md,f); fl=f.lower()
    if not fl.endswith(('.lua','.xml','.anm2')): continue
    try: t=open(p,encoding='utf-8',errors='replace').read()
    except: continue
    if fl.endswith('.lua'):
      for m in pat_req.finditer(t):
        n=m.group(2)
        if m.group(1)=='require': n=n.replace('.','/')
        if not n.lower().endswith('.lua'): n+='.lua'
        check(n,f,m.group(1))
      for m in pat_str.finditer(t): check(m.group(1),f,'luastr')
    else:
      for m in pat_attr.finditer(t):
        check(m.group(1),f,'anm2' if fl.endswith('.anm2') else 'xml')
        # anm2 spritesheets relative to the anm2's dir
  if bad:
    per[mod]=bad
    for b in bad: res[b[0]]+=1
print('mods with mismatches:',len(per),'of',len([m for m in os.listdir(root) if os.path.isdir(os.path.join(root,m))]))
print(dict(res))
for m,b in per.items():
  ks=collections.Counter(x[0] for x in b)
  print('##',m,dict(ks))
  for x in b[:4]: print('   ',x)
