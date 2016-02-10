# Script for gathering data from context switch test

# Open and gather all data from result files

micro = [[0] for x in range(2)]		# used to contain every active/inactive time period
macro = [[0] for x in range(2)]		# used to contain only large active/inactive time periods
time = []

for i in range(2):
	micro[i].remove(0)		# Remove that initial 0
	macro[i].remove(0)
	
	results = open("./output" + str(i+1) + ".txt", 'r')
	frequency = int(results.readline().split(' ')[0])
	
	active = True
	threshold = 1	# Threshold of "large" periods (in ms)
	long_duration = 0
	for line in results:
		duration = float(line.split(' ')[-2][1:])
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

# print micro
print macro[0]
print macro[1]

print (time[0], time[1])


# also calculate the extra overhead in inactive times

# begin writing to plot_multi.gp starting with the one that starts first
