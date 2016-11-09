CFLAGS =	-D__KERNEL__  -DMODULE -DVERBOSE
#CFLAGS +=	-I../../include
CFLAGS +=	-Wall -Wstrict-prototypes 
CFLAGS +=	-O2 -fomit-frame-pointer -fno-strict-aliasing -m32 -pipe 
CFLAGS +=	-mno-fpu -fcall-used-g5 -fcall-used-g7

CAN_OBJ =	can_main.o can_obj.o can_console.o

all: bargraph.o elan.o can.o

bargraph.o: bargraph.c

elan.o: elan.c

can.o: $(CAN_OBJ)
	ld -r -o $@ $(CAN_OBJ)

inst:
	cp ../../include/asm-sparc/meiko/*.h /usr/include/asm/meiko

clean:
	rm -f *.o
