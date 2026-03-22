#!/usr/bin/env python3
"""
mikai_info.py — Analizzatore di dump e file vendor per tag SRIX4K (MyKey / Mikai)

Utilizzo:
    python3 mikai_info.py --dump <FILE.bin>
    python3 mikai_info.py --vendor <FILE.bin>
    python3 mikai_info.py --dump <FILE.bin> --vendor <FILE.bin>
"""

import argparse
import os
import struct
import sys

# ─── Colori ANSI ──────────────────────────────────────────────────────────────

class C:
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    DIM     = "\033[2m"
    RED     = "\033[91m"
    GREEN   = "\033[92m"
    YELLOW  = "\033[93m"
    BLUE    = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN    = "\033[96m"
    WHITE   = "\033[97m"

    @staticmethod
    def bold(s):    return f"\033[1m{s}\033[0m"
    @staticmethod
    def dim(s):     return f"\033[2m{s}\033[0m"
    @staticmethod
    def red(s):     return f"\033[91m{s}\033[0m"
    @staticmethod
    def green(s):   return f"\033[92m{s}\033[0m"
    @staticmethod
    def yellow(s):  return f"\033[93m{s}\033[0m"
    @staticmethod
    def blue(s):    return f"\033[94m{s}\033[0m"
    @staticmethod
    def magenta(s): return f"\033[95m{s}\033[0m"
    @staticmethod
    def cyan(s):    return f"\033[96m{s}\033[0m"
    @staticmethod
    def white(s):   return f"\033[97m{s}\033[0m"


def supports_color() -> bool:
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


if not supports_color():
    for _a in ["bold","dim","red","green","yellow","blue","magenta","cyan","white"]:
        setattr(C, _a, staticmethod(lambda s, *a, **k: s))
    for _a in ["RESET","BOLD","DIM","RED","GREEN","YELLOW","BLUE","MAGENTA","CYAN","WHITE"]:
        setattr(C, _a, "")


# ─── Costanti ─────────────────────────────────────────────────────────────────

SRIX4K_BLOCKS  = 128
SRIX_BLOCK_LEN = 4
SRIX4K_BYTES   = SRIX4K_BLOCKS * SRIX_BLOCK_LEN
DUMP_FILE_SIZE = 8 + SRIX4K_BYTES
COL_W          = 58

VENDOR_RESET_B18 = bytes([0x8F, 0xCD, 0x0F, 0x48])
VENDOR_RESET_B19 = bytes([0xC0, 0x82, 0x00, 0x07])

BLOCK_COLORS = {
    0x05: C.RED,
    0x06: C.YELLOW,
    0x07: C.CYAN,
    0x08: C.CYAN,
    0x18: C.MAGENTA,
    0x19: C.MAGENTA,
    0x1C: C.MAGENTA,
    0x1D: C.MAGENTA,
    0x21: C.GREEN,
    0x23: C.BLUE,
    0x25: C.GREEN,
    0x27: C.BLUE,
    **{0x34 + i: C.YELLOW for i in range(9)},
}


# ─── Crittografia ─────────────────────────────────────────────────────────────

def encode_decode_block(block: bytes) -> bytes:
    b = list(block)
    out = [0] * 4
    out[0] = ((b[0] & 0xC0)        | ((b[1] & 0xC0) >> 2) |
              ((b[2] & 0xC0) >> 4) | ((b[3] & 0xC0) >> 6))
    out[1] = (((b[0] & 0x30) << 2) |  (b[1] & 0x30)        |
              ((b[2] & 0x30) >> 2) | ((b[3] & 0x30) >> 4))
    out[2] = (((b[0] & 0x0C) << 4) | ((b[1] & 0x0C) << 2) |
               (b[2] & 0x0C)       | ((b[3] & 0x0C) >> 2))
    out[3] = (((b[0] & 0x03) << 6) | ((b[1] & 0x03) << 4) |
              ((b[2] & 0x03) << 2) |  (b[3] & 0x03))
    return bytes(out)


def calculate_block_checksum(block: bytes, block_num: int) -> bytes:
    b  = list(block)
    cs = (0xFF - block_num
          - (b[3] & 0x0F) - ((b[3] >> 4) & 0x0F)
          - (b[2] & 0x0F) - ((b[2] >> 4) & 0x0F)
          - (b[1] & 0x0F) - ((b[1] >> 4) & 0x0F)) & 0xFF
    return bytes([cs, b[1], b[2], b[3]])


