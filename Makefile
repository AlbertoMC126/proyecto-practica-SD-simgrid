BIN_FILES  =  modelo2

INSTALL_PATH = $(HOME)/simgrid-3.20

CC = gcc

CPPFLAGS = -I$(INSTALL_PATH)/include -I/usr/local/include/

NO_PRAYER_FOR_THE_WICKED =	-w -O3 -g 

LDFLAGS = -L$(INSTALL_PATH)/lib/
LDLIBS = -lm -lsimgrid -rdynamic $(INSTALL_PATH)/lib/libsimgrid.so -Wl,-rpath,$(INSTALL_PATH)/lib


all: CFLAGS=$(NO_PRAYER_FOR_THE_WICKED)
all: $(BIN_FILES) clean-obj
.PHONY : all

modelo2: modelo2.o rand.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

clean:
	rm -f $(BIN_FILES) *.o

clean-obj:
	rm -f *.o

.SUFFIXES:
.PHONY : clean
