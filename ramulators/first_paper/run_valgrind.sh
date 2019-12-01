valgrind --leak-check=full -v ./ramulator configs/HBM-config.cfg --mode=cpu \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu0.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu1.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu2.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu3.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu4.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu5.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu6.txt \
		~/Desktop/coded_access/parsec/ramulator_results/vips_cpu7.txt 
