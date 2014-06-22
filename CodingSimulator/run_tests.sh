#!/bin/bash

for i in {0..5}
do
	for n in {1..10}
	do
		./sim $n $[i*5]
	done
	mv coding_results.txt coding_results_$[i*5].txt
done
