"""
mifare_dump_reader.py
─────────────────────
Legge e visualizza un file dump MIFARE (.bin) creato da TessereLogic.

Formato file (little-endian):
  4  byte  → magic "MFDR"
  1  byte  → version
  1  byte  → uidLen
  7  byte  → uid (padded a 7 byte)
  1  byte  → sak
  2  byte  → atqa
  1  byte  → numSectors
  240 byte → keyA[40][6]
  40  byte → keyAFound[40]
  240 byte → keyB[40][6]
  40  byte → keyBFound[40]
  256 byte → blockRead[256]
  4096 byte → data[256][16]

Uso:
  python mifare_dump_reader.py <file.bin>
  python mifare_dump_reader.py <file.bin> --raw       # mostra anche blocchi non letti
  python mifare_dump_reader.py <file.bin> --sector 3  # mostra solo un settore
"""

import sys
import struct
import argparse
from pathlib import Path

# ─── Costanti formato ─────────────────────────────────────────────────────────

DUMP_MAGIC      = b"MFDR"
DUMP_VERSION    = 1
MAX_BLOCKS      = 256
MAX_SECTORS     = 40
BLOCK_SIZE      = 16
KEY_SIZE        = 6

# ─── Mapping SAK → tipo tag ───────────────────────────────────────────────────

SAK_TYPES = {
    0x08: "MIFARE Classic 1K",
    0x18: "MIFARE Classic 4K",
    0x09: "MIFARE Mini",
    0x00: "MIFARE Ultralight",
    0x28: "MIFARE Classic 1K (SmartMX)",
    0x38: "MIFARE Classic 4K (SmartMX)",
}

# ─── Helpers struttura tag ────────────────────────────────────────────────────

def get_sector_count(sak: int) -> int:
    if sak == 0x18: return 40  # 4K
    if sak == 0x09: return 5   # Mini
    return 16                  # 1K default

def first_block(sector: int) -> int:
    if sector < 32: return sector * 4
    return 32 * 4 + (sector - 32) * 16

def blocks_in_sector(sector: int) -> int:
    return 4 if sector < 32 else 16

def trailer_block(sector: int) -> int:
    if sector < 32: return sector * 4 + 3
    return 32 * 4 + (sector - 32) * 16 + 15

# ─── Parsing ─────────────────────────────────────────────────────────────────

def parse_dump(path: str) -> dict:
    """Legge e valida il file .bin, restituisce un dizionario con tutti i dati."""
    data = Path(path).read_bytes()
    offset = 0

    def read(n):
        nonlocal offset
        chunk = data[offset:offset + n]
        offset += n
        return chunk

    # ── Header ────────────────────────────────────────────────────────────────
    magic = read(4)
    if magic != DUMP_MAGIC:
        raise ValueError(f"File non valido: magic '{magic}' != 'MFDR'")

    version = read(1)[0]
    if version != DUMP_VERSION:
        raise ValueError(f"Versione non supportata: {version}")

    uid_len    = read(1)[0]
    uid        = read(7)[:uid_len]
    sak        = read(1)[0]
    atqa       = struct.unpack_from("<H", read(2))[0]
    num_sectors = read(1)[0]

    # ── Chiavi ────────────────────────────────────────────────────────────────
    key_a_raw   = read(MAX_SECTORS * KEY_SIZE)
    key_a_found = list(read(MAX_SECTORS))
    key_b_raw   = read(MAX_SECTORS * KEY_SIZE)
    key_b_found = list(read(MAX_SECTORS))

    key_a = [key_a_raw[i*6:(i+1)*6] for i in range(MAX_SECTORS)]
    key_b = [key_b_raw[i*6:(i+1)*6] for i in range(MAX_SECTORS)]

    # ── Dati EEPROM ───────────────────────────────────────────────────────────
    block_read = list(read(MAX_BLOCKS))
    blocks_raw = read(MAX_BLOCKS * BLOCK_SIZE)
    blocks     = [blocks_raw[i*16:(i+1)*16] for i in range(MAX_BLOCKS)]

    return {
        "uid":         uid,
        "uid_hex":     uid.hex().upper(),
        "sak":         sak,
        "atqa":        atqa,
        "tag_type":    SAK_TYPES.get(sak, f"Unknown (SAK: {sak:02X})"),
        "num_sectors": num_sectors,
        "key_a":       key_a,
        "key_a_found": key_a_found,
        "key_b":       key_b,
        "key_b_found": key_b_found,
        "block_read":  block_read,
        "blocks":      blocks,
    }

# ─── Visualizzazione ─────────────────────────────────────────────────────────

def fmt_hex(b: bytes) -> str:
    """Formatta bytes come stringa hex con spazi (es. AA BB CC)."""
    return " ".join(f"{x:02X}" for x in b)

