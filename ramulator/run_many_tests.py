#! /usr/bin/python

import numpy as np
import subprocess
import sys
import os
import Queue
import thread
from time import sleep


def truncate(f, n):
    '''Truncates/pads a float f to n decimal places without rounding'''
    s = '{}'.format(f)
    if 'e' in s or 'E' in s:
        return '{0:.{1}f}'.format(f, n)
    i, p, d = s.partition('.')
    return '.'.join([i, (d+'0'*n)[:n]])

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

	alpha = "0" 
	arch = "0"
	cmd = "./test_traces.sh --trace parsec --alpha " + alpha + " --arch " + arch + " --prefix " + prefix
	p = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
	thread.start_new_thread(wait, (p, pids, cmd))
	cnt += 1
	print "spawned %s: %d (%d)" % (cmd, p.pid, cnt)
	sleep(0.5) # give ramulator a moment to read the config

	alphas =  map(str, [.05, .1, .2, .5, 1])
	archs = map(str, [1, 2, 3])

	for arch in archs:
		for idx in range(0, len(alphas)):
			alpha = truncate(alphas[idx], 8)
			cmd = "./test_traces.sh --trace parsec --alpha " + alpha + " --arch " + arch + " --prefix " + prefix
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

