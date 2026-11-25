# ZunzunOS - requiere clang, ld.lld, lld-link, python3 y qemu
CC    := clang
LD    := ld.lld
B     := build
QEMU  := qemu-system-x86_64
OVMF  := /opt/homebrew/share/qemu/edk2-x86_64-code.fd
KCFLAGS := -target x86_64-unknown-none-elf -std=gnu11 -O2 -g0 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fno-pic -fno-pie \
	-mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
	-fno-asynchronous-unwind-tables -fno-omit-frame-pointer
UCFLAGS := -target x86_64-unknown-none-elf -std=gnu11 -O2 -g0 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fno-pic -fno-pie \
	-mcmodel=small -msse2 -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Iuser/lib

KSRC := $(wildcard kernel/*.c)
KOBJ := $(B)/k/start.o $(patsubst kernel/%.c,$(B)/k/%.o,$(KSRC))
LSRC := $(wildcard user/lib/*.c)
LOBJ := $(B)/u/crt0.o $(patsubst user/lib/%.c,$(B)/u/%.o,$(LSRC))
APPS := $(patsubst user/apps/%.c,$(B)/rd/%,$(wildcard user/apps/*.c))
RES  := $(wildcard user/res/*)

all: $(B)/zunzun.img

$(B)/k $(B)/u $(B)/a $(B)/rd:
	mkdir -p $@

$(B)/boot.bin: boot/bios.S | $(B)/k
	$(CC) -target i386-unknown-none-elf -c $< -o $(B)/k/bios.o
	$(LD) -m elf_i386 -Ttext 0x7C00 --image-base=0 --oformat binary -e _start -o $@ $(B)/k/bios.o
	@test $$(wc -c < $@) -le 12800 || (echo "cargador demasiado grande"; exit 1)

$(B)/BOOTX64.EFI: boot/uefi.c | $(B)/k
	$(CC) -target x86_64-unknown-windows -std=gnu11 -O2 -ffreestanding -fno-builtin \
	  -fshort-wchar -mno-red-zone -mno-stack-arg-probe -fno-stack-protector -c $< -o $(B)/k/uefi.obj
	lld-link /nologo /subsystem:efi_application /entry:efi_main /nodefaultlib \
	  /out:$@ $(B)/k/uefi.obj

$(B)/k/start.o: kernel/start.S | $(B)/k
	$(CC) -target x86_64-unknown-none-elf -c $< -o $@
$(B)/k/%.o: kernel/%.c kernel/kernel.h user/lib/version.h | $(B)/k
	$(CC) $(KCFLAGS) -c $< -o $@
$(B)/KERNEL.BIN: $(KOBJ) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld --oformat binary -z noexecstack -o $@ $(KOBJ)

$(B)/u/crt0.o: user/lib/crt0.S | $(B)/u
	$(CC) -target x86_64-unknown-none-elf -c $< -o $@
$(B)/u/i18n.o: user/lib/i18n_tab.h
$(B)/u/%.o: user/lib/%.c user/lib/z.h user/lib/version.h | $(B)/u
	$(CC) $(UCFLAGS) -c $< -o $@
$(B)/a/%.o: user/apps/%.c user/lib/z.h user/lib/version.h | $(B)/a
	$(CC) $(UCFLAGS) -c $< -o $@
WSRC := html calc text
WOBJ := $(patsubst %,$(B)/w/%.o,$(WSRC))
$(B)/w:
	mkdir -p $@
$(B)/w/%.o: user/web/%.c user/web/kernel.h user/lib/z.h | $(B)/w
	$(CC) $(UCFLAGS) -w -Iuser/web -c $< -o $@
# Colibri (user/colibri): el motor JavaScript propio de ZunzunOS; solo lo enlazan los programas que lo usan
CBSRC := $(wildcard user/colibri/cb_*.c) user/colibri/cbhost.c
CBOBJ := $(patsubst user/colibri/%.c,$(B)/cb/%.o,$(CBSRC))
$(B)/cb:
	mkdir -p $@
$(B)/cb/%.o: user/colibri/%.c user/colibri/cb.h user/colibri/cbi.h user/colibri/cbhost.h user/lib/z.h | $(B)/cb
	$(CC) $(UCFLAGS) -w -Iuser/colibri -c $< -o $@
# navegador con JavaScript: el mismo motor HTML compilado con el DOM (user/web/dom.c y dom.js)
$(B)/w/dom_js.h: user/web/dom.js tools/js2h.py | $(B)/w
	python3 -I tools/js2h.py $< $@ DOM_JS
$(B)/w/htmljs.o: user/web/html.c user/web/dom.c user/web/domjs.h user/web/kernel.h $(B)/w/dom_js.h user/colibri/cbhost.h user/lib/z.h | $(B)/w
	$(CC) $(UCFLAGS) -w -Iuser/web -I$(B)/w -Iuser/colibri -DWEB_JS -c $< -o $@
WJOBJ := $(B)/w/htmljs.o $(B)/w/calc.o $(B)/w/text.o
$(B)/rd/navjs: EXTRA := $(WJOBJ) $(CBOBJ)
$(B)/rd/navjs: $(WJOBJ) $(CBOBJ)
$(B)/a/navjs.o: user/apps/browser.c user/web/domjs.h
JSAPPS := jsconsola
$(patsubst %,$(B)/a/%.o,$(JSAPPS)): UCFLAGS += -Iuser/colibri
$(patsubst %,$(B)/rd/%,$(JSAPPS)): EXTRA := $(CBOBJ)
$(patsubst %,$(B)/rd/%,$(JSAPPS)): $(CBOBJ)
$(B)/rd/apps: EXTRA := $(WOBJ)
$(B)/rd/apps: $(WOBJ)
$(B)/rd/browser: EXTRA := $(WOBJ)
$(B)/rd/browser: $(WOBJ)
$(B)/rd/%: $(B)/a/%.o $(LOBJ) user/lib/app.ld | $(B)/rd
	$(LD) -T user/lib/app.ld --gc-sections --oformat binary -z noexecstack -o $@ $(LOBJ) $< $(EXTRA)

$(B)/INITRD.BIN: $(APPS) $(RES) tools/mkinitrd.py $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN
	mkdir -p $(B)/inst && cp $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN docs/LEEME.TXT $(B)/inst/
	python3 -I tools/mkinitrd.py $@ $(B)/rd user/res $(B)/inst

$(B)/zunzun.img: $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN $(B)/INITRD.BIN tools/mkimage.py docs/LEEME.TXT
	python3 -I tools/mkimage.py $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN $(B)/INITRD.BIN docs/LEEME.TXT $@
	@truncate -s 256m $@

# Imagen para una memoria concreta:  make usb-img DISK_BYTES=4026531840 [LABEL="TYNI CORE"] [DOCS=carpeta]
LABEL ?= ZUNZUN
usb-img: $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN $(B)/INITRD.BIN
	python3 -I tools/mkimage.py $(B)/boot.bin $(B)/BOOTX64.EFI $(B)/KERNEL.BIN $(B)/INITRD.BIN docs/LEEME.TXT $(B)/usb.img --disk-bytes $(DISK_BYTES) --label "$(LABEL)" $(if $(DOCS),--docs "$(DOCS)")

BASE := -m 512 -rtc base=localtime -serial stdio -device VGA,vgamem_mb=32
run: $(B)/zunzun.img
	$(QEMU) $(BASE) -drive format=raw,file=$<
run-uefi: $(B)/zunzun.img
	$(QEMU) $(BASE) -drive if=pflash,format=raw,readonly=on,file=$(OVMF) -drive format=raw,file=$<

clean:
	rm -rf $(B)
.PHONY: all run run-uefi clean usb-img
