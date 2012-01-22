kernel: src/kernel.cc src/kernel.h
	g++ -o kernel src/kernel.cc -lev -leio -lv8 -g

clean:
	rm kernel
