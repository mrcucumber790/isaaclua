import os,re,sys
# api_coverage.py <apichk output> <version>: APICHK lines (test/apichk mod run by host_test) -> coverage table
# SCRATCH (env) holds apichk/<output>; mods are read from isaac-lua/proj/test under it
S=os.environ.get('SCRATCH', '.') + '/'
src=sys.argv[1]; ver=sys.argv[2]
lines=open(S+'apichk/'+src).read().split('\n')
ok=int(re.search(r'ok (\d+)',lines[0]).group(1))
miss=sorted(set(l.split('APICHK MISS ')[1].strip() for l in lines if 'MISS' in l))  # host draws repeat lines
roots=[S+'isaac-lua/proj/test/mods_cat']+[S+'isaac-lua/proj/test/'+d for d in os.listdir(S+'isaac-lua/proj/test') if d.startswith('mods_') and d!='mods_cat']
mods={}
for r in roots:
  for m in os.listdir(r):
    t=[]
    for a,b,fs in os.walk(os.path.join(r,m)):
      for f in fs:
        if f.endswith('.lua'): t.append(open(os.path.join(a,f),errors='ignore').read())
    mods[m]='\n'.join(t)
rows=[]
for e in miss:
  name=e.split(' ')[0]; cls,_,m=name.replace(':','.').partition('.')
  if m==cls or m.startswith('__'): continue
  if ':' in name: pat=re.compile(r'[:.]'+re.escape(m)+r'\s*\(')
  else: pat=re.compile(r'\.'+re.escape(m)+r'\b(?!\s*\()')
  us=sorted(x for x,t in mods.items() if pat.search(t))
  rows.append((len(us),name,us))
rows.sort(key=lambda r:(-r[0],r[1])); used=[r for r in rows if r[0]]
total=ok+len(rows)
out=[f"# Покрытие API ПК-версии в isaaclua {ver}","",
f"Проверено {total} функций и полей из документации IsaacDocs (конструкторы и метаметоды не считаются). **Есть: {ok}. Нет: {len(rows)}**, из них {len(used)} встречаются в коде хотя бы одного из {len(mods)} проверенных модов.","",
"Как проверял: мод `apichk` на ПК-тесте обращается к каждому имени у настоящего объекта (игрок, комната, спрайт…) и у глобальной таблицы класса. «Есть» значит, что имя отвечает; работает ли оно на Switch так же, как на ПК, эта проверка не говорит.","",
"Число модов посчитано поиском по тексту `:Имя(` или `.Имя`, без учёта класса, поэтому для частых имён (`Render`, `Update`, `Scale`) оно завышено.","",
"## Нет в загрузчике, используется модами","","| Модов | API | Примеры модов |","|---:|---|---|"]
for n,nm,us in used: out.append(f"| {n} | `{nm}` | {', '.join(us[:5])} |")
out+=["","## Нет в загрузчике, проверенные моды не используют","",", ".join(f"`{nm}`" for n,nm,us in rows if not n)]
open('/mnt/project-files/isaac-lua/coverage/api_coverage.md','w').write('\n'.join(out)+'\n')
print(total,ok,len(rows),len(used))
for r in used[:45]: print(r[0],r[1])