def calculate_encryption_key(uid: int, eeprom: list) -> int:
    b06 = eeprom[0x06]
    otp = (((0xFF - b06[3]) << 24) | ((0xFF - b06[2]) << 16) |
           ((0xFF - b06[1]) << 8)  |  (0xFF - b06[0])) + 1
    otp &= 0xFFFFFFFF
    b18d = encode_decode_block(eeprom[0x18])
    b19d = encode_decode_block(eeprom[0x19])
    vval = (b18d[2] << 24) | (b18d[3] << 16) | (b19d[2] << 8) | b19d[3]
    mkey = (uid * ((vval + 1) & 0xFFFFFFFF)) & 0xFFFFFFFF
    return (mkey * otp) & 0xFFFFFFFF


def get_current_credit(eeprom: list, enc_key: int) -> int:
    cc = bytearray(eeprom[0x21])
    cc[0] ^= (enc_key >> 24) & 0xFF
    cc[1] ^= (enc_key >> 16) & 0xFF
    cc[2] ^= (enc_key >> 8)  & 0xFF
    cc[3] ^=  enc_key         & 0xFF
    cc = encode_decode_block(bytes(cc))
    return (cc[2] << 8) | cc[3]


def check_lock_id(eeprom: list, enc_key: int) -> bool:
    cc    = bytearray(eeprom[0x21])
    cc[0] ^= (enc_key >> 24) & 0xFF
    cc[1] ^= (enc_key >> 16) & 0xFF
    cc[2] ^= (enc_key >> 8)  & 0xFF
    cc[3] ^=  enc_key         & 0xFF
    cc    = bytearray(encode_decode_block(bytes(cc)))
    saved = cc[0]
    cc    = bytearray(calculate_block_checksum(bytes(cc), 0x21))
    return eeprom[0x05][3] == 0x7F and saved != cc[0]


def is_reset(eeprom: list) -> bool:
    return eeprom[0x18] == VENDOR_RESET_B18 and eeprom[0x19] == VENDOR_RESET_B19


def get_production_date(eeprom: list) -> tuple:
    b  = eeprom[0x08]
    dd = ((b[0] & 0xF0) >> 4) * 10 + (b[0] & 0x0F)
    mm = ((b[1] & 0xF0) >> 4) * 10 + (b[1] & 0x0F)
    yy = ((b[3] & 0x0F) * 1000 + ((b[3] & 0xF0) >> 4) * 100 +
          ((b[2] & 0xF0) >> 4) * 10 +  (b[2] & 0x0F))
    return dd, mm, yy


def get_transaction_pointer(eeprom: list) -> int:
    if eeprom[0x3C][1] == 0xFF:
        return 7
    cur = bytearray([
        eeprom[0x3C][0],
        eeprom[0x3C][1] ^ eeprom[0x07][1],
        eeprom[0x3C][2] ^ eeprom[0x07][2],
        eeprom[0x3C][3] ^ eeprom[0x07][3],
    ])
    cur = encode_decode_block(bytes(cur))
    return min(cur[1], 7)


def get_transactions(eeprom: list) -> list:
    current = get_transaction_pointer(eeprom)
    txs     = []
    for _ in range(8):
        current = 0 if current == 7 else current + 1
        eb = eeprom[0x34 + current]
        if all(b == 0xFF for b in eb):
            txs.append(None)
        else:
            td = eb[0] >> 3
            tm = ((eb[0] & 0x07) << 1) | ((eb[1] & 0x80) >> 7)
            ty = 2000 + (eb[1] & 0x7F)
            tc = (eb[2] << 8) | eb[3]
            txs.append((td, tm, ty, tc))
    return txs


def get_otp_counter(eeprom: list) -> int:
    b = eeprom[0x06]
    return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0]


def vendor_id_from_blocks(b18: bytes, b19: bytes) -> int:
    b18d = encode_decode_block(b18)
    b19d = encode_decode_block(b19)
    return (b18d[2] << 24) | (b18d[3] << 16) | (b19d[2] << 8) | b19d[3]


# ─── Parsing file ──────────────────────────────────────────────────────────────

def parse_dump(filepath: str) -> tuple:
    with open(filepath, "rb") as f:
        data = f.read()
    if len(data) == DUMP_FILE_SIZE:
        uid = struct.unpack_from("<Q", data, 0)[0]
        raw = data[8:]
    elif len(data) == SRIX4K_BYTES:
        basename = os.path.splitext(os.path.basename(filepath))[0]
        try:
            uid = int(basename, 16)
        except ValueError:
            uid = 0
        raw = data
    else:
        raise ValueError(
            f"Dimensione file non valida: {len(data)} byte "
            f"(attesi {SRIX4K_BYTES} o {DUMP_FILE_SIZE})"
        )
    eeprom = [bytes(raw[i * 4: i * 4 + 4]) for i in range(SRIX4K_BLOCKS)]
    return uid, eeprom


