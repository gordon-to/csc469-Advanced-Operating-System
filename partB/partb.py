import sys, subprocess as sp

command = ["numactl", "--membind", "0", "--physcpubind",  "36", "mccalpin-stream" ]
p = sp.check_output(command)
print(p)