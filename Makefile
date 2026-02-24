XMPPD   = ./xmppd
USERADD = ./useradd

all: $(XMPPD) $(USERADD)

$(XMPPD):
	go build -o $(XMPPD) .

$(USERADD):
	go build -o $(USERADD) ./cmd/useradd

tests: all
	python3 tests/run_all.py

clean:
	rm -f $(XMPPD) $(USERADD) xmppd.log

.PHONY: all tests clean