def parse_vendor(filepath: str) -> tuple:
    with open(filepath, "rb") as f:
        data = f.read()
    if len(data) != 8:
        raise ValueError(f"File vendor non valido: {len(data)} byte (attesi 8)")
    return bytes(data[:4]), bytes(data[4:])


# ─── UI helpers ────────────────────────────────────────────────────────────────

def hex_block(block: bytes) -> str:
    return " ".join(f"{b:02X}" for b in block)


def section(title: str):
    bar = "━" * COL_W
    print()
    print(f"{C.CYAN}{C.BOLD}┏{bar}┓{C.RESET}")
    inner = title.center(COL_W)
    print(f"{C.CYAN}{C.BOLD}┃{C.WHITE}{inner}{C.RESET}{C.CYAN}{C.BOLD}┃{C.RESET}")
    print(f"{C.CYAN}{C.BOLD}┗{bar}┛{C.RESET}")


def kv(label: str, value: str, color=None):
    lbl = f"{C.DIM}{label:<22}{C.RESET}"
    val = color(value) if color else C.white(value)
    print(f"  {lbl}  {val}")


def rule():
    print(f"  {C.DIM}{'╌' * (COL_W - 2)}{C.RESET}")


# ─── Stampa vendor ─────────────────────────────────────────────────────────────

def print_vendor_info(b18: bytes, b19: bytes, filename: str = ""):
    fname = os.path.basename(filename) if filename else ""
    section(f"  FILE VENDOR  ·  {fname}" if fname else "  FILE VENDOR")
    b18d  = encode_decode_block(b18)
    b19d  = encode_decode_block(b19)
    vid   = vendor_id_from_blocks(b18, b19)
    reset = (b18 == VENDOR_RESET_B18 and b19 == VENDOR_RESET_B19)
    kv("Blocco 0x18  raw",     hex_block(b18),  C.yellow)
    kv("Blocco 0x19  raw",     hex_block(b19),  C.yellow)
    rule()
    kv("Blocco 0x18  decodif", hex_block(b18d), C.cyan)
    kv("Blocco 0x19  decodif", hex_block(b19d), C.cyan)
    rule()
    kv("Vendor ID",  f"{vid:08X}", C.magenta)
    kv("Stato",
       "DEFAULT — reset di fabbrica" if reset else "Vendor personalizzato",
       C.red if reset else C.green)
    print()


# ─── Stampa dump ───────────────────────────────────────────────────────────────

