import re
import sys
from pathlib import Path
from typing import List, Optional

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


def bytes_to_c(data: List[int]) -> str:
    return ", ".join(to_hex(x) for x in data)


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
    last_ptr = {}

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


def emit_arrays_in_order(txns: List[Txn]) -> str:
    out = []

    write_index = 0
    read_index = 0

    out.append("// Auto-generated from I2C sniff")
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("")

    out.append("// =========================")
    out.append("// Writes in sniff order")
    out.append("// =========================")
    for t in txns:
        if t.addr not in TARGET_ADDRS:
            continue
        if t.mode != "W":
            continue

        name = f"write_seq_{write_index:04d}_addr_{t.addr:02X}"
        out.append(f"// #{write_index} line {t.line_no} addr=0x{t.addr:02X} ptr={('0x%02X' % t.ptr) if t.ptr is not None else 'unknown'}")
        out.append(f"static const uint8_t {name}[{len(t.data)}] = {{ {bytes_to_c(t.data)} }};")
        out.append("")
        write_index += 1

    out.append("// =========================")
    out.append("// Reads in sniff order")
    out.append("// =========================")
    for t in txns:
        if t.addr not in TARGET_ADDRS:
            continue
        if t.mode != "R":
            continue

        name = f"read_seq_{read_index:04d}_addr_{t.addr:02X}"
        out.append(f"// #{read_index} line {t.line_no} addr=0x{t.addr:02X} ptr={('0x%02X' % t.ptr) if t.ptr is not None else 'unknown'}")
        out.append(f"static const uint8_t {name}[{len(t.data)}] = {{ {bytes_to_c(t.data)} }};")
        out.append("")
        read_index += 1

    out.append("// Optional counts")
    out.append(f"static const unsigned WRITE_SEQ_COUNT = {write_index};")
    out.append(f"static const unsigned READ_SEQ_COUNT  = {read_index};")
    out.append("")

    return "\n".join(out)


def make_report(txns: List[Txn]) -> str:
    out = []
    out.append("# I2C sniff ordered array report")
    out.append("")
    out.append(f"Total parsed transactions: {len(txns)}")
    out.append("")

    write_count = 0
    read_count = 0

    out.append("## Transactions in order")
    out.append("")
    for i, t in enumerate(txns):
        if t.addr not in TARGET_ADDRS:
            continue
        ptr_txt = ('0x%02X' % t.ptr) if t.ptr is not None else 'unknown'
        out.append(f"{i:04d}. line {t.line_no}: {t.mode} addr=0x{t.addr:02X} ptr={ptr_txt} data=[{bytes_to_c(t.data)}]")
        if t.mode == "W":
            write_count += 1
        else:
            read_count += 1

    out.append("")
    out.append(f"Writes kept: {write_count}")
    out.append(f"Reads kept:  {read_count}")
    out.append("")

    return "\n".join(out)


def main():
    if len(sys.argv) < 2:
        print("No input file was given on the command line.")
        print("Usage: python i2c_sniff_to_arrays_ordered.py <input.txt> [output_prefix]")
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

    header_path = input_path.with_name(f"{sanitize_name(prefix)}_ordered_arrays.h")
    report_path = input_path.with_name(f"{sanitize_name(prefix)}_ordered_report.txt")

    header_path.write_text(emit_arrays_in_order(txns), encoding="utf-8")
    report_path.write_text(make_report(txns), encoding="utf-8")

    print(f"Input:   {input_path}")
    print(f"Header:  {header_path}")
    print(f"Report:  {report_path}")
    print(f"Parsed transactions: {len(txns)}")


if __name__ == "__main__":
    main()