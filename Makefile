ifdef OS
	build = gcc -shared -o reorderLib.dll -fPIC reorderLib.c
	delete = del /Q
else
	ifeq ($(shell uname),Linux) 
		build = gcc -fPIC -shared -o reorderLib.so reorderLib.c
		delete = rm -f
	endif
endif

all :
	$(build)

clean :
	$(delete) reorderLib.dll reorderLib.so reorderLib.o

