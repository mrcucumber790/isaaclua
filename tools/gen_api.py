#!/usr/bin/env python3
"""gen_api.py <IsaacDocs docs dir> <funcs.tsv> <missing list> <vtable targets> > src/api_gen.lua

Bindings for PC API methods that api.lua does not have yet but the Switch game exports with a matching
C++ signature: the parameter list of the exported function (c++filt) must map one to one onto the
documented Lua parameters. Only argument kinds core.call can pass are used; anything else is left out.
Functions some vtable or function table points at (virtual: the subclass's own version may differ) are left
out too: <vtable targets> lists, one hex address per line, every address the game's relocations point at.
"""
import re, subprocess, sys, os

docs, funcs_tsv, missing_txt, vt_txt = sys.argv[1:5]
VT = {int(l, 16) for l in open(vt_txt) if l.strip()}
CLASS = {  # Lua class -> (C++ class, object expression, entity type test or None)
    'Entity': ('Entity', 'ptr(e)', None),
    'EntityPlayer': ('Entity_Player', 'ptr(e)', 'e.Type == 1'),
    'EntityTear': ('Entity_Tear', 'ptr(e)', 'e.Type == 2'),
    'EntityFamiliar': ('Entity_Familiar', 'ptr(e)', 'e.Type == 3'),
    'EntityBomb': ('Entity_Bomb', 'ptr(e)', 'e.Type == 4'),
    'EntityPickup': ('Entity_Pickup', 'ptr(e)', 'e.Type == 5'),
    'EntityLaser': ('Entity_Laser', 'ptr(e)', 'e.Type == 7'),
    'EntityKnife': ('Entity_Knife', 'ptr(e)', 'e.Type == 8'),
    'EntityProjectile': ('Entity_Projectile', 'ptr(e)', 'e.Type == 9'),
    'EntityNPC': ('Entity_NPC', 'ptr(e)', 'e.Type >= 10 and e.Type < 1000'),
    'EntityEffect': ('Entity_Effect', 'ptr(e)', 'e.Type == 1000'),
    'Room': ('Room', 'roomp()', None),
    'Level': ('Level', 'gp()', None),
    'Seeds': ('Seeds', 'offset(gp(), O.Game_Seeds)', None),
    'ItemPool': ('ItemPool', 'poolp()', None),
}
# virtual or engine-driven: calling the base class's version directly would be wrong
SKIP = {'Update', 'Render', 'PostRender', 'RenderShadowLayer', 'Init', 'Remove', 'Kill', 'Die', 'Morph'}

def enums():
    return {f[:-3] for f in os.listdir(os.path.join(docs, 'enums'))} if os.path.isdir(os.path.join(docs, 'enums')) else set()
ENUMS = enums() | {'ChampionColor', 'LinecheckMode', 'ItemPoolType', 'CollectibleType', 'TrinketType', 'Card',
                   'PillEffect', 'PillColor', 'EntityType', 'NullItemID', 'ActiveSlot', 'PlayerType', 'SoundEffect',
                   'GridEntityType', 'RoomType', 'LevelStage', 'StageType', 'Direction', 'BombVariant',
                   'CacheFlag', 'DamageFlag', 'EntityFlag', 'TearFlags', 'LevelCurse', 'PocketItemSlot', 'BabySubType'}

def lua_type(t):
    t = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', t).replace('const ', '').strip()
    return t

def doc_sigs(cls):
    path = os.path.join(docs, cls + '.md')
    if not os.path.exists(path): return {}
    out = {}
    for l in open(path, encoding='utf-8'):
        m = re.match(r"^#### (.+?) (\w+) \( ?(.*?) ?\) \{: \.copyable aria-label='Functions'", l)
        if not m: continue
        ret, name, params = lua_type(m.group(1)), m.group(2), m.group(3)
        ps = []
        for p in [x.strip() for x in params.split(',')] if params.strip() else []:
            p = lua_type(p)
            mm = re.match(r'^(.+?) (\w+)(?: = (.+))?$', p)
            if not mm: ps = None; break
            ps.append((mm.group(1).strip(), mm.group(2), mm.group(3)))
        if ps is None: continue
        out.setdefault(name, []).append((ret, ps))
    return out

