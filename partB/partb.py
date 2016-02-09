import sys, subprocess

commmand = ["numactl", "--membind", "0", "--physcpubind",  "36", "mccalpin-stream" ]
p = subprocess.check_output(command, shell=True)
print(p)