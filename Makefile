CC = gcc
CFLAGS = -g -Wall

program : ms2130s_patch.c
	$(CC) $(CFLAGS) -o ms2130s_patch ms2130s_patch.c
clean:
	rm -f ms2130s_patch
