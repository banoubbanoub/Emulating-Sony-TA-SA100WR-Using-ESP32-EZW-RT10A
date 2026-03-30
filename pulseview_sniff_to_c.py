import re
from pathlib import Path
from collections import defaultdict

INPUT_FILE = r"C:\Users\pc\Desktop\pairing successful ta-sa100wr with dav-hdx589w.txt" # change if needed
TARGET_ADDRS = {0x40, 0x41}

# -------- Format A (PulseView detailed text export) --------
re_addrw_a = re.compile(r"Address write:\s*([0-9A-Fa-f]{2})")
re_addrr_a = re.compile(r"Address read:\s*([0-9A-Fa-f]{2})")
re_dw_a    = re.compile(r"Data write:\s*([0-9A-Fa-f]{2})")
re_dr_a    = re.compile(r"Data read:\s*([0-9A-Fa-f]{2})")
re_stop_a  = re.compile(r"I²C: Address/data: Stop")

# -------- Format B (PulseView “bubble label” export / copy) --------
# Examples:
#   S  AR:40  DW:13  P
#   S  AR:40  DR:50  DR:F0  P
# Some exports may use "AW" or "AR" both for address phase; we accept AR:
re_tokens_b = re.compile(r"\b(AR|AW|DR|DW):([0-9A-Fa-f]{2})\b")

def to_hex(b: int) -> str:
    return f"0x{b:02X}"

def emit_c_array(name: str, blocks: list[list[int]]):
    if not blocks:
        return f"// {name}: (no blocks captured)\n"
    max_len = max(len(b) for b in blocks)
    out = []
    out.append(f"static const uint8_t {name}[{len(blocks)}][{max_len}] = {{")
    for blk in blocks:
        padded = blk + [blk[-1]] * (max_len - len(blk))  # pad with last byte
        out.append("  { " + ", ".join(to_hex(x) for x in padded) + " },")
    out.append("};\n")
    return "\n".join(out)

def main():
    text = Path(INPUT_FILE).read_text(errors="ignore").splitlines()

    last_ptr = {}  # addr -> last pointer
    reads = defaultdict(list)  # (addr, ptr) -> [ [readbytes], ... ]

    # Current transaction (format A)
    cur_mode = None   # "W" or "R"
    cur_addr = None
    cur_wbytes = []
    cur_rbytes = []

    def flush_a():
        nonlocal cur_mode, cur_addr, cur_wbytes, cur_rbytes
        if cur_addr is None:
            return
        if cur_mode == "W":
            if cur_wbytes:
                last_ptr[cur_addr] = cur_wbytes[0]
        elif cur_mode == "R":
            ptr = last_ptr.get(cur_addr, None)
            if ptr is not None and cur_rbytes:
                reads[(cur_addr, ptr)].append(cur_rbytes.copy())
        cur_mode = None
        cur_addr = None
        cur_wbytes.clear()
        cur_rbytes.clear()

    for line in text:
        # ---- Try Format B first: tokenized “AR:xx DW:yy DR:zz” lines ----
        tokens = re_tokens_b.findall(line)
        if tokens:
            # Parse one “bubble” line as a complete mini-transaction.
            addr = None
            wbytes = []
            rbytes = []
            saw_read = False

            for kind, hh in tokens:
                val = int(hh, 16)
                if kind in ("AR", "AW"):
                    addr = val
                elif kind == "DW":
                    wbytes.append(val)
                elif kind == "DR":
                    rbytes.append(val)
                    saw_read = True

            if addr is not None:
                if wbytes:
                    last_ptr[addr] = wbytes[0]
                if saw_read:
                    ptr = last_ptr.get(addr, None)
                    if ptr is not None and rbytes:
                        reads[(addr, ptr)].append(rbytes)
            continue

        # ---- Format A: detailed multi-line decode ----
        m = re_addrw_a.search(line)
        if m:
            cur_mode = "W"
            cur_addr = int(m.group(1), 16)
            continue

        m = re_addrr_a.search(line)
        if m:
            cur_mode = "R"
            cur_addr = int(m.group(1), 16)
            continue

        m = re_dw_a.search(line)
        if m and cur_mode == "W" and cur_addr is not None:
            cur_wbytes.append(int(m.group(1), 16))
            continue

        m = re_dr_a.search(line)
        if m and cur_mode == "R" and cur_addr is not None:
            cur_rbytes.append(int(m.group(1), 16))
            continue

        if re_stop_a.search(line):
            flush_a()
            continue

    # Emit header
    c_out = []
    c_out.append("// ===== Auto-generated from PulseView I2C sniff =====\n")

    for (addr, ptr), blocks in sorted(reads.items()):
        if addr not in TARGET_ADDRS:
            continue
        name = f"reply_{addr:02X}_ptr{ptr:02X}"
        c_out.append(emit_c_array(name, blocks))

    out_path = Path("sniff_replies.h")
    out_path.write_text("\n".join(c_out))
    print(f"Generated: {out_path.resolve()}")

    print("\nSummary:")
    for (addr, ptr), blocks in sorted(reads.items()):
        if addr not in TARGET_ADDRS:
            continue
        lens = sorted({len(b) for b in blocks})
        print(f"  addr 0x{addr:02X} ptr 0x{ptr:02X}: {len(blocks)} blocks, lengths={lens}")

if __name__ == "__main__":
    main()