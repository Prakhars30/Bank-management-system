CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2 -g
HEADERS = $(wildcard *.h)

SERVER_OBJS = server.o seed_data.o client_handler.o auth_service.o \
              tx_manager.o record_file.o lock_manager.o util.o \
              customer_menu.o employee_menu.o manager_menu.o admin_menu.o \
              user_service.o account_service.o loan_service.o feedback_service.o

all: server client

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

client: client.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

run: server
	./server

# delete the data files so the next run seeds fresh demo data
reseed:
	rm -rf data

tests/crash_test: tests/crash_test.c record_file.o lock_manager.o tx_manager.o
	$(CC) $(CFLAGS) -o $@ $^

# smoke: every command of every role; crash: healing a dead process's transaction;
# stress: concurrent transfers plus kill -9 of the whole server
test: all tests/crash_test
	python3 tests/smoke.py
	python3 tests/crash.py

stress: all
	python3 tests/stress.py

clean:
	rm -f *.o server client tests/crash_test
