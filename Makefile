
PROGS     = vidcap

CFLAGS += -Wall -g -O2 -std=c++11

PKGS    = rapp
LDFLAGS += -lrapp

CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --libs $(PKGS))

OBJS = vidcap.o

all:	$(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LIBS) $(LDLIBS) $^ $(LDFLAGS) -o $@

clean:
	rm -f $(PROGS) *.o core
	rm -f *.tar

