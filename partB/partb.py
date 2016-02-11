import sys, subprocess as sp, re
import matplotlib.pyplot as plt
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

x = ["Best rate MB/s", "Avg time", "Min time", "Max time"]
x1 = [i*12 for i in range(4)]
xnum = [i for i in range(4)]

def get_vals(i):
	return ([float(copyv[j*4 + i]) for j in range(4)], [float(triadv[j*4 + i]) for j in range(4)], [float(addv[j*4 + i]) for j in range(4)], [float(scalev[j*4 + i])for j in range(4)])

cnum, tnum, anum, snum = get_vals(0)

plt.figure(0)
ax = plt.subplot(111)
a = ax.bar(xnum, cnum,width=0.1,color='b')
b = ax.bar([i + 0.25 for i in xnum], snum, width=0.1,color='g')
c = ax.bar([i + 0.5 for i in xnum], anum ,width=0.1,color='r')
d = ax.bar([i + 0.75 for i in xnum], tnum , width=0.1, color='y')
plt.xticks(xnum,x1)
plt.legend()
plt.ylabel(x[0])
plt.xlabel("CPU core")
plt.legend((a,b,c,d),lst)
ax.autoscale(tight=True)

plt.figure(1)
ax1 = plt.subplot(111)
cnum1, tnum1, anum1, snum1 = get_vals(1)
a1 = ax1.bar(xnum, cnum1,width=0.1,color='b')
b1 = ax1.bar([i + 0.25 for i in xnum], snum1, width=0.1,color='g')
c1 = ax1.bar([i + 0.5 for i in xnum], anum1,width=0.1,color='r')
d1 = ax1.bar([i + 0.75 for i in xnum], tnum1, width=0.1, color='y')
plt.xticks(xnum,x1)
plt.legend()
plt.ylabel(x[1])
plt.xlabel("CPU core")
plt.legend((a,b,c,d),lst)
ax1.autoscale(tight=True)

plt.figure(2)
ax2 = plt.subplot(111)
cnum2, tnum2, anum2, snum2 = get_vals(2)
a2 = ax2.bar(xnum, cnum2,width=0.2,color='b')
b2 = ax2.bar([i + 0.25 for i in xnum], snum2, width=0.1,color='g')
c2 = ax2.bar([i + 0.5 for i in xnum], anum2,width=0.1,color='r')
d2 = ax2.bar([i + 0.75 for i in xnum], tnum2, width=0.1, color='y')
plt.xticks(xnum,x1)
plt.legend()
plt.ylabel(x[2])
plt.xlabel("CPU core")
plt.legend((a,b,c,d),lst)
ax2.autoscale(tight=True)

plt.figure(3)
ax3 = plt.subplot(111)
cnum3, tnum3, anum3, snum3 = get_vals(3)
a3 = ax3.bar(xnum, cnum3,width=0.1,color='b')
b3 = ax3.bar([i + 0.25 for i in xnum], snum3, width=0.1,color='g')
c3 = ax3.bar([i + 0.5 for i in xnum], anum3,width=0.1,color='r')
d3 = ax3.bar([i + 0.75 for i in xnum], tnum3, width=0.1, color='y')
plt.xticks(xnum,x1)
plt.legend()
plt.ylabel(x[3])
plt.xlabel("CPU core")
plt.legend((a,b,c,d),lst)
ax3.autoscale(tight=True)
plt.show()

print(out)
