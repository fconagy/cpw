# This is the make file for the command utility.

all: cpw

cpw: cpw.c
	cc -Wall -o cpw cpw.c

test: cpw
	-./cpw
	-./cpw a
	-./cpw a b
	-./cpw a b c
	-./cpw s_x^xx xxxx
	./cpw s_xxx xxxx

clean:
	rm -f cpw

