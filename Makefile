CC = gcc
CFLAGS = -fPIC -Wall
LDFLAGS = -shared

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o so_thread_module.o
	$(CC) $(LDFLAGS) -o $@ $^

so_scheduler.o: so_scheduler.c
	$(CC) $(CFLAGS) -o $@ -c $<

so_thread_module.o: so_thread_module.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	-rm -f so_scheduler.o so_thread_module.o libscheduler.so