def print_dump_info(uid: int, eeprom: list, filename: str = ""):
    enc_key    = calculate_encryption_key(uid, eeprom)
    lock       = check_lock_id(eeprom, enc_key)
    reset      = is_reset(eeprom)
    dd, mm, yy = get_production_date(eeprom)
    otp        = get_otp_counter(eeprom)
    fname      = os.path.basename(filename) if filename else ""

    section(f"  TAG SRIX4K  ·  {fname}" if fname else "  TAG SRIX4K")
    kv("UID",             f"{uid:016X}", C.magenta)
    kv("Data produzione", f"{dd:02d}/{mm:02d}/{yy:04d}", C.white)
    kv("Chiave sessione", f"{enc_key:08X}", C.cyan)
    kv("Contatore OTP",   f"{otp:08X}  ({otp})", C.white)
    kv("Lock ID",
       "ATTIVO  ⚠" if lock else "no",
       C.red if lock else C.green)
    kv("Associata a vendor",
       "no  (stato di reset)" if reset else "si",
       C.red if reset else C.green)
    if not lock and not reset:
        credit = get_current_credit(eeprom, enc_key)
        kv("Credito corrente",
           f"{credit // 100}.{credit % 100:02d} EUR",
           C.green if credit > 0 else C.yellow)
    elif reset:
        kv("Credito corrente", "n/d  (tag in reset)", C.dim)

    section("  BLOCCHI VENDOR")
    b18  = eeprom[0x18]
    b19  = eeprom[0x19]
    b18d = encode_decode_block(b18)
    b19d = encode_decode_block(b19)
    vid  = vendor_id_from_blocks(b18, b19)
    kv("Blocco 0x18  raw",     hex_block(b18),  C.yellow)
    kv("Blocco 0x19  raw",     hex_block(b19),  C.yellow)
    rule()
    kv("Blocco 0x18  decodif", hex_block(b18d), C.cyan)
    kv("Blocco 0x19  decodif", hex_block(b19d), C.cyan)
    rule()
    kv("Vendor ID",  f"{vid:08X}", C.magenta)
    kv("Stato",
       "DEFAULT — reset di fabbrica" if reset else "Vendor personalizzato",
       C.red if reset else C.green)

    section("  STORICO TRANSAZIONI")
    txs     = get_transactions(eeprom)
    has_any = False
    for i, tx in enumerate(txs):
        slot = C.dim(f"  [{i}]")
        if tx is None:
            print(f"{slot}  {C.DIM}— nessuna transazione{C.RESET}")
        else:
            td, tm, ty, tc = tx
            date_s   = C.white(f"{td:02d}/{tm:02d}/{ty:04d}")
            amount_s = C.green(f"{tc // 100}.{tc % 100:02d} EUR")
            print(f"{slot}  {date_s}    {amount_s}")
            has_any = True
    if not has_any:
        print(f"  {C.DIM}Nessuna transazione registrata.{C.RESET}")

    section("  DUMP EEPROM")
    h_addr  = C.bold(C.dim("ADDR"))
    h_hex   = C.bold(C.dim("HEX         "))
    h_ascii = C.bold(C.dim("ASCII"))
    print(f"  {h_addr}   {h_hex}   {h_ascii}")
    print(f"  {C.DIM}{'─' * COL_W}{C.RESET}")

    prev_color = "NONE"
    for i in range(SRIX4K_BLOCKS):
        blk   = eeprom[i]
        color = BLOCK_COLORS.get(i)

        if i > 0 and color != prev_color:
            print(f"  {C.DIM}{'·' * COL_W}{C.RESET}")
        prev_color = color

        addr_raw = f"[{i:02X}]"
        hex_raw  = " ".join(f"{b:02X}" for b in blk)
        ascii_s  = "".join(
            chr(b) if 0x20 <= b < 0x7F else C.dim("·") for b in blk
        )

        if color:
            addr_s = f"{color}{C.BOLD}{addr_raw}{C.RESET}"
            hex_s  = f"{color}{hex_raw}{C.RESET}"
        else:
            addr_s = C.dim(addr_raw)
            hex_s  = hex_raw

        print(f"  {addr_s}   {hex_s:<11}   {ascii_s}")

    print()
    print(f"  {C.DIM}Legenda:{C.RESET}")
    entries = [
        (C.RED,     "Lock ID 0x05"),
        (C.YELLOW,  "OTP / transazioni"),
        (C.CYAN,    "Sistema / data"),
        (C.MAGENTA, "Vendor"),
        (C.GREEN,   "Credito corrente"),
        (C.BLUE,    "Credito precedente"),
    ]
    row = "  "
    for col, lbl in entries:
        row += f"{col}{C.BOLD}■{C.RESET} {C.DIM}{lbl}{C.RESET}   "
    print(row)
    print()


# ─── Banner ────────────────────────────────────────────────────────────────────

def banner():
    art = [
        r"  ███╗   ███╗██╗██╗  ██╗ █████╗ ██╗",
        r"  ████╗ ████║██║██║ ██╔╝██╔══██╗██║",
        r"  ██╔████╔██║██║█████╔╝ ███████║██║",
        r"  ██║╚██╔╝██║██║██╔═██╗ ██╔══██║██║",
        r"  ██║ ╚═╝ ██║██║██║  ██╗██║  ██║██║",
        r"  ╚═╝     ╚═╝╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝",
    ]
    print()
    for line in art:
        print(f"{C.CYAN}{C.BOLD}{line}{C.RESET}")
    print(f"  {C.DIM}SRIX4K (MyKey) tag analyser{C.RESET}")
    print()


# ─── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Analizzatore dump e vendor per tag SRIX4K — MyKey / Mikai",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Esempi:\n"
            "  python3 mikai_info.py --dump D002AABBCCDDEEFF.bin\n"
            "  python3 mikai_info.py --vendor mikai_gestore.bin\n"
            "  python3 mikai_info.py --dump D002AABBCCDDEEFF.bin --vendor mikai_gestore.bin\n"
        ),
    )
    parser.add_argument("--dump",   metavar="FILE",
                        help="File dump  .bin (512 B o 520 B con UID)")
    parser.add_argument("--vendor", metavar="FILE",
                        help="File vendor .bin (8 B: blocco 0x18 + 0x19)")
    args = parser.parse_args()

    if not args.dump and not args.vendor:
        banner()
        parser.print_help()
        sys.exit(1)

    banner()

    if args.vendor:
        try:
            b18, b19 = parse_vendor(args.vendor)
            print_vendor_info(b18, b19, args.vendor)
        except (ValueError, OSError) as e:
            print(f"{C.red('[ERRORE]')} Vendor: {e}", file=sys.stderr)
            sys.exit(1)

    if args.dump:
        try:
            uid, eeprom = parse_dump(args.dump)
            print_dump_info(uid, eeprom, args.dump)
        except (ValueError, OSError) as e:
            print(f"{C.red('[ERRORE]')} Dump: {e}", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
