import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from rogen import MapGen

m = MapGen("gen_test", 60, 60, seed=3)
grass = m.add_texture("grass.bmp")
m.noise(scale=16, amplitude=10)
m.hill(40, 20, 12, 22)
m.river([(0, 38), (25, 34), (60, 42)], width=6, depth=14)
m.water(level=-3)
m.terrace_rect(8, 8, 18, 18, 12)
m.paint_all(grass)
m.save(os.path.join(os.path.dirname(__file__), "out_test"))
