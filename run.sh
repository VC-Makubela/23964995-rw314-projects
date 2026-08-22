#echo "Arguments: threads, process file, scheduler = 0 (priority) | 1 (RR) | 2 (FCFS),  time quantum = %d"

# echo "./schedule_processes data/process1.list 0 2 (quantum -- ignored) "
#./schedule_processes 1 data/process1.list 0 2

#echo "./schedule_processes data/process1.list 1 2 "
#./schedule_processes 1 data/process1.list 1 2

# echo "./schedule_processes data/process1.list 2 2 (quantum ignored) "
./schedule_processes 1 data/process1.list 2 2
