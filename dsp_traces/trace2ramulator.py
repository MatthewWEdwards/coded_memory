#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

if len(sys.argv) < 5:
	print("Usage: " + sys.argv[0] + " <huawei trace> <new ramulator trace> <dram|cpu> <num_repeat>")
	sys.exit(0)

input_trace = sys.argv[1]
ramulator_trace = sys.argv[2]
mode = sys.argv[3]
num_repeat = int(sys.argv[4])

repeat_offset = 0x01000000
repeat_inc = 0x00001000
repeat_ramp = 0

def write_to_file(output_f, time, addr, writeback, mode):
	if mode == "cpu":
		if writeback != "":
			output_f.write(time + " " + addr + " " + writeback + "\n")
		else:
			output_f.write(time  + " " + addr + "\n")
	else:
		if writeback != "":
			output_f.write(addr + " W\n")
		else:
			output_f.write(addr + " R\n")

with open(ramulator_trace, "wt") as output_f:
	for repeat in range(0, num_repeat):
		with open(input_trace, "rt") as input_f:
			last_time = 0
			prev_addr = "0x0"
			prev_read = False
			prev_time = 0
			for line in input_f:
				fields = line.split()
				time = int(fields[0].split(":")[1])
				address = int(fields[7].split(":")[1], 16)
				if address > 0x00a00000 and address < 0x00aa0000:
					address = address + repeat_offset + 0x4*repeat_ramp
					repeat_ramp += 8
				address = str(address)	
				is_read = fields[2].split(":")[1] == "Read"

				if is_read:
					if prev_addr == "0x0":
						prev_addr = address
					if prev_read:
						write_to_file(output_f, str(prev_time - last_time), prev_addr, "", mode)
						last_time = prev_time
					prev_addr = address
					prev_time = time
				else:
					write_to_file(output_f, str(time - last_time), address, prev_addr, mode)
					last_time = time
					#prev_addr = "0x0"
				prev_read = is_read


