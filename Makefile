CC      = x86_64-w64-mingw32-gcc
CFLAGS  = -masm=intel -Wall -Wno-unused-function -I src/common
OUTDIR  = out

BOFS = bof_loaddriver \
       bof_killprocess \
       bof_unloaddriver \
       bof_delete \
       bof_kill_multi

all: $(OUTDIR) $(BOFS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

%: src/%.c
	$(CC) $(CFLAGS) -c $< -o $(OUTDIR)/$@.x64.o
	@echo "[+] $@.x64.o built"

clean:
	rm -rf $(OUTDIR)
	@echo "[+] Clean"

.PHONY: all clean