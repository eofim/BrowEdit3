# Builds data/spritedb.json from the client's own lua tables (the .lub files below "luafiles514/lua files/datainfo").
# usage: python gen_spritedb.py "<path to data>\luafiles514\lua files\datainfo" data/spritedb.json
# needs: pip install lupa
# The .lub files are 32 bit Lua 5.1 bytecode; lubconv.py rewrites them so a 64 bit Lua 5.1 can run them.
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lubconv import convert
import lupa.lua51 as L

src, dst = sys.argv[1], sys.argv[2]
lua = L.LuaRuntime(encoding=None)
ld = lua.eval('function(d,n) local f,e=loadstring(d,n); if not f then return e end; local ok,err=pcall(f); if not ok then return tostring(err) end; return "ok" end')
def load(name):
    p = os.path.join(src, name)
    if not os.path.exists(p): print('missing', name); return
    r = ld(convert(open(p, 'rb').read()), name.encode())
    if r != b'ok': print(name, r)
for n in ['jobidentity', 'npcidentity', 'accessoryid', 'accname', 'spriterobeid', 'spriterobename', 'weapontable', 'tb_layer_priority']:
    load(n + '.lub')
g = lua.globals()
def s(v): return v.decode('utf-8', 'replace') if isinstance(v, bytes) else v
def table(name):
    t = g[name.encode()]
    return {} if t is None else {s(k): s(v) for k, v in t.items()}

acc_ids = table('ACCESSORY_IDs')        # ACCESSORY_NAME -> view id
acc_names = table('AccNameTable')       # view id -> "_spritename"
accessories = []
for const, vid in acc_ids.items():
    sprite = acc_names.get(vid)
    if sprite: accessories.append({'id': vid, 'name': const[len('ACCESSORY_'):] if const.startswith('ACCESSORY_') else const, 'sprite': sprite})
accessories.sort(key=lambda a: a['id'])

robe_ids = table('SPRITE_ROBE_IDs'); robe_names = table('RobeNameTable')
robes = []
for const, rid in robe_ids.items():
    if rid in robe_names: robes.append({'id': rid, 'name': const[len('ROBE_'):] if const.startswith('ROBE_') else const, 'sprite': robe_names[rid]})
robes.sort(key=lambda a: a['id'])

weapon_ids = table('Weapon_IDs'); weapon_names = table('WeaponNameTable')
weapons = []
for const, wid in weapon_ids.items():
    if wid in weapon_names and weapon_names[wid] != '': weapons.append({'id': wid, 'name': const, 'sprite': weapon_names[wid]})
for wid, name in weapon_names.items():          # ids without a constant
    if name != '' and not any(w['id'] == wid for w in weapons): weapons.append({'id': wid, 'name': 'Weapon %d' % wid, 'sprite': name})
weapons.sort(key=lambda a: a['id'])

prio = {}
t = g[b'TB_Layer_Priority']
if t is not None:
    for k in ('Default_Top', 'Default_Mid', 'Default_Bottom', 'Default_Robe'):
        v = t[k.encode()]
        if isinstance(v, (int, float)): prio[k] = v
# per accessory draw priorities: viewId -> {d: default, r: [8 per direction values or null]}
items = {}
t = g[b'TB_Layer_Priority']
if t is not None and t[b'Items_List'] is not None:
    for vid, e in t[b'Items_List'].items():
        o = {}
        if e[b'Default'] is not None: o['d'] = e[b'Default']
        dr = e[b'Direction']
        if dr is not None: o['r'] = {int(k): v for k, v in dr.items() if isinstance(v, (int, float))}
        items[int(vid)] = o
# Korean sprite name -> job constant (client keeps these in its binary, not in the lua tables)
KR = {'초보자':'NOVICE','검사':'SWORDMAN','마법사':'MAGICIAN','궁수':'ARCHER','성직자':'ACOLYTE','상인':'MERCHANT','도둑':'THIEF',
 '기사':'KNIGHT','프리스트':'PRIEST','위저드':'WIZARD','제철공':'BLACKSMITH','헌터':'HUNTER','어세신':'ASSASSIN','크루세이더':'CRUSADER',
 '몽크':'MONK','세이지':'SAGE','로그':'ROGUE','연금술사':'ALCHEMIST','바드':'BARD','결혼':'WEDDING','슈퍼노비스':'SUPERNOVICE','건너':'GUNSLINGER',
 '닌자':'NINJA','산타':'XMAS','로드나이트':'KNIGHT_H','하이위저드':'WIZARD_H','화이트스미스':'BLACKSMITH_H','스나이퍼':'HUNTER_H','어쌔신크로스':'ASSASSIN_H',
 '팔라딘':'CRUSADER_H','챔피온':'MONK_H','프로페서':'SAGE_H','스토커':'ROGUE_H','크리에이터':'ALCHEMIST_H','클라운':'BARD_H','하이프리':'PRIEST_H',
 '태권소년':'TAEKWON','권성':'STAR','소울링커':'LINKER','룬나이트':'RUNE_KNIGHT','워록':'WARLOCK','레인져':'RANGER','아크비숍':'ARCHBISHOP',
 '미케닉':'MECHANIC','길로틴크로스':'GUILLOTINE_CROSS','소서러':'SORCERER','민스트럴':'MINSTREL','슈라':'SURA','제네릭':'GENETIC',
 '쉐도우체이서':'SHADOW_CHASER','리벨리온':'REBELLION','가드':'GUARD','여름':'SUMMER','한복':'HANBOK','성제':'SOUL_ASCETIC'}
jt = {s(k): v for k, v in g[b'JTtbl'].items()}
jobs = []
for kr, c in KR.items():
    jobs.append({'sprite': kr, 'const': c, 'id': jt.get('JT_' + c, -1)})
byid = {}
for k, v in jt.items():
    if isinstance(v, (int, float)) and v < 4400 and k.startswith('JT_'): byid[k[3:].lower()] = int(v)
json.dump({'accessories': accessories, 'robes': robes, 'weapons': weapons, 'layerPriority': prio, 'layerItems': items, 'jobs': jobs, 'jobConsts': byid},
          open(dst, 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
print(len(accessories), 'accessories', len(robes), 'robes', len(weapons), 'weapons ->', dst)