def cpp_kind(t):
    t = t.strip()
    if t in ('int', 'long', 'unsigned long', 'short', 'unsigned short', 'unsigned char', 'char'): return 'i'
    if t == 'unsigned int': return 'u'
    if t == 'bool': return 'b'
    if t == 'float': return 'f'
    if t == 'KAGE::Math::Vector2 const&': return 'V'
    if t == 'KAGE::Math::Vector2': return 'v'
    if re.match(r'^IsaacRepentance::(Entity|Entity_\w+)\*$', t): return 'p'
    if t == 'IsaacRepentance::Entity::EntityRef const&': return 'R'
    if re.match(r'^IsaacRepentance::(\w+::)?e\w+$', t): return 'i'  # enums
    return None

def lua_kind(t):
    if t in ('int', 'integer') or t in ENUMS or t.endswith('Type') or t.endswith('Flag') or t.endswith('Flags'): return 'i'
    if t == 'boolean': return 'b'
    if t == 'float': return 'f'
    if t == 'Vector': return 'V'
    if t.startswith('Entity') and t not in ('EntityRef', 'EntityPtr', 'EntityList'): return 'p'
    if t == 'EntityRef': return 'R'
    return None

def ret_kind(t):
    if t == 'void': return '_'
    if t == 'TearFlags': return None  # bitset128_t comes back in x0:x1 (hand-written in api_post.lua)
    if t in ('int', 'integer') or t in ENUMS: return 'i'
    if t == 'boolean': return 'b'
    if t == 'float': return 'f'
    if t == 'Vector': return 'v'
    if t.startswith('Entity') and t not in ('EntityRef', 'EntityPtr', 'EntityList'): return 'e'
    return None

