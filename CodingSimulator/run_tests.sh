#!/bin/bash

for i in {2..2}
do
	for n in {1..10}
	do
		./sim $n $[i*5] 0 "../traces/UMTS/dsp_0_trace.txt" 
	done
done
