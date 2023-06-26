ifdef OS
	build = gcc -g 
	delete = del /Q
else
	ifeq ($(shell uname),Linux) 
		build = gcc -g -rdynamic -lSegFault
		delete = rm -f
	endif
endif

test : test.o reorderLib.o
		$(build) -o test test.o reorderLib.o
test.o : test.c ../reorderLib.h
		$(build) -O0 -c test.c
reorderLib.o : ../reorderLib.c ../reorderLib.h
		$(build) -O0 -c ../reorderLib.c

clean :
	$(delete) test test.exe test.o reorderLib.o