def split_params(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '<(': depth += 1
        if ch in '>)': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [x.strip() for x in out]

ADDR = {}
for l in open(funcs_tsv):
    a, _, n = l.rstrip('\n').split('\t')
    ADDR[n] = int(a, 16)
syms = list(ADDR)
miss = [l.strip() for l in open(missing_txt) if ':' in l]
by_cls = {}
for m in miss:
    c, n = m.split(':'); by_cls.setdefault(c, []).append(n.split(' ')[0])

lines, made, skipped = [], 0, []
for cls, names in sorted(by_cls.items()):
    if cls not in CLASS: continue
    cpp, objexpr, test = CLASS[cls]
    docsig = doc_sigs(cls)
    for name in sorted(set(names)):
        if name in SKIP: skipped.append(cls + ':' + name + ' (virtual)'); continue
        cands = []
        # the PC name, else its snake_case form (the Switch build keeps e.g. Entity_Familiar::add_to_orbit)
        for nm in (name, re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()):
            pat = '%d%s%d%s' % (len(cpp), cpp, len(nm), nm)
            cands += [s for s in syms if s.startswith(('_ZN15IsaacRepentance' + pat, '_ZNK15IsaacRepentance' + pat))]
        if not cands or name not in docsig: skipped.append(cls + ':' + name + ' (no export)'); continue
        done = False
        if all(ADDR[s] in VT for s in cands): skipped.append(cls + ':' + name + ' (virtual)'); continue
        for s in cands:
            if ADDR[s] in VT: continue
            dm = subprocess.run(['c++filt', s], capture_output=True, text=True).stdout.strip()
            m = re.search(r'\((.*)\)( const)?$', dm)
            if not m: continue
            cp = split_params(m.group(1))
            if cp == ['void']: cp = []
            ck = [cpp_kind(x) for x in cp]
            if None in ck: continue
            for ret, ps in docsig[name]:
                rk = ret_kind(ret)
                if rk is None or len(ps) != len(cp): continue
                lk = [lua_kind(p[0]) for p in ps]
                ok = all(a == b or (a == 'i' and b in 'iu') or (a == 'V' and b == 'v') for a, b in zip(lk, ck))
                if not ok: continue
                sig = rk + 'p' + ''.join('p' if k == 'R' else ('V' if k == 'V' else k) for k in ck)
                argn = [p[1].lower() + '_' for p in ps]
                conv = []
                for (pt, pn, dflt), k, a in zip(ps, ck, argn):
                    if dflt and dflt != 'nil': conv.append('if %s == nil then %s = %s end' % (a, a, dflt))
                    elif k == 'i' or k == 'u': conv.append('%s = %s or 0' % (a, a))
                    elif k == 'f': conv.append('%s = %s or 0' % (a, a))
                    elif k in 'Vv': conv.append('%s = %s or Vector(0, 0)' % (a, a))
                    if k == 'R': conv.append('%s = gen_ref(%s)' % (a, a))
                    if k == 'v': pass
                default = {'_': 'nil', 'i': '0', 'b': 'false', 'f': '0', 'v': 'Vector(0, 0)', 'e': 'nil'}[rk]
                what = '%s.%s' % (cls, name)
                var = 'gen_' + cls + '_' + name
                tbl = {'Room': 'room', 'Level': 'level', 'Seeds': 'seeds', 'ItemPool': 'itempool'}.get(cls, 'EM')
                lines.append('  do local %s = native("%s", "%s", "%s", %s)' % (var, s, sig, what, default))
                body = ' '.join(conv)
                call = '%s(%s%s)' % (var, objexpr, ''.join(', ' + a for a in argn))
                head = 'function(e%s)' % ''.join(', ' + a for a in argn)
                if tbl == 'EM':
                    guard = ('if not (%s) then return %s end ' % (test, default)) if test else ''
                    prev = 'local prev_%s = EM.%s' % (var, name)
                    # a method of another class with the same name: dispatch by type
                    lines.append('    ' + prev)
                    fb = 'prev_%s and prev_%s(e%s) or %s' % (var, var, ''.join(', ' + a for a in argn), default)
                    guard = ('if not (%s) then return %s end ' % (test, fb)) if test else ''
                    lines.append('    EM.%s = %s %s%s return %s end end' % (name, head, guard, body + ' ' if body else '', call))
                else:
                    lines.append('    if rawget(%s, "%s") == nil then %s.%s = %s %s return %s end end end' %
                                 (tbl, name, tbl, name, head, body + ' ' if body else '', call))
                made += 1; done = True
                break
            if done: break
        if not done: skipped.append(cls + ':' + name + ' (signature)')

print('-- generated by tools/gen_api.py from the IsaacDocs function list and the Switch game\'s exports:')
print('-- %d PC API methods the game exports with the documented parameters (do not edit by hand)' % made)
print('do')
print('''  local function gen_ref(src)  -- an engine EntityRef (layout at Entity::TakeDamage in api.lua)
    local r = C.buf(0x30)
    src = src and C.unbox(src) or {}
    poke(r, 0, "i32", src.Type or 0); poke(r, 4, "i32", src.Variant or 0)
    poke(r, 8, "i32", src.SpawnerType or 0); poke(r, 0xc, "i32", src.SpawnerVariant or 0)
    local pos, vel = src.Position or Vector(0, 0), src.Velocity or Vector(0, 0)
    poke(r, 0x10, "f32", pos.X); poke(r, 0x14, "f32", pos.Y); poke(r, 0x18, "f32", vel.X); poke(r, 0x1c, "f32", vel.Y)
    poke(r, 0x20, "u32", (src.IsCharmed and 1 or 0) | (src.IsFriendly and 2 or 0))
    if src.Entity and ptr(src.Entity) then poke(r, 0x28, "p", ptr(src.Entity)) end
    return r
  end''')
print('\n'.join(lines))
print('end')
sys.stderr.write('made %d, skipped %d\n' % (made, len(skipped)))
open(os.path.join(os.path.dirname(missing_txt), 'gen_skipped.txt'), 'w').write('\n'.join(skipped) + '\n')
