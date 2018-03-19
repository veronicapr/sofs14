CC = gcc
CFLAGS = -Wall -I "../debugging" -I "../rawIO14" -I "../sofs14"

TARGET = showblock_sofs14

SUFFIX = $(shell getconf LONG_BIT)

LIBS += -lrawIO14
LIBS += -ldebugging

LFLAGS = -L "../../lib" $(LIBS)

OBJS = sofs_blockviews.o

all:		$(TARGET)

$(TARGET):	$(TARGET).o $(OBJS)
			$(CC) -o $@ $^ $(LFLAGS)
			cp $@ ../../run
			rm -f $^ $@

clean:
			rm -f $(TARGET) $(TARGET).o $(OBJS)
			rm -f ../../run/$(TARGET)

