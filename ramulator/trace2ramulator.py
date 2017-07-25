#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

if len(sys.argv) < 4:
    print("Usage: " + sys.argv[0] + " <huawei trace> <new ramulator trace> dram|cpu")
    sys.exit(0)

input_trace = sys.argv[1]
ramulator_trace = sys.argv[2]
mode = sys.argv[3]

with open(input_trace, "rt") as input_f:
    with open(ramulator_trace, "wt") as output_f:
        last_time = 0
        for line in input_f:
            fields = line.split()
            time = int(fields[0].split(":")[1])
            address = fields[7].split(":")[1]
            is_read = fields[2].split(":")[1] == "Read"
            #is_write = fields[2].split(":")[1] == "Write"
            if is_read:
                if mode == "cpu":
                    output_f.write(str(time - last_time) + " " + address + "\n")
                elif mode == "dram":
                    output_f.write(address + " R\n")
                last_time = time
