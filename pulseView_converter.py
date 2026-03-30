import re
import sys
from pathlib import Path
from typing import List, Optional, Dict

# Supports two common PulseView text styles:
# 1) Detailed decode lines:
#    Address write: 40
#    Data write: 13
#    Data read: F0
#    Stop
#
# 2) Bubble/token lines:
#    S AR:40 DW:13 P
#    S AR:40 DR:F0 DR:01 P

TARGET_ADDRS = {0x40, 0x41}

re_addrw_a = re.compile(r"Address write:\s*([0-9A-Fa-f]{2})")
re_addrr_a = re.compile(r"Address read:\s*([0-9A-Fa-f]{2})")
re_dw_a    = re.compile(r"Data write:\s*([0-9A-Fa-f]{2})")
re_dr_a    = re.compile(r"Data read:\s*([0-9A-Fa-f]{2})")
re_stop_a  = re.compile(r"I²C: Address/data: Stop|I2C: Address/data: Stop|\bStop\b")
re_tokens_b = re.compile(r"\b(AR|AW|DR|DW):([0-9A-Fa-f]{2})\b")


def to_hex(b: int) -> str:
    return f"0x{b:02X}"


def bytes_to_c(data: List[int], per_line: int = 16, indent: str = "    ") -> str:
    if not data:
        return ""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append(indent + ", ".join(to_hex(x) for x in chunk))
    return ",\n".join(lines)


def sanitize_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


class Txn:
    def __init__(self, addr: int, mode: str, data: List[int], ptr: Optional[int], line_no: int):
        self.addr = addr
        self.mode = mode  # 'W' or 'R'
        self.data = data[:]
        self.ptr = ptr
        self.line_no = line_no


def parse_transactions(lines: List[str]) -> List[Txn]:
    txns: List[Txn] = []
    last_ptr: Dict[int, int] = {}

    cur_mode = None
    cur_addr = None
    cur_wbytes: List[int] = []
    cur_rbytes: List[int] = []
    cur_start_line = 0

    def flush_a(line_no: int):
        nonlocal cur_mode, cur_addr, cur_wbytes, cur_rbytes, cur_start_line

        if cur_addr is None:
            return

        if cur_mode == "W":
            ptr = cur_wbytes[0] if cur_wbytes else None
            if ptr is not None:
                last_ptr[cur_addr] = ptr
            txns.append(Txn(cur_addr, "W", cur_wbytes, ptr, cur_start_line or line_no))

        elif cur_mode == "R":
            ptr = last_ptr.get(cur_addr)
            txns.append(Txn(cur_addr, "R", cur_rbytes, ptr, cur_start_line or line_no))

        cur_mode = None
        cur_addr = None
        cur_wbytes = []
        cur_rbytes = []
        cur_start_line = 0

    for idx, line in enumerate(lines, start=1):
        # Bubble/token style
        tokens = re_tokens_b.findall(line)
        if tokens:
            addr = None
            wbytes: List[int] = []
            rbytes: List[int] = []
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
                    ptr = wbytes[0]
                    last_ptr[addr] = ptr
                    txns.append(Txn(addr, "W", wbytes, ptr, idx))
                if saw_read:
                    txns.append(Txn(addr, "R", rbytes, last_ptr.get(addr), idx))
            continue

        # Detailed style
        m = re_addrw_a.search(line)
        if m:
            flush_a(idx)
            cur_mode = "W"
            cur_addr = int(m.group(1), 16)
            cur_start_line = idx
            continue

        m = re_addrr_a.search(line)
        if m:
            flush_a(idx)
            cur_mode = "R"
            cur_addr = int(m.group(1), 16)
            cur_start_line = idx
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
            flush_a(idx)
            continue

    flush_a(len(lines))
    return txns


def collect_grouped_data(txns: List[Txn]):
    grouped = {
        "W": {addr: [] for addr in sorted(TARGET_ADDRS)},
        "R": {addr: [] for addr in sorted(TARGET_ADDRS)},
    }

    for t in txns:
        if t.addr not in TARGET_ADDRS:
            continue
        grouped[t.mode][t.addr].extend(t.data)

    return grouped


