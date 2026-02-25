# Build the default (Go) implementation
all:
	$(MAKE) -C go all

# Run integration tests against an implementation
# Usage: make tests           → tests Go (default)
#        make test-go         → explicit Go
#        make test-c          → C implementation
tests: test-go

test-go: all
	XMPPD_BIN=go/xmppd USERADD_BIN=go/useradd python3 tests/run_all.py

test-c:
	XMPPD_BIN=c/xmppd USERADD_BIN=c/useradd python3 tests/run_all.py

clean:
	$(MAKE) -C go clean

.PHONY: all tests test-go test-c clean
