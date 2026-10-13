ARCH ?= aarch64
LATO ?= /usr/share/fonts/truetype/lato

# aarch64 uses its own _start so the binary only needs GLIBC_2.17 (see start_aarch64.S).
START_aarch64 = -nostartfiles start_aarch64.S

portmsg.$(ARCH): portmsg.c stb_truetype.h fonts/portmsg_regular.h fonts/portmsg_bold.h $(wildcard start_$(ARCH).S)
	gcc -O2 -Wall -Wno-unused-function -o $@ $(START_$(ARCH)) portmsg.c $$(sdl2-config --cflags --libs) -lm
	strip $@

fonts/portmsg_regular.h: fonts/PortmsgSans-Regular.ttf
	( echo 'static const unsigned char portmsg_regular_ttf[] = {'; xxd -i < $<; echo '};' ) > $@

fonts/portmsg_bold.h: fonts/PortmsgSans-Bold.ttf
	( echo 'static const unsigned char portmsg_bold_ttf[] = {'; xxd -i < $<; echo '};' ) > $@

# Regenerate the committed font subsets from Lato (needs python3-fonttools).
fonts:
	python3 tools/subset_font.py $(LATO)/Lato-Regular.ttf fonts/PortmsgSans-Regular.ttf Regular
	python3 tools/subset_font.py $(LATO)/Lato-Bold.ttf fonts/PortmsgSans-Bold.ttf Bold

clean:
	rm -f portmsg.$(ARCH) fonts/*.h

.PHONY: fonts clean
