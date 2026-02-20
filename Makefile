CC       = cc
CFLAGS   = -std=c11 -Wall -Wextra -pedantic -g $(shell xml2-config --cflags)
LDFLAGS  = $(shell xml2-config --libs)

SRCDIR   = src
INCDIR   = include
BUILDDIR = build

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

all: xmppd useradd

xmppd: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

useradd: tools/useradd.c
	$(CC) -std=c11 -Wall -Wextra -pedantic -g -o $@ $<

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR) xmppd useradd

.PHONY: all clean
