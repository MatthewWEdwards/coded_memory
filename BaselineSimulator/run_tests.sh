#!/bin/bash

for i in {1..10}
do
	./sim $i "../traces/LTE/dsp_0_trace.txt"
done
for i in {1..10}
do
	./sim $i "../traces/UMTS/dsp_0_trace.txt"
done
