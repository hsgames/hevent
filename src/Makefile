CC=gcc
AR=ar
OPTIMIZATION?=-O2
STD=-std=c11 -pedantic
WARN=-Wall -W -Wno-missing-field-initializers
OPT=$(OPTIMIZATION)
DEBUG=-g -ggdb
FINAL_CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
FINAL_LDFLAGS=$(LDFLAGS) $(DEBUG)
FINAL_CFLAGS+=-I../include

HEVENT_LIB_NAME=libhevent.a
HEVENT_LIB_OBJ=he.o hnet.o
ECHO_NAME=echo
ECHO_OBJ=echo.o

DEP = $(HEVENT_LIB_OBJ:%.o=%.d) $(ECHO_OBJ:%.o=%.d)
-include $(DEP)

all: $(HEVENT_LIB_NAME) $(ECHO_NAME)
	@echo "hevent make success"

.PHONY: all

$(HEVENT_LIB_NAME): $(HEVENT_LIB_OBJ)
	$(AR) rcs $@ $^

$(ECHO_NAME): $(ECHO_OBJ) $(HEVENT_LIB_NAME)
	$(CC) $(FINAL_LDFLAGS) -o $@ $^ $(HEVENT_LIB_NAME)

%.o: %.c
	$(CC) $(FINAL_CFLAGS) -c $*.c -o $*.o
	$(CC) $(FINAL_CFLAGS) -MM $*.c > $*.d

clean:
	rm -rf $(HEVENT_LIB_NAME) $(ECHO_NAME) *.o *.d

.PHONY: clean
