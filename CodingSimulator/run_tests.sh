#!/bin/bash

for i in {2..2}
do
	for n in {1..10}
	do
		./sim $n $[i*5] 0
	done
	mv coding_results.txt coding_results_$[i*5]_0.txt
done
