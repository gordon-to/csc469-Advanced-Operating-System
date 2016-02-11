# Script for gathering data from context switch test

micro = [[0] for x in range(2)]		# used to contain every active/inactive time period
macro = [[0] for x in range(2)]		# used to contain only large active/inactive time periods
active = [None] * 2
long_dur = [None] * 2
start = [None] * 2
threshold = 2800000	# Threshold of "large" periods (in cycles)
proc = 0

for i in range(2):
	micro[i].remove(0)		# Remove that initial 0
	macro[i].remove(0)
	active[i] = True
	long_dur[i] = 0

results = open("./output.txt", 'r')
frequency = int(results.readline().split(' ')[0])

for line in results:
	if line.split(' ')[0] == "Parent":
		proc = 0
	elif line.split(' ')[0] == "Child":
		proc = 1
	else:
		continue
	
	duration = int(line.split(' ')[7])
	micro[proc].append(duration)
	long_dur[proc] = long_dur[proc] + duration
	if not start[proc]:
		start[proc] = int(line.split(' ')[5][:-1])
	
	if active[proc]:
		active[proc] = False
	else:
		if duration > threshold:
			# Push the previous active duration into macro and also add
			# current inactive duration
			macro[proc].append(long_dur[proc])
			macro[proc].append(duration)
			
			# Reset the duration counter
			long_dur[proc] = 0
		
		active[proc] = True
	
results.close()

'''
for i in range(2):
	micro[i].remove(0)		# Remove that initial 0
	macro[i].remove(0)
	
	# Open and gather all data from result files
	results = open("./output" + str(i+1) + ".txt", 'r')
	
	active = True
	threshold = 2800000	# Threshold of "large" periods (in cycles)
	long_duration = 0
	for line in results:
		duration = float(line.split(' ')[5])
		micro[i].append(duration)
		long_duration = long_duration + duration
		
		if active:
			active = False
		else:
			if duration > threshold:
				# Push the previous active duration into macro and also add
				# current inactive duration
				macro[i].append(long_duration)
				macro[i].append(duration)
				
				# Reset the duration counter
				long_duration = 0
			
			active = True
	
	results.close()
'''

#print micro
#print macro[0]
#print macro[1]


# also calculate the extra overhead in inactive times

# begin writing to plot_multi.gp starting with the one that starts first
gp = open("./plot_multi.gp", 'w')
gp.write('set output "bars multi.png";\n')
gp.write('set title "Active and Inactive periods";\n')
gp.write('set xlabel "Time (ms)";\n')
gp.write('set nokey;\n')
gp.write('set noytics;\n')
gp.write('set terminal png size 1200,720;\n')

k = 1
for i in range(2):
	sum = start[i]
	active = True
	for dur in micro[i]:
		out = "set object " + str(k) + " rect from " + str(sum) + ", " + str(0.5 + i * 2) + " to " + str(sum + dur) + ", " + str(1.5 + i * 2)
		if active:
			out = out + ' fc rgb "blue" fs solid\n'
		else:
			out = out + ' fc rgb "red" fs solid\n'
		
		gp.write(out)
		
		k = k + 1;
		sum = sum + dur
		if active:
			active = False
		else:
			active = True
		
gp.write('plot [0:' + str(sum) + '] [0:4] 0\n')
gp.close()
