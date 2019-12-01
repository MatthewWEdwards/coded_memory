#!/usr/bin/python 
# -*- coding: utf-8 -*-

import sys
import os
import numpy as np
import matplotlib.pyplot as plt

# ensure proper input
num_trace_files = len(sys.argv)
if num_trace_files < 2:
	print "Usage: " + sys.argv[0] + " <lte|umts>"
	sys.exit(0)

# grab file names
files = []
path = "./" + sys.argv[1]
for i in os.listdir(path):
	if os.path.isfile(os.path.join(path, i)) and "dsp" in i:
		files.append(os.path.join(path, i))

colors = ['r', 'g', 'b', 'pink', 'cyan', 'lime', 'yellow', 'navy']
legend = np.ndarray((0,))
for file_num in range(0, len(files)):
	legend = np.append(legend, sys.argv[1] + "_" + str(file_num))
	with open(files[file_num], "rt") as input_f:
		# get length
		length = 0
		for line in input_f:
			length += 1

		# get data
		data = np.ndarray((length,2))
		time = 0
		length = 0
		input_f = open(files[file_num], "rt") 
		for line in input_f:
			fields = line.split()
			time += int(fields[0])/2 # in nanoseconds
			address = int(fields[1])
			line_data = np.array([[time, address]])
			data[length, 0] = time
			data[length, 1] = address
			length += 1
		plt.scatter(data[:, 0], data[:, 1], color=colors[file_num % 8], marker='x')

plt.xlabel('Time in nanoseconds')
plt.ylabel('Address')
plt.title(sys.argv[1] + " Memory Trace")
plt.legend(legend)
plt.show()
