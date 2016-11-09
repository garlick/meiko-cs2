L_TARGET := meiko.a

ifeq ($(CONFIG_MEIKO_BARGRAPH),y)
L_OBJS += bargraph.o
else
  ifeq ($(CONFIG_MEIKO_BARGRAPH),m)
  M_OBJS += bargraph.o
  endif
endif

ifeq ($(CONFIG_MEIKO_CAN),y)
L_OBJS += can.o
O_TARGET = can.o
O_OBJS = can_main.o can_obj.o can_console.o
else
  ifeq ($(CONFIG_MEIKO_CAN),m)
  M_OBJS += can.o 
  O_TARGET = can.o
  O_OBJS = can_main.o can_obj.o can_console.o
  endif
endif

ifeq ($(CONFIG_MEIKO_ELAN),y)
L_OBJS += elan.o
else
  ifeq ($(CONFIG_MEIKO_ELAN),m)
  M_OBJS += elan.o
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
