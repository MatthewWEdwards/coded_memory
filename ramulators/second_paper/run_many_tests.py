#! /usr/bin/python

import numpy as np
import subprocess
import sys
import os
import Queue
import thread
from time import sleep

def wait(p, q, desc):
	try: p.wait()
	finally: q.put((desc, p.pid))

def main():
	if len(sys.argv) < 2:
		print "Usage: ./run_many_traces <parsec_trace_prefix_string>"
		sys.exit(0)

	prefix = sys.argv[1]
	pids = Queue.Queue()
	cnt = 0

	rob_length = "0" 
	arch = "0"
	cmd = "./test_traces.sh --trace parsec --rob_length " + rob_length + " --arch " + arch + " --prefix " + prefix
	p = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
	thread.start_new_thread(wait, (p, pids, cmd))
	cnt += 1
	print "spawned %s: %d (%d)" % (cmd, p.pid, cnt)
	sleep(0.5) # give ramulator a moment to read the config

	rob_lengths =  map(str, [1, 8, 16, 32, 128, 1024])
	archs = map(str, [1, 2, 3, 4, 5])

	for arch in archs:
		for rob_length in rob_lengths:
			cmd = "./test_traces.sh --trace parsec --rob_length " + rob_length + " --arch " + arch + " --prefix " + prefix
			p = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
			thread.start_new_thread(wait, (p, pids, cmd))
			cnt += 1
			print "spawned %s: %d (%d)" % (cmd, p.pid, cnt)
			sleep(0.5) # give ramulator a moment to read the config

	while cnt:
		desc, p = pids.get()
		print desc, p, "finished"
		cnt -= 1

if __name__ == '__main__':
	main()

