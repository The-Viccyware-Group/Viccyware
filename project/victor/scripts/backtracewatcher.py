#!/usr/bin/env python3
import re, subprocess, sys, os
import paramiko
from pathlib import Path

LIBDIR = "./_build/vicos/Release/lib"
BINDIR = "./_build/vicos/Release/bin"
SYSROOT_LIBDIR = str(Path.home() / ".anki/vicos-sdk/dist/5.3.0-r07/sysroot/usr/lib")

with open("./robot_ip.txt") as f:
    ROBOT_IP = f.read().strip()
SSH_KEY = "./robot_sshkey"
SSH_USER = "root"

bt_re = re.compile(
    r"#(?P<frame>\d+)\s+pc\s+0x(?P<pc>[0-9a-f]+)\s+\((?P<lib>.+?)\s+\+\s+0x(?P<off>[0-9a-f]+)\)"
)

BOLD_LBLUE = "\033[1;94m"
BOLD_RED = "\033[1;91m"
RESET = "\033[0m"

IGNORE_FILES = {
    "tombstoneHooks.cpp"
}

SEARCH_DIRS = [LIBDIR, BINDIR, SYSROOT_LIBDIR]

def map_path(libpath):
    name = os.path.basename(libpath)
    for d in SEARCH_DIRS:
        candidate = os.path.join(d, name)
        candidate_full = candidate + ".full"
        if os.path.exists(candidate_full):
            return candidate_full
        if os.path.exists(candidate):
            return candidate
        for f in os.listdir(d):
            if f.startswith(name):
                return os.path.join(d, f)
    return None


def find_symbol_addr(symbol):
    for d in SEARCH_DIRS:
        for fname in os.listdir(d):
            path = os.path.join(d, fname)
            if not os.path.exists(path):
                continue
            try:
                out = subprocess.check_output(
                    ["nm", "--defined-only", path],
                    stderr=subprocess.DEVNULL
                ).decode(errors="replace")
                for l in out.splitlines():
                    if symbol in l:
                        addr = l.split()[0]
                        return path, addr
            except subprocess.CalledProcessError:
                continue
    return None, None

def symbolize(line):
    m = bt_re.search(line)
    local = None
    off = None

    if m:
        libpath = m.group("lib").strip()
        off = m.group("off")
        local = map_path(libpath)
    else:
        sym_match = re.search(r"(_ZN\w+)", line)
        if sym_match:
            symbol = sym_match.group(1)
            local, off = find_symbol_addr(symbol)

    if not local:
        return line.strip() + "    [no full binary found]"

    try:
        if off:
            out = subprocess.check_output(
                ["addr2line", "-e", local, "-f", "-p", "0x" + off],
                stderr=subprocess.DEVNULL
            ).decode().strip()
        else:
            out = "[symbol not found in binary]"

        if " at " in out:
            func, fileline = out.split(" at ", 1)
            if any(ignore in fileline for ignore in IGNORE_FILES):
                return None
            return f"{line.strip()}\n{func}\n{BOLD_LBLUE}{fileline}{RESET}"
        else:
            return f"{line.strip()}\n{out}"
    except subprocess.CalledProcessError:
        return line.strip() + "    [addr2line error]"

def main():
    key = paramiko.RSAKey.from_private_key_file(SSH_KEY)
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ROBOT_IP, username=SSH_USER, pkey=key)

    transport = ssh.get_transport()
    channel = transport.open_session()
    channel.exec_command("tail -F /var/log/messages")

    buf = b""
    in_backtrace = False

    while True:
        if channel.recv_ready():
            chunk = channel.recv(4096)
            if not chunk:
                break
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                sline = line.decode("utf-8", errors="replace")

                if "***********************************************" in sline:
                    in_backtrace = not in_backtrace
                    if in_backtrace:
                        print(f"{BOLD_RED}BACKTRACE:") 
                        print(f"{BOLD_RED}{'*'*50}{RESET}") 
                    else:
                        print(f"{BOLD_RED}{'*'*50}{RESET}")
                    continue

                if in_backtrace:
                    if "#0" in sline or "#1" in sline or "#2" in sline:
                        result = symbolize(sline)
                        if result:
                            print(result)
                    elif "#0" not in sline:
                        result = symbolize(sline)
                        if result:
                            print(result)
        else:
            channel.recv_ready()

if __name__ == "__main__":
    main()
