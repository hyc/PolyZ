all: libpzlib.so libpzsnap.so

libpzlib.so:	pzlib.c
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $?

libpzsnap.so:	pzsnap.cc
	$(CXX) -shared -fPIC $(CFLAGS) -o $@ $?
