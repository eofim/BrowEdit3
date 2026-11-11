"""step A: clear the copy's models, put back only walls/towers/entrance + pines/rocks/cliffs outside the walls"""
from common import *
keep = kept_originals()
items = [item(o) for o in keep]
cmds = [{"cmd": "info"}, {"cmd": "delete_models", "filter": ""}]
for i in range(0, len(items), 60):
    cmds.append({"cmd": "models", "items": items[i:i + 60], "smart_gat": True})
cmds.append({"cmd": "camera", "u": 78, "v": 100, "distance": 1300})
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/t1a.json", "w"), separators=(",", ":"))
print(len(items), "kept models,", len(cmds) - 3, "chunks")
