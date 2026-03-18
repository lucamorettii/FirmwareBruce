"""
hex_blocks4_offset.py
─────────────────────
Legge un file .bin e stampa blocchi da 4 byte (8 cifre hex)
con indice che parte da 18.

Uso:
  python hex_blocks4_offset.py file.bin
"""

import sys
from pathlib import Path


def stampa_blocchi(path: str, start_index=18):
    data = Path(path).read_bytes()

    idx = start_index
    for i in range(0, len(data), 4):
        chunk = data[i:i+4]
        hex_str = "".join(f"{b:02X}" for b in chunk)
        print(f"[{idx}]: {hex_str}")
        idx += 1


def main():
    if len(sys.argv) != 2:
        print("Uso: python hex_blocks4_offset.py file.bin")
        sys.exit(1)

    try:
        stampa_blocchi(sys.argv[1])
    except FileNotFoundError:
        print("[ERRORE] File non trovato")


if __name__ == "__main__":
    main()
