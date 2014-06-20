
all:
	g++ -I. -I$(SYSTEMC_HOME)include -L. -L$(SYSTEMC_HOME)lib-linux64/ Baseline_Simulator.cpp -lsystemc -lm
