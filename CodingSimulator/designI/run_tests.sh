#!/bin/bash

if [ "$#" -eq 0 ];then
	echo "runtest usage : "
	echo "./runtest.sh 1"
	echo "where 1 is number of traces you want to run "
	exit 0
fi

#TRACENAME=( "LTE" "UMTS" "trace1" "trace2" "trace3" "trace4" "case4")
TRACENAME=( "case4")

for ((j=0; j < $1 ; j ++))
    do
    for i in {2..2}	
	do
        for n in {1..1}
        do
	    ./sim $n $[i*5] 0 "../../traces/"${TRACENAME[j]}"/dsp_0_trace.txt"
        done
    done
    mv coding_results.txt designI_coding_results_${TRACENAME[j]}.txt
done

