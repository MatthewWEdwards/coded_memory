#!/bin/sh

sudo gdb --args ./ramulator configs/HBM-config.cfg --mode=cpu \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu0.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu1.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu2.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu3.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu4.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu5.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu6.txt \
	~/Desktop/coded_access/parsec/ramulator_results/vips_cpu7.txt \



#sudo gdb --args ./ramulator configs/HBM-config.cfg --mode=cpu \
#	../../dsp_traces/lte/dsp_0_trace.txt \
#	../../dsp_traces/lte/dsp_1_trace.txt \
#	../../dsp_traces/lte/dsp_2_trace.txt \
#	../../dsp_traces/lte/dsp_3_trace.txt \
#	../../dsp_traces/lte/dsp_4_trace.txt \
#	../../dsp_traces/lte/dsp_5_trace.txt

