
help_message="Useage: ./run_gdb.sh <vips|lte>"

if [[ $1 == "vips" ]]
then
	sudo gdb --args ./ramulator configs/HBM-config.cfg --mode=cpu \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu0.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu1.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu2.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu3.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu4.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu5.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu6.txt \
		~/Desktop/coded_access_project/parsec_traces/vips_cpu7.txt 
	exit
fi


if [[ $1 == "lte" ]]
then
	sudo gdb --args ./ramulator configs/HBM-config.cfg --mode=cpu \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_0_trace.txt \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_1_trace.txt \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_2_trace.txt \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_3_trace.txt \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_4_trace.txt \
		~/Desktop/coded_access_project/dsp_traces/lte/dsp_5_trace.txt 
	exit
fi

echo $help_message
