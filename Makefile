LDFLAGS := -levent -ldb-4.8
CFLAGS := -Wall
redirector: redirector.o

clean:
	rm -f redirector.db *.o redirector

test_data.db: test_data
	db4.8_load -f $< -T -t hash $@

test: test_data.db
	@echo "Run './redirector -p 8080 -f test_data.db' and open 'http://127.0.0.1:8080' in your browser"

.PHONY: clean test
