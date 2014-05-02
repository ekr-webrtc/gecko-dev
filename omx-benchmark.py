#!/usr/bin/env python
import argparse
import sys
import subprocess
import re
import threading
import time
import datetime

TIMEFMT = '%Y-%m-%d %H:%M:%S.%f'

DATA = {
    "inserted":0,
    "encode":0,
    "encoded":0,
    "encoded_frame":0,
    "decode":0,
    "decoded":0
}


DK = DATA.keys()

outfile = None

def reset():
    for k in DATA:
        DATA[k] = 0

def matches(st, s):
    return st.find(s) != -1


class CollectorThread(threading.Thread):
    def run(self):
        FIRST_TIME = None
        subprocess.call(["adb", "logcat", "-c"])
        self.sub = subprocess.Popen(["adb", "logcat"], stdout=subprocess.PIPE)
        self.initialized = False
        self.should_stop = False

        while True:
            l = self.sub.stdout.readline()
            if self.should_stop:
                sys.exit(0)

            do_print = False
            if matches(l, "init OMX"):
                if not self.initialized:
                    self.initialized = True
                    do_print=True

            if not self.initialized:
                continue

            if args.debug:
                debug.write(l)

            m = re.search(": (2014.*) UTC - (.*)", l)
            if m is None:
                continue
            
            ts = datetime.datetime.strptime(m.group(1), TIMEFMT)
            if FIRST_TIME is None:
                FIRST_TIME = ts
            delta = (ts - FIRST_TIME).total_seconds()

            l = m.group(2)

            if matches(l, "Inserting frame"):
                do_print = True
                DATA["inserted"] += 1
            
            if matches(l, "Encoding frame"):
                do_print = True
                DATA["encode"] += 1
            
            if matches(l, "Emit NAL"):
                do_print = True
                DATA["encoded"] += 1

            if matches(l, "Emit frame"):
                do_print = True
                DATA["encoded_frame"] += 1
        
            if matches(l, "will decode len"):
                do_print = True
                DATA["decode"] += 1
        
            if matches(l, "Decoded frame"):
                do_print = True
                DATA["decoded"] += 1
        
            if do_print:
                print l,
                print DATA
                if outfile is not None:
                    ob = []
                    for k in DK:
                        ob.append("%d"%DATA[k])
                    outfile.write("%f\t%s\n"%(delta, "\t".join(ob)))
                        
    def stop(self):
        self.should_stop = True

parser = argparse.ArgumentParser()
parser.add_argument('--no-recv', dest='no_recv', action='store_true',
                    default=False, help="Don't receive")
parser.add_argument('--out', dest="out", default=None)
parser.add_argument('--debug', dest="debug", default=None)
parser.add_argument('--frames',dest="frames", default=300, type=int)
parser.add_argument('--file',dest='file',default='/data/niklas_176x144_30.y4m')

args = parser.parse_args()

if args.out is not None:
    outfile = open(args.out, "w")
    outfile.write("TIME\t%s\n"%"\t".join(DK))

if args.debug is not None:
    debug = open(args.debug, "w")
    
thr = CollectorThread()
thr.start()    

time.sleep(2)

cargs = ["adb", "shell", "sh", "/data/b2g-benchmark.sh","-video-benchmark-file", args.file]

if not args.no_recv:
    cargs.append("-video-benchmark-receive")

cargs.append("-video-benchmark-frames")
cargs.append("%d"%args.frames)

print " ".join(cargs)

subprocess.call(cargs)

print "Waiting"

time.sleep(10);

print "Done waiting"

thr.stop()

sys.exit(0)

