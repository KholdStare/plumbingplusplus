PROJ=plumbing_test

CC=g++-4.7

DEBUGFLAGS= -g -DDEBUG
CFLAGS= -Wall -Werror -DUNIX -O3 -std=c++11 $(DEBUGFLAGS)

LIBS= -lpthread
 
$(PROJ): $(PROJ).cpp
	$(CC) $(CFLAGS) -o $@ $^ $(INC_DIRS:%=-I%) $(LIB_DIRS:%=-L%) $(LIBS)

.PHONY: clean

clean:
	rm $(PROJ)
