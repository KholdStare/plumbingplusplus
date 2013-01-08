PROGS=plumbing_test test_suite

CC=g++-4.7

DEBUGFLAGS= -g -pg
CFLAGS= -Wall -Werror -std=c++0x $(DEBUGFLAGS)

LIBS= -lpthread -lboost_unit_test_framework

all: $(PROGS)
 
%: %.cpp
	$(CC) $(CFLAGS) -o $@ $^ $(INC_DIRS:%=-I%) $(LIB_DIRS:%=-L%) $(LIBS)

.PHONY: clean

clean:
	rm $(PROGS)
