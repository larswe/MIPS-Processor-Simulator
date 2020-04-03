# MIPS-Processor-Simulator
11/2019 - Simulator for a MIPS Processor 

In your terminal, run
gcc -o mipssim mipssim.c memoryhierarchy.c -std=gnu99 -lm 
to create a mipssim executable file. This may vary depending on your platform. 

To run the program, use e.g.
./mipssim [n] memfile-simple.txt regfile.txt,
where [n] is the size of the cache you would like the simulated processor to use, 
where memfile-simple.txt contains instructions in binary, and
where regfile.txt contains the values of the processor's registers before the execution. 
