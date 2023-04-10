# NOTE: for this code to work, the spear analyses need to be exported as
# "Text - Resampled Frames" under File > Export Format in SPEAR.
# Also note: to avoid having way too many partials, remove some from the analysis.
# I typically use the following process:
# * Edit > Select Partials Below Threshold -50 dB > Delete
# * Edit > Select Partials Below Duration 0.1 seconds > Delete

import numpy as np
import sys

from collections import deque

if len(sys.argv) < 3 :
    print('usage: python3 spear_to_coll.py <spear-file> <output-file')
    sys.exit(1)

spear_file = sys.argv[1]
out_file = sys.argv[2]

spear_lines = open(spear_file, 'r').readlines()
total_partials = int(spear_lines[2].split()[1])
total_lines = int(spear_lines[3].split()[1])
data_lines = spear_lines[5:]
max_osces = np.max([int(line.split()[1]) for line in data_lines])

available = deque([i for i in range(max_osces)])
osc_map = {}
output_lines = []
line_num = 1

# there is often silence at the beginning
first_time = float(data_lines[0].split()[0])
num_silent = int(round(first_time / 0.01))
for i in range(num_silent) :
    output_lines.append(str(line_num) + ', ' + ' '.join(['0.'] * (2*max_osces)) + ';\n')
    line_num += 1

for line in data_lines :
    l_split = line.split()
    t = float(l_split[0])
    num_osces = int(l_split[1])
    # (index freq amp) per osc
    freqs = [0. for i in range(max_osces)]
    amps = [0. for i in range(max_osces)]
    line_osces = set() # need this to compare against map for partials that have ended

    # we actually have to first see which positions we can free up
    for i in range(2, 2+(3*num_osces), 3) :
        osc = int(l_split[i])
        line_osces.add(osc)
    for osc in list(osc_map.keys()) :
        if not osc in line_osces :
            pos = osc_map[osc]
            available.appendleft(pos)
            del osc_map[osc]

    # now we can build the line
    for i in range(2, 2+(3*num_osces), 3) :
        index = int(l_split[i])
        if index in osc_map :
            pos = osc_map[index]
        else :
            pos = available.popleft()
            osc_map[index] = pos
        freqs[pos] = float(l_split[i+1])
        amps[pos] = float(l_split[i+2])
    combine = ' '.join(['{f:.2f} {a:.5f}'.format(f=freqs[i], a=amps[i]) for i in range(max_osces)])
    out = str(line_num) + ', ' + combine + ';\n'
    output_lines.append(out)
    line_num += 1

# we will need this to kill the remaining partials when the coll is done
output_lines.append(str(line_num) + ', ' + ' '.join(['0.'] * (2*max_osces)) + ';\n')
open(out_file, 'w').writelines(output_lines)
print('num osces = {o}'.format(o=max_osces))
