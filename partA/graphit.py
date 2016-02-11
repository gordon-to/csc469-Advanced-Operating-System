# Script to take output data and convert to gnuplot script
import sys

max = 0
args = sys.argv
if len(args) > 1:
	max = int(sys.argv[1])

# Set full or concatenated mode
if max:
	gp = open("./plot" + str(max) + ".gp", 'w')
	gp.write('set output "bars zoomed.png";\n')
else:
	gp = open("./plot.gp", 'w')
	gp.write('set output "bars full.png";\n')

gp.write('set title "Active and Inactive periods";\n')
gp.write('set xlabel "Time (ms)";\n')
gp.write('set nokey;\n')
gp.write('set noytics;\n')
gp.write('set terminal png size 1200,480;\n')

# Open and read from output results
results = open("./output.txt", 'r')
frequency = int(results.readline().split(' ')[0])

active = True
sum = 0
i = 1;
for line in results:
	duration = float(line.split(' ')[-2][1:])
	out = "set object " + str(i) + " rect from " + str(sum) + ", 1 to " + str(sum + duration) + ", 2"
	if active:
		out = out + ' fc rgb "blue" fs solid\n'
	else:
		out = out + ' fc rgb "red" fs solid\n'
		
	gp.write(out)
	
	i = i + 1;
	sum = sum + duration
	if active:
		active = False
	else:
		active = True
	
	if max and i > max * 2:
		break

gp.write('plot [0:' + str(sum) + '] [0:3] 0\n')

results.close()
gp.close()
