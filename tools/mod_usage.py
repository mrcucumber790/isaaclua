import re, os, sys, collections
proj, apichk, missfile, storedfile, out = sys.argv[1:6]
src = open(os.path.join(proj, apichk)).read()
classes = {}
for m in re.finditer(r'^\s*(\w+) = \{f = \{([^}]*)\}, v = \{([^}]*)\}\}', src, re.M):
    cls = m.group(1)
    fs = re.findall(r'"(\w+)"', m.group(2)); vs = re.findall(r'"(\w+)"', m.group(3))
    classes[cls] = (fs, vs)
cat = {}
for line in open(missfile):
    m = re.match(r'APICHK MISS (\w+)[:.](\w+)', line)
    if m: cat[(m.group(1), m.group(2))] = 'нет'
for line in open(storedfile):
    m = re.match(r'(\w+)\.(\w+)', line.strip())
    if m: cat[(m.group(1), m.group(2))] = 'только хранится'
CONST = {'GetSmoothBodyRotation','GetLastActionTriggers','GetModelingClayEffect','GetTractorBeam','IsP2Appearing',
 'GetTearRangeModifier','SetTargetTrapDoor','IsSeedComboBanned','GetScreenPointScale','GetChallengeIdByName',
 'GetCostumeIdByPath','GetEvadeMovementCountdown','SetCanCrushRocks','ToPressurePlate','GetCanSeeEverything','SetCanSeeEverything'}
APPROX = {'Die','GetEndPoint','CalculateEndPoint','GetMultiShotPositionVelocity','AnimWalkFrame','QueryNPCsGroup','QueryNPCsSpawnerType',
 'QueryNPCsType','GetNumBlueFlies','GetNumBlueSpiders','GetLastDirection','GetShootingJoystick','GetMovementJoystick','IsInvincible'}
names = collections.defaultdict(set)
for cls, (fs, vs) in classes.items():
    for n in fs: names[n].add((cls, 'f'))
    for n in vs: names[n].add((cls, 'v'))
def status(n):
    sts = set()
    for cls, k in names[n]:
        s = cat.get((cls, n))
        if s: sts.add(s + ' (' + cls + ')')
    if n in CONST: sts.add('константа/заглушка')
    if n in APPROX: sts.add('приближённо')
    return sts
mods_dir = os.path.join(proj, 'test/mods_cat')
use = collections.defaultdict(lambda: [set(), 0, collections.Counter()])
pat = {}
interesting = [n for n in names if status(n)]
for n in interesting:
    pat[n] = re.compile(r'([.:])' + n + r'\b(\s*\()?')
for mod in sorted(os.listdir(mods_dir)):
    for root, _, files in os.walk(os.path.join(mods_dir, mod)):
        for f in files:
            if not f.endswith('.lua'): continue
            try: t = open(os.path.join(root, f), errors='replace').read()
            except Exception: continue
            for n in interesting:
                if n not in t: continue
                for m in pat[n].finditer(t):
                    pre = t[max(0, m.start()-40):m.start()]
                    if re.search(r'function\s+[\w.:]*$', pre): continue  # the mod's own definition
                    u = use[n]; u[0].add(mod); u[1] += 1
                    after = t[m.end():m.end()+60]
                    if m.group(2):  # skip the call's arguments
                        d = 1; i = 0
                        while i < len(after) and d: d += {'(':1, ')':-1}.get(after[i], 0); i += 1
                        after = after[i:]
                    a = after.lstrip()
                    if a.startswith('..') or pre.rstrip().endswith('..'): u[2]['строка'] += 1
                    elif a[:1] in '.:[' : u[2]['индексирует'] += 1
                    elif re.match(r'(==|~=|<|>)', a) or re.search(r'(==|~=|<=|>=|<|>)\s*$', pre): u[2]['сравнивает'] += 1
                    elif re.match(r'[-+*/%]', a): u[2]['считает'] += 1
                    elif re.search(r'(if|not|and|or)\s*$', pre): u[2]['условие'] += 1
                    elif a.startswith('=') and not a.startswith('=='): u[2]['пишет'] += 1
rows = sorted(interesting, key=lambda n: -len(use[n][0]))
with open(out, 'w') as o:
    o.write('# Что моды каталога ждут от неполных мест API\n\n')
    o.write('Скан %d модов (`test/mods_cat`): для каждого имени API, которое у загрузчика отсутствует, только хранится в Lua,\n' % len(os.listdir(mods_dir)))
    o.write('отвечает константой или приближённо, считается, сколько модов его вызывают и что делают с ответом.\n')
    o.write('Совпадение имени у разных классов и у собственных функций мода даёт шум: это оценка, не точный подсчёт.\n\n')
    o.write('| Имя | Модов | Вызовов | Что делают с ответом | Состояние в загрузчике |\n|---|---|---|---|---|\n')
    for n in rows:
        mods, k, how = use[n]
        h = ', '.join('%s %d' % x for x in how.most_common())
        o.write('| `%s` | %d | %d | %s | %s |\n' % (n, len(mods), k, h or '-', '; '.join(sorted(status(n)))))
print(len(rows), 'names;', sum(1 for n in rows if use[n][0]), 'used by some mod')
for n in rows[:25]: print(n, len(use[n][0]), dict(use[n][2]), sorted(status(n)))
