
SRCS = $(shell egrep -l '^(main|int main)' *.c)

CMDS = $(SRCS:.c=.exe)

CDEFS    = -I. -I/c/app/Cypress/Cypress-USB-Serial/library/inc -DWIN32
CFLAGS   = $(CDEFS)
CXXFLAGS = $(CFLAGS)

LDFLAGS = -Wl,--gc-sections
LIBS = -L. -lcyusbserial

CC  = gcc -Wall -g3 -std=c99 
CXX = g++ -Wall -g3 -std=c++11
LD  = $(CXX)

.SECONDEXPANSION:
.SUFFIXES:
.SUFFIXES: .c .cpp .h .o .exe

.c.o:
	$(CC) $(CFLAGS) $($*-cflags) -MD -c $<

.cpp.o:
	$(CXX) $(CXXFLAGS) $($*-cxxflags) -MD -c $<

.o.lo:
	objcopy --redefine-sym _main=_main_$(subst -,_,$*) $< $@

.c.lh:
	echo '#include "$*.h"' > $@
	cproto -Dmain=main_$(subst -,_,$*) $(CFLAGS) -e $< >> $@

all: $(CMDS)

$(CMDS) : %.exe : %.o $$($$*-objs)
	$(LD) -o $@ $+ $(LDFLAGS) $($*-ldflags) $(LIBS)

clean:
	$(RM) *.exe *.o *.lh *.lo

distclean: clean
	$(RM) *.d *~

-include $(wildcard *.d)
