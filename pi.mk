# ZunzunOS para la Raspberry Pi 2 Model B (BCM2836, Cortex-A7, ARMv7-A de 32 bits)
P     := $(B)/pi
ARMT  := -target armv7a-none-eabihf -mcpu=cortex-a7 -marm
PKCFLAGS := -target armv7a-none-eabi -mcpu=cortex-a7 -marm -mfloat-abi=soft -mfpu=none -std=gnu11 -O2 -g0 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-int-to-pointer-cast \
	-Wno-pointer-to-int-cast -ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fno-pic -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -fno-omit-frame-pointer -DZZ_TACTIL
PUCFLAGS := $(ARMT) -mfloat-abi=hard -mfpu=neon-vfpv4 -std=gnu11 -O2 -g0 -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-int-to-pointer-cast \
	-Wno-pointer-to-int-cast -ffreestanding -fno-builtin -nostdinc -fno-stack-protector -fno-pic -fno-pie \
	-fno-math-errno -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Iuser/lib

# kernel: lo comun a las dos arquitecturas + kernel/pi
PKCOMMON := lib kmem sched syscall vfs fat exfat fsro wm input blk sdhci sesion cifrado guardia net usbprn hid usbnet gpu
PKOBJ := $(P)/k/start.o $(patsubst %,$(P)/k/%.o,$(PKCOMMON)) \
	$(patsubst kernel/pi/%.c,$(P)/k/pi_%.o,$(wildcard kernel/pi/*.c)) $(P)/k/aeabi.o $(P)/k/aeabi_s.o
$(P)/k $(P)/u $(P)/a $(P)/rd $(P)/w $(P)/cb:
	mkdir -p $@
$(P)/k/start.o: kernel/pi/start.S | $(P)/k
	$(CC) $(ARMT) -c $< -o $@
$(P)/k/%.o: kernel/%.c kernel/kernel.h kernel/pi/arch.h user/lib/version.h kernel/usb.h kernel/usbnet.h | $(P)/k
	$(CC) $(PKCFLAGS) -c $< -o $@
$(P)/k/pi_%.o: kernel/pi/%.c kernel/kernel.h kernel/pi/arch.h user/lib/version.h kernel/usb.h kernel/usbnet.h | $(P)/k
	$(CC) $(PKCFLAGS) -c $< -o $@
$(P)/k/aeabi.o: user/lib/arm/aeabi.c | $(P)/k
	$(CC) $(PKCFLAGS) -c $< -o $@
$(P)/k/aeabi_s.o: user/lib/arm/aeabi_s.S | $(P)/k
	$(CC) $(ARMT) -c $< -o $@
$(P)/kernel.elf: $(PKOBJ) kernel/pi/kernel.ld
	$(LD) -T kernel/pi/kernel.ld -z noexecstack -o $@ $(PKOBJ)
$(P)/KERNEL.BIN: $(P)/kernel.elf
	llvm-objcopy -O binary $< $@ 2>/dev/null || /opt/homebrew/opt/llvm/bin/llvm-objcopy -O binary $< $@

# aplicaciones (las mismas fuentes; libz con su parte ARM en user/lib/arm)
PLOBJ := $(P)/u/crt0.o $(patsubst user/lib/%.c,$(P)/u/%.o,$(LSRC)) $(P)/u/arm_aeabi.o $(P)/u/arm_aeabi_s.o $(P)/u/arm_mates.o
$(P)/u/crt0.o: user/lib/arm/crt0.S | $(P)/u
	$(CC) $(ARMT) -c $< -o $@
$(P)/u/arm_%.o: user/lib/arm/%.c | $(P)/u
	$(CC) $(PUCFLAGS) -c $< -o $@
$(P)/u/arm_aeabi_s.o: user/lib/arm/aeabi_s.S | $(P)/u
	$(CC) $(ARMT) -c $< -o $@
$(P)/u/i18n.o: user/lib/i18n_tab.h
$(P)/u/%.o: user/lib/%.c user/lib/z.h user/lib/version.h | $(P)/u
	$(CC) $(PUCFLAGS) -c $< -o $@
$(P)/a/%.o: user/apps/%.c user/lib/z.h user/lib/version.h | $(P)/a
	$(CC) $(PUCFLAGS) -c $< -o $@
$(P)/w/%.o: user/web/%.c user/web/kernel.h user/lib/z.h | $(P)/w
	$(CC) $(PUCFLAGS) -w -Iuser/web -c $< -o $@
$(P)/cb/%.o: user/colibri/%.c user/colibri/cb.h user/colibri/cbi.h user/colibri/cbhost.h user/lib/z.h | $(P)/cb
	$(CC) $(PUCFLAGS) -w -Iuser/colibri -c $< -o $@
$(P)/w/htmljs.o: user/web/html.c user/web/dom.c user/web/domjs.h user/web/kernel.h $(B)/w/dom_js.h user/colibri/cbhost.h user/lib/z.h | $(P)/w
	$(CC) $(PUCFLAGS) -w -Iuser/web -I$(B)/w -Iuser/colibri -DWEB_JS -c $< -o $@
PWOBJ := $(patsubst %,$(P)/w/%.o,$(WSRC))
PWJOBJ := $(P)/w/htmljs.o $(P)/w/calc.o $(P)/w/text.o
PCBOBJ := $(patsubst user/colibri/%.c,$(P)/cb/%.o,$(CBSRC))
$(P)/rd/navjs: PEXTRA := $(PWJOBJ) $(PCBOBJ)
$(P)/rd/navjs: $(PWJOBJ) $(PCBOBJ)
$(P)/a/navjs.o: user/apps/browser.c user/web/domjs.h
$(patsubst %,$(P)/a/%.o,$(JSAPPS)): PUCFLAGS += -Iuser/colibri
$(patsubst %,$(P)/rd/%,$(JSAPPS)): PEXTRA := $(PCBOBJ)
$(patsubst %,$(P)/rd/%,$(JSAPPS)): $(PCBOBJ)
$(P)/rd/apps $(P)/rd/browser: PEXTRA := $(PWOBJ)
$(P)/rd/apps $(P)/rd/browser: $(PWOBJ)
$(P)/rd/%: $(P)/a/%.o $(PLOBJ) user/lib/app.ld | $(P)/rd
	$(LD) -T user/lib/app.ld --gc-sections --oformat binary -z noexecstack -o $@ $(PLOBJ) $< $(PEXTRA)
# navjs y jsconsola usan Colibri, que aun necesita su parte ARM (matematicas x87 y la recogida de basura): fuera por ahora
PI_SIN := navjs jsconsola
PAPPS := $(filter-out $(patsubst %,$(P)/rd/%,$(PI_SIN)),$(patsubst user/apps/%.c,$(P)/rd/%,$(wildcard user/apps/*.c)))

$(P)/INITRD.BIN: $(PAPPS) $(RES) tools/mkinitrd.py
	mkdir -p $(P)/inst && cp docs/LEEME.TXT $(P)/inst/
	python3 -I tools/mkinitrd.py $@ $(P)/rd user/res $(P)/inst
# kernel7.img = kernel + relleno hasta el final del BSS + cabecera "ZZINITRD" + initrd
$(P)/kernel7.img: $(P)/KERNEL.BIN $(P)/INITRD.BIN $(P)/kernel.elf tools/mkpi.py
	python3 -I tools/mkpi.py kernel7 $(P)/kernel.elf $(P)/KERNEL.BIN $(P)/INITRD.BIN $@

pi: $(P)/kernel7.img
# tarjeta SD: make pi-sd [SD_BYTES=...] (necesita los archivos del firmware en build/pi/firmware, ver docs/RASPBERRY.md)
SD_BYTES ?= 268435456
pi-sd: $(P)/kernel7.img
	python3 -I tools/mkpi.py sd $(P)/kernel7.img $(P)/firmware $(P)/sd.img $(SD_BYTES)
pi-run: $(P)/kernel7.img
	qemu-system-arm -M raspi2b -bios $< -serial stdio
.PHONY: pi pi-sd pi-run
.SECONDARY: $(PLOBJ) $(PWOBJ) $(PWJOBJ) $(PCBOBJ) $(patsubst user/apps/%.c,$(P)/a/%.o,$(wildcard user/apps/*.c))
