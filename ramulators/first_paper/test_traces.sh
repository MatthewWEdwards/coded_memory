#!/bin/bash

help_message="Useage: ./test_traces <--trace lte|umts|parsec> <--alpha ALPHA> <--arch PARITY_ARCHITECTURE> [--keep_stats] [--prefix <PARSEC_PREFIX>] [--discard_results]"

stats_name="HBM.stats"

### Grab arguments ###
trace=""
alpha="0"
arch="0"
stats="0"
prefix=""
discard_results=0
while test $# -gt 0; do
	case "$1" in
		--trace)
			shift
			trace=$1
			shift
			;;
		--alpha)
			shift
			alpha=$1
			shift
			;;
		--arch)
			shift
			arch=$1
			shift
			;;
		--keep_stats)
			stats=1
			shift
			;;
		--discard_results)
			discard_results=1
			shift
			;;
		--prefix)
			shift
			prefix=$1
			shift
			;;
		*)
			echo "$1 is an unrecognized flag"
			echo $help_message
			exit
			;;
	esac
done
		
### Verify arguments ###
if  [[ $trace != "lte" && $trace != "umts" && $trace != "parsec" ]] 
then
	echo $help_message
	exit 
fi

if  [[ -z $arch  || -z $alpha ]]
then
	echo $help_message
	exit 
fi

### Set alpha and arch ###
sed -i "18s/.*/ memory_coding = $arch/" configs/HBM-config.cfg
sed -i "19s/.*/ alpha = $alpha/" configs/HBM-config.cfg

### Run simulation ###
if [[ $trace == "lte" ]] || [[ $trace == "umts" ]];
then
	stats_name="results/HBM_${arch}_${alpha}.stats"
	./ramulator configs/HBM-config.cfg --mode=cpu --stats $stats_name \
		../../dsp_traces/${trace}/dsp_0_trace.txt \
		../../dsp_traces/${trace}/dsp_1_trace.txt \
		../../dsp_traces/${trace}/dsp_2_trace.txt \
		../../dsp_traces/${trace}/dsp_3_trace.txt \
		../../dsp_traces/${trace}/dsp_4_trace.txt \
		../../dsp_traces/${trace}/dsp_5_trace.txt \
		../../dsp_traces/${trace}/dsp_0_trace.txt \
		../../dsp_traces/${trace}/dsp_1_trace.txt \
		../../dsp_traces/${trace}/dsp_2_trace.txt \
		../../dsp_traces/${trace}/dsp_3_trace.txt \
		../../dsp_traces/${trace}/dsp_4_trace.txt \
		../../dsp_traces/${trace}/dsp_5_trace.txt
fi

if [[ $trace == "parsec" ]];
then
    trace_path="../../parsec_traces"
    trace_list=""
    for file in ${trace_path}/*.txt;
    do
        if [[ -f "${file}" ]] && [[ $file =~ ${prefix} ]] ;
        then
            trace_list+="${file}  "
        fi
    done

	stats_name="results/HBM_${arch}_${alpha}.stats"
    ./ramulator configs/HBM-config.cfg --mode=cpu --stats $stats_name $trace_list
fi


### Write results to file ###
if [[ $discard_results == 0 ]]
then
	## Grab cycles
	cycles="$(grep cpu_cycles $stats_name | sed 's/[a-Z]*//g' | sed 's/#*//g' | sed 's/\._//g' | sed 's/ //g') "

	## Grab switches
	switches=0
	channo=0
	while [ $channo -lt 8 ];
	do
		new_switches="$(grep -E "topology_switches_$channo" $stats_name | awk '{print $2}' )"
		switches=$((switches + new_switches))
		let channo=channo+1
	done
	switches=$(bc -l <<< "scale=3; ${switches}/8")

	## Echo results, append to output file
	echo cpu cycles: $cycles
	echo topology switches: $switches
	append="$alpha,$cycles,$switches"
fi

echo $append >> "results/${trace}_$arch.csv"

if [[ $stats == "1" ]]
then
	exit
fi
rm $stats_name


