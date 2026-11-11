import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from rogen import MapGen

m = MapGen("gen_village", 100, 100, seed=7)
grass = m.add_texture("backside\\grass.bmp")
dirt = m.add_texture("backside\\dirt.bmp")
sand = m.add_texture("backside\\sand.bmp")
rock = m.add_texture("backside\\rock.bmp")
m.noise(scale=24, amplitude=14)
m.hill(75, 25, 18, 28)
m.river([(0, 60), (30, 55), (55, 70), (100, 62)], width=7, depth=16)
m.flatten(38, 30, 62, 50, height=0.0, blend=4)             # village square
m.water(level=-4)
m.path([(5, 20), (40, 40), (50, 40), (95, 85)], width=3, texture=dirt)
m.terrace_rect(10, 10, 22, 22, 14)                          # a raised block with cliff walls
m.set_wall_texture(rock)
m.paint_all(grass)
m.paint_noise(dirt, scale=6, coverage=0.15)
m.paint_by_slope(rock, 0.55)
m.paint_shore(sand, 1.5)
m.path([(5, 20), (40, 40), (50, 40), (95, 85)], width=3, texture=dirt)
for (u, v) in [(42, 34), (55, 34), (42, 46), (56, 46)]:
    m.place("house.rsm", u, v, rot=(0, 15, 0), footprint=(4, 3.4), name="house")
m.scatter(["tree1.rsm", "tree2.rsm"], 140, region=(3, 3, 97, 97), min_dist=3, footprint=(0.8, 0.8),
          avoid=("water", "path"), max_slope=0.5)
m.set_sun(longitude=40, latitude=50, ambient=(0.7, 0.7, 0.7))
files = m.save(os.path.join(os.path.dirname(__file__), "out"))
print(files)
