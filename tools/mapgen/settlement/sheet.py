"""asset sheet: candidate models in a grid on the (cleared) interior, to look at them in BrowEdit, then undo"""
import sys
from common import *
sets = {
 1: ["roo\\house%s.rsm" % n for n in ("1","2","3","4","5","6","7","8","9","10","11","11-a","12","13","14","15","15-a")] +
    ["prontera_re\\bakery_01.rsm", "roo\\refinery.rsm", "roo\\refinery1.rsm", "roo\\vendor.rsm"] + ["roo\\vendor%d.rsm" % n for n in range(1, 11)] + ["roo\\vendor_ice.rsm", "roo\\vendor_machine.rsm",
    "roo\\field.rsm", "roo\\field2.rsm", "roo\\swing.rsm", "roo\\umbrella2.rsm", "roo\\fountain.rsm", "roo\\fountain1.rsm", "roo\\fountain2.rsm", "roo\\fountain3.rsm", "roo\\statue.rsm", "roo\\success.rsm", "roo\\eran.rsm", "roo\\arc01.rsm",
    "roo\\bridge.rsm", "roo\\bridge1.rsm", "roo\\peco.rsm"],
 2: ["roo\\fence.rsm", "roo\\fence2.rsm", "roo\\fence3.rsm", "roo\\fence4.rsm", "roo\\bakod.rsm", "roo\\bakod1.rsm", "roo\\bakod2.rsm", "roo\\bakod3.rsm", "roo\\wood.rsm", "roo\\wood2.rsm", "roo\\sack.rsm", "roo\\box.rsm", "roo\\box1.rsm", "roo\\box2.rsm", "roo\\box3.rsm",
    "roo\\barrel.rsm", "roo\\barrel1.rsm", "roo\\barrel2.rsm", "roo\\barrel3.rsm", "roo\\barrel4.rsm", "roo\\barrel5.rsm", "roo\\giftbox.rsm", "roo\\giftbox2.rsm", "roo\\post1.rsm", "roo\\sign.rsm", "roo\\sign1.rsm", "roo\\sign2.rsm", "roo\\notice.rsm", "roo\\mailbox.rsm",
    "roo\\flag.rsm", "roo\\flag2.rsm", "roo\\flag3.rsm", "roo\\prt_flag.rsm", "roo\\lamp.rsm", "roo\\lamp_shell.rsm", "roo\\lamp_shell2.rsm", "roo\\lamp_flag.rsm", "roo\\bench.rsm", "roo\\bench2.rsm", "roo\\bench3.rsm", "roo\\plantbox.rsm", "roo\\plantbox2.rsm", "roo\\plantbox3.rsm", "roo\\table.rsm", "roo\\chair.rsm", "roo\\dummy.rsm", "roo\\decor.rsm", "roo\\decor1.rsm", "roo\\decor2.rsm", "roo\\decor3.rsm",
    "roo\\bush.rsm", "roo\\bush1.rsm", "roo\\bush2.rsm", "roo\\bush3.rsm", "roo\\bush4.rsm", "roo\\bush5.rsm", "roo\\tanaman.rsm", "roo\\tanaman2.rsm", "roo\\tanaman3.rsm", "roo\\tanaman4.rsm", "roo\\tanaman5.rsm", "roo\\tanaman6.rsm", "roo\\tanaman7.rsm", "roo\\tanaman8.rsm", "roo\\tanaman9.rsm", "roo\\tanaman10.rsm", "roo\\tanaman11.rsm", "roo\\tanaman12.rsm"],
 3: ["prontera_re\\smithy_0%d.rsm" % n for n in range(1, 7)] + ["prontera_re\\anvil_01.rsm", "prontera_re\\anvil_02.rsm", "prontera_re\\house_01.rsm", "prontera_re\\house_02.rsm", "prontera_re\\storage_s_01.rsm", "prontera_re\\storage_s_02.rsm", "prontera_re\\woodbox_01.rsm", "prontera_re\\woodbox_02.rsm", "prontera_re\\trainingwood_s_01.rsm", "prontera_re\\windmils_01.rsm", "prontera_re\\torch_01.rsm", "prontera_re\\streetlamp_01.rsm", "prontera_re\\barrel_j_01.rsm", "prontera_re\\barrel_s_01.rsm", "prontera_re\\bench_01.rsm", "prontera_re\\box_01.rsm", "prontera_re\\trees_01.rsm", "prontera_re\\trees_02.rsm", "prontera_re\\bundle_s_01.rsm", "prontera_re\\hammer_s_01.rsm", "prontera_re\\build_merchant.rsm", "prontera_re\\build_sword.rsm", "prontera_re\\butcher_01.rsm", "prontera_re\\fishstore_01.rsm", "prontera_re\\fruitsstore_01.rsm", "prontera_re\\shop_h_01.rsm", "prontera_re\\shop01_h_01.rsm", "prontera_re\\plantsprop_01.rsm"] +
    ["ilusion\\cart_j_01.rsm", "ilusion\\cart_j_02.rsm", "ilusion\\cart_j_03.rsm", "crazy_i\\cart_s_01.rsm", "crazy_i\\cart_s_02.rsm", "ilusion\\tent_s_01.rsm", "ilusion\\tent_s_02.rsm", "ilusion\\tent_y_01.rsm", "ilusion\\wood_j_01.rsm", "roo_fild\\rock5.rsm", "roo_fild\\rock8.rsm", "roo_fild\\bush1.rsm", "roo_fild\\tree.rsm"],
}
n = int(sys.argv[1])
names = sets[n]
COLS, DX, DY = 9, 12.0, 14.0
u0, v0 = 30, 34
items, idx = [], []
for i, f in enumerate(names):
    c, r = i % COLS, i // COLS
    u, v = u0 + c * DX, v0 + r * DY
    items.append({"file": f, "name": "sheet%d_%d" % (n, i), "pos": pos_of(u, v, 0), "rot": [0, 0, 0], "scale": [1, 1, 1]})
    idx.append((i, f, round(u), round(v)))
chunks = [items[i:i + 60] for i in range(0, len(items), 60)]
cmds = [{"cmd": "models", "items": ch, "smart_gat": False} for ch in chunks]
cmds.append({"cmd": "camera", "u": 78, "v": 100, "distance": 1500})
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/sheet%d.json" % n, "w"), separators=(",", ":"))
json.dump(idx, open("/tmp/sheet%d_idx.json" % n, "w"))
print(n, len(items), "models in", len(chunks), "chunks")
