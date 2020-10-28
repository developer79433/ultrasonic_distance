CC=gcc
CFLAGS=-g3 -Wall -Werror
LD=$(CC)
LDFLAGS=
LIBNAMES=pigpio rt
LIBS=$(addprefix -l,$(LIBNAMES))

.PHONY: all
all: ultrasonic_distance

.PHONY: clean
clean:
	rm -f *.o ultrasonic_distance

.c.o: $(CC) $(CFLAGS) -c -o "$@" "$<"

ultrasonic_distance: ultrasonic_distance.o
	$(LD) $(LDFLAGS) -o "$@" "$<" $(LIBS)
