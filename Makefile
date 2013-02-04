all: ftjrev
ftjrev: ftjrev.c
	gcc -Wall -O2 -o ftjrev ftjrev.c -lftdi
