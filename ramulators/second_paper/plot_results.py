#!/usr/bin/python

import os
import sys

import matplotlib.pyplot as plot
import matplotlib.patches as patch
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
baseline = pd.read_csv(files_to_plot[0], header=None, names=["rob_length", "cpu_cycles"])
del files_to_plot[0]

fig, ax1 = plot.subplots()
ax1.set_xlabel("Parity Scheme")
ax1.set_ylabel("Cpu Cycles Relative to Baseline")

colors_bars = [(1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1), (0, 1, 1, 1)]
x_axis_labels = ["8-banks 4-message symbols", "4-banks 4-message symbols", "No parities", "8-bank duplicate", "12-bank 2-message symbols"]

bar_positions = np.array([-.05, 0, .05, .1])
file_num = 1

for file_num, file_path in files_to_plot.iteritems():
	# Import data
	data = pd.read_csv(file_path, header=None, names=["rob_length", "cpu_cycles"])
	data = data.sort_values("rob_length")
	data["cpu_cycles_norm"] = data["cpu_cycles"] / baseline["cpu_cycles"][0]
	ax1.bar(bar_positions + file_num, data["cpu_cycles_norm"], width=0.05, color=colors_bars)

	file_num += 1

legend_patches = np.array([])
for color in colors_bars:
	legend_patches = np.append(legend_patches, patch.Patch(color=color))

ax1.legend(legend_patches, ["ROB length 8", "ROB length 32", "ROB length 128", "ROB length 1024"])
plot.xticks([1,2,3,4,5], x_axis_labels)
plot.title("Coding Scheme Performance on \"" + results_dir + "\" Benchmark")
plot.show()


