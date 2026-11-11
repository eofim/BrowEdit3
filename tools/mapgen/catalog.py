"""Search the asset catalog that BrowEdit3 exports (File -> "Export asset catalog (for Claude)" makes catalog.json).
usage: python catalog.py catalog.json tree          -> models/textures whose path contains 'tree'
       python catalog.py catalog.json tree grass    -> several keywords (any of them)"""
import json, sys

def load(path):
    return json.load(open(path))

def find(catalog, *words, kind=None, limit=60):
    out = {}
    for k in ("models", "textures"):
        if kind and k != kind:
            continue
        hits = [x for x in catalog[k] if any(w.lower() in x for w in words)]
        out[k] = hits[:limit]
    return out

if __name__ == "__main__":
    c = load(sys.argv[1])
    print("%d models, %d textures" % (len(c["models"]), len(c["textures"])))
    for k, v in find(c, *sys.argv[2:]).items():
        print("--", k)
        for x in v: print(x)
