CC            = gcc
CFLAGS        = -std=gnu99 -s -Os -D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections -Wl,--gc-sections -static
LIBS          = -lsquashfuse -lsquashfuse_ll -lzstd -lpthread -ldl

all: runtime-fuse2 runtime-fuse3

# Compile runtime
runtime-fuse2.o: runtime.c
	$(CC) -I/usr/local/include/squashfuse -I/usr/include/fuse -o runtime-fuse2.o -c $(CFLAGS) $^

runtime-fuse2: runtime-fuse2.o
	$(CC) $(CFLAGS) $^ $(LIBS) -lfuse -o runtime-fuse2

runtime-fuse3.o: runtime.c
	$(CC) -I/usr/local/include/squashfuse -I/usr/include/fuse3 -o runtime-fuse3.o -c $(CFLAGS) $^

runtime-fuse3: runtime-fuse3.o
	$(CC) $(CFLAGS) $^ $(LIBS) -lfuse3 -o runtime-fuse3

clean:
	rm -f *.o runtime-fuse2 runtime-fuse3
