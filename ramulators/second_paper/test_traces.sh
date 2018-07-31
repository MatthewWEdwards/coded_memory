#!/bin/bash

help_message=$'Usage: ./test_traces <--trace lte|umts|parsec> <--rob_length ROB_LENGTH> <--arch 1|2|3|4> [--keep_stats] [--prefix <PARSEC_PREFIX>]\n'
help_message+=$'--trace: The traces test files to run.\n'
help_message+=$'--rob_length: Maximum size of the Re-order buffer as specified by ROB_LENGTH\n'
help_message+=$'--arch: The Parity architecture to use. numbered 1 to 4.\n'
help_message+=$'    1: 8 Parity Banks \n'
help_message+=$'    2: 4 Parity Banks \n'
help_message+=$'    3: No extra banks \n'
help_message+=$'    4: 8 Duplicate banks\n'
help_message+=$'--keep_stats: Do not delete the .stats output file from ramulator\n'
help_message+=$'--prefix: The prefix appended to the parsec trace files create by trace2ramulator. Only valid with the \"parsec\" options selected'
	
stats_name="HBM.stats"

# Grab arguments
trace=""
rob_length="0"
arch="0"
stats="0"
prefix=""
while test $# -gt 0; do
	case "$1" in
		--trace)
			shift
			trace=$1
			shift
			;;
		--rob_length)
			shift
			rob_length=$1
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
		--prefix)
			shift
			prefix=$1
			shift
			;;
		--help)
			printf '%s\n' "$help_message"
			exit
			;;
		*)
			printf "$1 is an unrecognized flag"
			printf $help_message
			exit
			;;
	esac
done
		
# Verify arguments
if  [[ $trace != "lte" && $trace != "umts" && $trace != "parsec" ]] 
then
	printf '%s\n' "$help_message"
	exit 
fi

if  [[ -z $arch  || -z $rob_length ]]
then
	printf '%s\n' "$help_message"
	exit 
fi

# Set rob_length and arch
sed -i "18s/.*/ memory_coding = $arch/" configs/HBM-config.cfg
sed -i "19s/.*/ rob_length = $rob_length/" configs/HBM-config.cfg

# Run simulation
if [[ $trace == "lte" ]] || [[ $trace == "umts" ]];
then
	./ramulator configs/HBM-config.cfg --mode=cpu \
		dsp_traces/${trace}/dsp_0_trace.txt \
		dsp_traces/${trace}/dsp_1_trace.txt \
		dsp_traces/${trace}/dsp_2_trace.txt \
		dsp_traces/${trace}/dsp_3_trace.txt \
		dsp_traces/${trace}/dsp_4_trace.txt \
		dsp_traces/${trace}/dsp_5_trace.txt

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
			echo "File: $file"
		fi
	done

	stats_name="HBM_${arch}_${rob_length}.stats"
	./ramulator configs/HBM-config.cfg --mode=cpu --stats $stats_name $trace_list
fi

# Write results to file
cycles="$(grep cpu_cycles $stats_name | sed 's/[a-Z]*//g' | sed 's/#*//g' | sed 's/\._//g' | sed 's/ //g') "
echo cpu_cycles: $cycles
append="$rob_length,$cycles"

echo $append >> "results/${trace}_${arch}.csv"

if [[ $stats == "1" ]]
then
	exit
fi
rm  $stats_name


