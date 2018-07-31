#!/usr/bin/python

import os
import sys

import matplotlib.pyplot as plot
import numpy as np
import pandas as pd

def print_help():
    print("Usage: " + sys.argv[0] + " <results_directory>")

# Grab parameters
if len(sys.argv) < 2:
	print_help()
	sys.exit(0)

# Verify parameters
results_dir = sys.argv[1]
if not os.path.isdir(results_dir):
	print("ERROR: " + results_dir + " not found")
	print_help()
	sys.exit(0)

# Grab files to plot
test_num = 0
files_to_plot = {}
while(True):
	file_name = results_dir + "/parsec_" + str(test_num) + ".csv"
	if os.path.isfile(file_name):
		files_to_plot[test_num] = file_name
		test_num += 1
	else:
		break

# Import data and plot
baseline = pd.read_csv(files_to_plot[0], header=None, names=["alphas", "cpu_cycles", "region_switches"])
del files_to_plot[0]

fig, ax1 = plot.subplots()
ax1.set_xlabel("alpha")
ax1.set_ylabel("Cpu Cycles Relative to Baseline")
ax2 = ax1.twinx()
ax2.set_ylabel("Average Region Switches Per Channel")

transparency = .3
colors_lines = [(1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1)]
colors_bars  = [(1, 0, 0, transparency), (0, 1, 0, transparency), (0, 0, 1, transparency)]

bar_offset = -.01
color_idx = 0
for file_num, file_path in files_to_plot.iteritems():
	# Import data
	data = pd.read_csv(file_path, header=None, names=["alphas", "cpu_cycles", "region_switches"])
	data["cpu_cycles_norm"] = data["cpu_cycles"] / baseline["cpu_cycles"][0]

	ax2.bar(data["alphas"] + bar_offset, data["region_switches"], width=0.01, color=colors_bars[color_idx])
	bar_offset += .01
	color_idx += 1

color_idx = 0
for file_num, file_path in files_to_plot.iteritems():
	# Import data
	data = pd.read_csv(file_path, header=None, names=["alphas", "cpu_cycles", "region_switches"])
	to_insert = []
	to_insert.insert(0, {'alphas' : 0, 'cpu_cycles' : baseline["cpu_cycles"][0], 'region_switches' : 0})
	data = pd.concat([pd.DataFrame(to_insert), data], ignore_index=True)
	data["cpu_cycles_norm"] = data["cpu_cycles"] / baseline["cpu_cycles"][0]

	# Plot
	ax1.plot(data["alphas"], data["cpu_cycles_norm"], color=colors_lines[color_idx])
	color_idx += 1

ax1.legend(["Scheme 1 Cycles", "Scheme 2 Cycles", "Scheme 3 Cycles"], bbox_to_anchor=(1,1))
ax2.legend(["Scheme 1 Switches", "Scheme 2 Switches", "Scheme 3 Switches"], bbox_to_anchor=(1, .8))
plot.xticks(data["alphas"], data["alphas"])
plot.title("Coding Scheme Performance on \"" + results_dir + "\" Benchmark")
plot.show()