def emit_grouped_arrays(txns: List[Txn]) -> str:
    grouped = collect_grouped_data(txns)
    out = []

    out.append("// Auto-generated from I2C sniff")
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("")

    out.append("// ========================================")
    out.append("// Writes grouped into one array per addr")
    out.append("// ========================================")
    for addr in sorted(TARGET_ADDRS):
        data = grouped["W"][addr]
        name = f"writes_addr_{addr:02X}"
        out.append(f"static const uint8_t {name}[{len(data)}] = {{")
        if data:
            out.append(bytes_to_c(data))
        out.append("};")
        out.append(f"static const unsigned {name}_len = {len(data)};")
        out.append("")

    out.append("// ========================================")
    out.append("// Reads grouped into one array per addr")
    out.append("// ========================================")
    for addr in sorted(TARGET_ADDRS):
        data = grouped["R"][addr]
        name = f"reads_addr_{addr:02X}"
        out.append(f"static const uint8_t {name}[{len(data)}] = {{")
        if data:
            out.append(bytes_to_c(data))
        out.append("};")
        out.append(f"static const unsigned {name}_len = {len(data)};")
        out.append("")

    total_w = sum(len(grouped["W"][addr]) for addr in TARGET_ADDRS)
    total_r = sum(len(grouped["R"][addr]) for addr in TARGET_ADDRS)

    out.append("// Totals")
    out.append(f"static const unsigned TOTAL_WRITE_BYTES = {total_w};")
    out.append(f"static const unsigned TOTAL_READ_BYTES  = {total_r};")
    out.append("")

    return "\n".join(out)


def make_report(txns: List[Txn]) -> str:
    grouped = collect_grouped_data(txns)
    out = []

    out.append("# I2C sniff grouped-array report")
    out.append("")
    out.append(f"Total parsed transactions: {len(txns)}")
    out.append("")

    kept_txns = [t for t in txns if t.addr in TARGET_ADDRS]
    out.append(f"Transactions kept for target addresses: {len(kept_txns)}")
    out.append("")

    out.append("## Transaction list")
    out.append("")
    for i, t in enumerate(kept_txns):
        ptr_txt = f"0x{t.ptr:02X}" if t.ptr is not None else "unknown"
        data_txt = ", ".join(to_hex(x) for x in t.data)
        out.append(
            f"{i:04d}. line {t.line_no}: {t.mode} addr=0x{t.addr:02X} "
            f"ptr={ptr_txt} len={len(t.data)} data=[{data_txt}]"
        )

    out.append("")
    out.append("## Grouped totals")
    out.append("")
    for addr in sorted(TARGET_ADDRS):
        out.append(
            f"addr 0x{addr:02X}: "
            f"write_bytes={len(grouped['W'][addr])}, "
            f"read_bytes={len(grouped['R'][addr])}"
        )

    out.append("")
    return "\n".join(out)


def main():
    if len(sys.argv) < 2:
        print("No input file was given on the command line.")
        print("Usage: python i2c_sniff_grouped_arrays.py <input.txt> [output_prefix]")
        try:
            entered = input("Paste the sniff text file path here, then press Enter: ").strip().strip('"')
        except EOFError:
            entered = ""

        if not entered:
            print("Error: no input file path provided.")
            return

        input_path = Path(entered)
        prefix = input_path.stem
    else:
        input_path = Path(sys.argv[1])
        prefix = sys.argv[2] if len(sys.argv) >= 3 else input_path.stem

    if not input_path.exists():
        print(f"Error: file not found: {input_path}")
        return

    if not input_path.is_file():
        print(f"Error: not a file: {input_path}")
        return

    lines = input_path.read_text(errors="ignore").splitlines()
    txns = parse_transactions(lines)

    header_path = input_path.with_name(f"{sanitize_name(prefix)}_grouped_arrays.h")
    report_path = input_path.with_name(f"{sanitize_name(prefix)}_grouped_report.txt")

    header_path.write_text(emit_grouped_arrays(txns), encoding="utf-8")
    report_path.write_text(make_report(txns), encoding="utf-8")

    print(f"Input:   {input_path}")
    print(f"Header:  {header_path}")
    print(f"Report:  {report_path}")
    print(f"Parsed transactions: {len(txns)}")

    kept = [t for t in txns if t.addr in TARGET_ADDRS]
    print(f"Kept target transactions: {len(kept)}")

    grouped = collect_grouped_data(txns)
    for addr in sorted(TARGET_ADDRS):
        print(
            f"addr 0x{addr:02X}: "
            f"write_bytes={len(grouped['W'][addr])}, "
            f"read_bytes={len(grouped['R'][addr])}"
        )


if __name__ == "__main__":
    main()