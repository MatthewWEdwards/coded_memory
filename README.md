#README# 

This repository contains the systemC implementation of coding algorithms to increase the data redundancy in the memory systems. 

#Code Structure #

Directories : 
1. BaselineSimulator/ : This folder contains baselineSimulator.cpp . There is a makefile which compiles the code and produces the file `sim`. The file run_tests.sh runs the file `sim` for various arguments. 
2. CodingSimulator/ : This folder contains codingSimulator.cpp There is a makefile which compiles the code and produces the file `sim`. The file run_tests.sh runs the file `sim` for various arguments. 
3. results/ : This is a repo of the results. 

Result Format : 

The baseline_results.txt or coding_results.txt generally looks like : 
1       0.063377        0.063188        0.007589        125676
which has the following meaning : 
Clock Ratio  |  Critical Read Latency |  Trans Read Latency |  Write Latency | Trace Execution Time
    1        |     0.063377           |     0.063188        |     0.007589   |      125676



#Contributors # 
Dr. Sriram Vishwanath  (president@saltare-systems.com )
Ankit Singh Rawat  (president@saltare-systems.com )
Casen William Hunger (casen@saltare-systems.com )
Hardik Jain  (hardik@saltare-systems.com )