def fmt_ascii(b: bytes) -> str:
    """Formatta bytes come stringa ASCII (sostituisce non-stampabili con '.')."""
    return "".join(chr(x) if 32 <= x < 127 else "." for x in b)

def print_header(dump: dict):
    """Stampa le informazioni di intestazione del tag."""
    print("=" * 60)
    print("  MIFARE DUMP READER")
    print("=" * 60)
    print(f"  UID:      {dump['uid_hex']}")
    print(f"  SAK:      0x{dump['sak']:02X}")
    print(f"  ATQA:     0x{dump['atqa']:04X}")
    print(f"  Tipo:     {dump['tag_type']}")
    print(f"  Settori:  {dump['num_sectors']}")

    # Conta blocchi letti
    total_blocks = first_block(dump['num_sectors'] - 1) + blocks_in_sector(dump['num_sectors'] - 1)
    read_count   = sum(1 for i in range(total_blocks) if dump['block_read'][i])
    print(f"  Blocchi:  {read_count}/{total_blocks} letti")
    print("=" * 60)

def print_sector(dump: dict, sector: int, show_unread: bool = False):
    """Stampa un singolo settore con blocchi, chiavi e dati."""
    first  = first_block(sector)
    count  = blocks_in_sector(sector)
    trail  = trailer_block(sector)

    # Intestazione settore con chiavi
    key_a_str = fmt_hex(dump['key_a'][sector]) if dump['key_a_found'][sector] else "?? ?? ?? ?? ?? ??"
    key_b_str = fmt_hex(dump['key_b'][sector]) if dump['key_b_found'][sector] else "?? ?? ?? ?? ?? ??"
    ka_ok = "✓" if dump['key_a_found'][sector] else "✗"
    kb_ok = "✓" if dump['key_b_found'][sector] else "✗"

    print(f"\n┌─ Settore {sector:02d} {'─' * 38}")
    print(f"│  Key A [{ka_ok}]: {key_a_str}")
    print(f"│  Key B [{kb_ok}]: {key_b_str}")
    print(f"├{'─' * 55}")

    for b in range(count):
        block_num = first + b
        is_read   = dump['block_read'][block_num]
        is_trail  = (block_num == trail)
        label     = " [TRAILER]" if is_trail else ""
        label    += " [B0-MFG]"  if block_num == 0 else ""

        if not is_read:
            if show_unread:
                print(f"│  [{block_num:03d}] ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??  ????????????????{label}")
            else:
                print(f"│  [{block_num:03d}] <non letto>{label}")
            continue

        raw   = dump['blocks'][block_num]
        hex_s = fmt_hex(raw)
        asc_s = fmt_ascii(raw)
        print(f"│  [{block_num:03d}] {hex_s}  {asc_s}{label}")

    print(f"└{'─' * 55}")

def print_keys_summary(dump: dict):
    """Stampa un riepilogo compatto di tutte le chiavi trovate."""
    print("\n" + "=" * 60)
    print("  RIEPILOGO CHIAVI")
    print("=" * 60)
    print(f"  {'Settore':<8} {'Key A':<20} {'Key B':<20}")
    print(f"  {'-'*7:<8} {'-'*18:<20} {'-'*18:<20}")
    for s in range(dump['num_sectors']):
        ka = fmt_hex(dump['key_a'][s]) if dump['key_a_found'][s] else "non trovata  "
        kb = fmt_hex(dump['key_b'][s]) if dump['key_b_found'][s] else "non trovata  "
        print(f"  {s:<8} {ka:<20} {kb:<20}")

# ─── Entry point ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visualizza un dump MIFARE .bin creato da TessereLogic"
    )
    parser.add_argument("file",              help="Percorso del file .bin")
    parser.add_argument("--raw",             action="store_true",
                        help="Mostra anche i blocchi non letti come '??'")
    parser.add_argument("--sector", "-s",   type=int, default=None,
                        help="Mostra solo il settore specificato")
    parser.add_argument("--keys",   "-k",   action="store_true",
                        help="Mostra solo il riepilogo delle chiavi")
    args = parser.parse_args()

    try:
        dump = parse_dump(args.file)
    except (ValueError, FileNotFoundError) as e:
        print(f"[ERRORE] {e}")
        sys.exit(1)

    print_header(dump)

    if args.keys:
        print_keys_summary(dump)
        return

    if args.sector is not None:
        if args.sector < 0 or args.sector >= dump['num_sectors']:
            print(f"[ERRORE] Settore {args.sector} non valido (0-{dump['num_sectors']-1})")
            sys.exit(1)
        print_sector(dump, args.sector, args.raw)
    else:
        for s in range(dump['num_sectors']):
            print_sector(dump, s, args.raw)
        print_keys_summary(dump)

if __name__ == "__main__":
    main()
