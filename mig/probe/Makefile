.PHONY: all
all: mig server

logger.o: logger.c
	gcc -c $<

message_queue.o: message_queue.c message_queue.h
	gcc -c $<

main.o: main.c logger.h
	gcc -c $<

mig: main.o logger.o
	gcc -o $@ $^

server.o: server.c logger.h message_queue.h
	gcc -c $<

server: server.o logger.o message_queue.o
	gcc -o $@ $^

.PHONY: clean
clean:
	@echo Cleanup...
	@rm -vf mig server *.o
