import sys, subprocess as sp, re

command = ["numactl", "--membind", "0", "--physcpubind",  "0", "mccalpin-stream" ]

copyv = []
scalev = []
addv = []
triadv = []

for i in range(4):
	command[4] = str(i * 12)
	p = sp.check_output(command)
	copy = re.search(r"Copy:[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+", p)
	scale = re.search(r"Scale:[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+", p)
	add = re.search(r"Add:[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+", p)
	triad = re.search(r"Triad:[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+([\d\.]+)[^\d]+", p)
	for j in range(4):
		copyv.append(copy.group(j+1))
		scalev.append(scale.group(j+1))
		addv.append(add.group(j+1))
		triadv.append(triad.group(j+1))

lst = ["Copy", "Scale", "Add", "Triad"]
ls2 = [copyv, scalev, addv, triadv]
out = ""
out += "All done in numanode 0\n"
outfmt = "Cpu %s, Best rate MB/s %s, Avg time %s, Min time %s, Max time %s\n"
for i in range(4):
	out += lst[i] + "\n"
	for j in range(4):
		out += outfmt % (j*12, ls2[i][j*4],ls2[i][j*4+1],ls2[i][j*4+2],ls2[i][j*4+3])

print(out)