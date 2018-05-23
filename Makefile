CFLAGS?=-Wall
LDFLAGS?=-static
pm2105:	pm2105.o devtag-allinone.o
install:	pm2105
	strip pm2105
	mkdir -p $(DESTDIR)/$(PREFIX)/bin
	cp -p pm2105 $(DESTDIR)/$(PREFIX)/bin
clean:
	rm -f *.o pm2105

