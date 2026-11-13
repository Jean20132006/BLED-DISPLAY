# Host (PC) regression tests for the BLED logic: protocol, time, SD storage, link (real + simulated), case model.
# Needs only gcc + python3.   Run:  make -C test/host
SRC   = ../../src
CC    = gcc
STD   = -std=gnu17 -O2 -g
SAN   = -fsanitize=address,undefined -fno-sanitize-recover=all
# warnings equivalent to ESP-IDF 5.2 (-Werror=all) plus the strict truncation checks this project cares about
WARN  = -Wall -Werror=all -Wno-error=unused-function -Wextra -Wno-unused-parameter -Wno-sign-compare \
        -Werror=format-truncation=2 -Werror=stringop-truncation -Werror=stringop-overflow
INC   = -I$(SRC) -Istubs -I.

all: run
test_proto: test_proto.c $(SRC)/bled_proto.c
	$(CC) $(STD) $(SAN) $(WARN) $(INC) -o $@ $^
test_time: test_time.c $(SRC)/bled_time.c
	$(CC) $(STD) $(SAN) $(WARN) $(INC) -o $@ $^
test_storage: test_storage.c $(SRC)/bled_storage.c $(SRC)/bled_time.c $(SRC)/bled_proto.c
	$(CC) $(STD) $(SAN) $(WARN) $(INC) -o $@ $^
test_link_real: test_link_real.c $(SRC)/bled_link.c $(SRC)/bled_proto.c host_min.c
	$(CC) $(STD) $(SAN) $(WARN) $(INC) -DBLED_LINK_SIMULATE=0 -o $@ $^
test_core: test_core.c $(SRC)/bled_core.c $(SRC)/bled_link.c $(SRC)/bled_storage.c $(SRC)/bled_time.c $(SRC)/bled_proto.c host_min.c
	$(CC) $(STD) $(SAN) $(WARN) $(INC) -DBLED_LINK_SIMULATE=1 -o $@ $^ -lm
run: test_proto test_time test_storage test_link_real test_core
	./test_proto && ./test_time && ./test_storage 2>/dev/null && ./test_link_real 2>/dev/null && ./test_core 2>/dev/null && python3 crosscheck.py
clean:
	rm -f test_proto test_time test_storage test_link_real test_core
