CFLAGS += 	-Wall
BINFILES =	cansnoop canctrl cancon canping canhb canwhack candebug 
BINFILES +=	testclock testled
MAN8FILES = 	canping.8 cansnoop.8 cancon.8 canctrl.8

MAN8DIR =	/usr/local/man/man8
BINDIR =	/usr/local/bin

all: $(BINFILES)

$(BINFILES): libcan.a

cansnoop: cansnoop.o
	$(CC) $(CFLAGS) -o $@ cansnoop.o -L. -lcan

canhb: canhb.o
	$(CC) $(CFLAGS) -o $@ canhb.o -L. -lcan

canctrl: canctrl.o
	$(CC) $(CFLAGS) -o $@ canctrl.o -L. -lcan

canping: canping.o 
	$(CC) $(CFLAGS) -o $@ canping.o -L. -lcan -lpthread

cancon: cancon.o
	$(CC) $(CFLAGS) -o $@ cancon.o -L. -lcan -lpthread

testclock.s: testclock.c
	$(CC) -S $(CFLAGS) testclock.c

libcan.a: can.o
	rm -f $@
	ar crv $@ can.o

clean:
	rm -f *.o $(BINFILES) libcan.a

install: $(ALL)
	install -m 555 -o root -g bin cansnoop $(BINDIR)
	install -m 555 -o root -g bin canctrl $(BINDIR)
	install -m 555 -o root -g bin cancon $(BINDIR)
	install -m 555 -o root -g bin canping $(BINDIR)
	install -m 555 -o root -g bin canhb $(BINDIR)
	install -m 644 -o root -g bin canping.8 $(MAN8DIR)
	install -m 644 -o root -g bin cansnoop.8 $(MAN8DIR)
	install -m 644 -o root -g bin cancon.8 $(MAN8DIR)
	install -m 644 -o root -g bin canctrl.8 $(MAN8DIR)
