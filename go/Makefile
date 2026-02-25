XMPPD   = ./xmppd
USERADD = ./useradd

all: $(XMPPD) $(USERADD)

$(XMPPD):
	go build -o $(XMPPD) .

$(USERADD):
	go build -o $(USERADD) ./cmd/useradd

clean:
	rm -f $(XMPPD) $(USERADD)

.PHONY: all clean
