#!/bin/bash

if [ "$#" -eq 0 ];then
	echo "runtest usage :"
	echo "./runtest.sh 1"
	echo "where 1 is number of traces you want to run "
	exit 0
fi

TRACENAME=("LTE" "UMTS" "case4" "trace1" "trace2" "trace3" "trace4")

for ((j=0; j < $1 ; j ++))
    do
        for n in {1..10}
        do
	    ./sim $n "../traces/"${TRACENAME[j]}"/dsp_0_trace.txt"
    done
done
