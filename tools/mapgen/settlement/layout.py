"""Tier 1 layout anchors (cell coords u,v). Roads, plaza and plots are fixed anchors so later tiers (Village -> Metropolis) grow from them."""
G = (78.0, 25.0)            # entrance (the original gate)
C = (78.0, 102.0)           # town centre / fountain (the original fountain)
MAIN = [(78, 6), (78, 25), (78, 32), (76.5, 44), (79, 56), (77.5, 70), (78, 84), (78, 94)]
BRANCH = {
    "west_smithy": [(69, 101), (61, 99), (53, 97)],
    "east_work": [(87, 101), (95, 98), (103, 96)],
    "north_street": [(78, 111), (77, 124), (78, 138)],
    "nw_house": [(72, 109), (65, 116), (60, 121)],
    "ne_house": [(84, 109), (91, 116), (97, 122)],
    "w_house": [(77, 58), (69, 60), (62, 60)],
    "e_house": [(78, 74), (87, 75), (94, 75)],
    "west_track": [(24, 102), (40, 102), (58, 101)],
    "east_track": [(134, 101), (118, 101), (98, 101)],
}
YARDS = [(57, 97, 8.0), (103, 95, 7.5), (60, 121, 7.5), (97, 123, 8.0), (60, 60, 7.0), (94, 75, 7.0)]
PLAZA_R = 11.0
