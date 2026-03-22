#!/usr/bin/env python3
"""
leggi_dump.py
Analizza e visualizza tutte le informazioni di un dump MIFARE
nel formato prodotto dal modulo Tessere di Bruce.

Uso:
    python leggi_dump.py <file.bin>
    python leggi_dump.py <file.bin> --settore 0
    python leggi_dump.py <file.bin> --blocco 4
    python leggi_dump.py <file.bin> --microel
    python leggi_dump.py <file.bin> --raw
    python leggi_dump.py <file.bin> --esporta dump.txt
"""

import sys
import struct
import argparse
from pathlib import Path

# ─── Costanti formato dump ────────────────────────────────────────────────────

DUMP_FIRMA    = b"MFDR"
DUMP_VERSIONE = 1

MAX_BLOCCHI  = 256
MAX_SETTORI  = 40

# Offset nel file binario
OFF_FIRMA       = 0
OFF_VERSIONE    = 4
OFF_LUNG_UID    = 5
OFF_UID         = 6
OFF_SAK         = 13
OFF_ATQA        = 14
OFF_NUM_SETTORI = 16
OFF_CHIAVE_A    = 17
OFF_CHIAVE_A_OK = 17 + 240          # 40 × 6
OFF_CHIAVE_B    = OFF_CHIAVE_A_OK + 40
OFF_CHIAVE_B_OK = OFF_CHIAVE_B + 240
OFF_BLOCC_LETTO = OFF_CHIAVE_B_OK + 40
OFF_DATI        = OFF_BLOCC_LETTO + 256

DIMENSIONE_ATTESA = OFF_DATI + MAX_BLOCCHI * 16  # ~4929 byte

# ─── Tabella tipi di tag da SAK ───────────────────────────────────────────────

TIPO_DA_SAK = {
    0x08: "MIFARE Classic 1K",
    0x18: "MIFARE Classic 4K",
    0x09: "MIFARE Mini",
    0x00: "MIFARE Ultralight",
    0x28: "MIFARE Classic 1K (SmartMX)",
    0x38: "MIFARE Classic 4K (SmartMX)",
}

# ─── Colori ANSI ─────────────────────────────────────────────────────────────

class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"
    ROSSO  = "\033[91m"
    VERDE  = "\033[92m"
    GIALLO = "\033[93m"
    BLU    = "\033[94m"
    VIOLA  = "\033[95m"
    CIANO  = "\033[96m"
    BIANCO = "\033[97m"

def titolo(testo):
    larghezza = 60
    print(f"\n{C.BOLD}{C.BLU}{'─' * larghezza}{C.RESET}")
    print(f"{C.BOLD}{C.BLU}  {testo}{C.RESET}")
    print(f"{C.BOLD}{C.BLU}{'─' * larghezza}{C.RESET}")

def campo(etichetta, valore, colore=C.BIANCO):
    print(f"  {C.DIM}{etichetta:<20}{C.RESET}{colore}{valore}{C.RESET}")

def errore(msg):
    print(f"{C.ROSSO}[ERRORE] {msg}{C.RESET}", file=sys.stderr)
    sys.exit(1)

def avviso(msg):
    print(f"{C.GIALLO}[AVVISO] {msg}{C.RESET}")

# ─── Parsing del file ─────────────────────────────────────────────────────────

def leggi_file(percorso: Path) -> bytes:
    if not percorso.exists():
        errore(f"File non trovato: {percorso}")
    dati = percorso.read_bytes()
    if len(dati) < DIMENSIONE_ATTESA:
        avviso(f"File troppo corto: {len(dati)} byte (attesi {DIMENSIONE_ATTESA})")
    return dati

def valida_header(dati: bytes):
    if dati[OFF_FIRMA:OFF_FIRMA+4] != DUMP_FIRMA:
        errore(f"Firma non valida: {dati[0:4]} (attesa: {DUMP_FIRMA})")
    versione = dati[OFF_VERSIONE]
    if versione != DUMP_VERSIONE:
        errore(f"Versione non supportata: {versione} (attesa: {DUMP_VERSIONE})")

def estrai_header(dati: bytes) -> dict:
    lung_uid    = dati[OFF_LUNG_UID]
    uid         = dati[OFF_UID:OFF_UID + lung_uid]
    sak         = dati[OFF_SAK]
    atqa        = struct.unpack_from("<H", dati, OFF_ATQA)[0]
    num_settori = dati[OFF_NUM_SETTORI]
    return {
        "uid":         uid,
        "lung_uid":    lung_uid,
        "sak":         sak,
        "atqa":        atqa,
        "num_settori": num_settori,
        "tipo":        TIPO_DA_SAK.get(sak, f"Sconosciuto (SAK: 0x{sak:02X})"),
    }

def estrai_chiavi(dati: bytes) -> tuple:
    """Restituisce (chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok)."""
    chiavi_a    = [dati[OFF_CHIAVE_A    + s*6 : OFF_CHIAVE_A    + s*6 + 6] for s in range(MAX_SETTORI)]
    chiavi_a_ok = [bool(dati[OFF_CHIAVE_A_OK + s]) for s in range(MAX_SETTORI)]
    chiavi_b    = [dati[OFF_CHIAVE_B    + s*6 : OFF_CHIAVE_B    + s*6 + 6] for s in range(MAX_SETTORI)]
    chiavi_b_ok = [bool(dati[OFF_CHIAVE_B_OK + s]) for s in range(MAX_SETTORI)]
    return chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok

def estrai_blocchi(dati: bytes) -> tuple:
    """Restituisce (blocchi_letti, contenuto_blocchi)."""
    letti    = [bool(dati[OFF_BLOCC_LETTO + i]) for i in range(MAX_BLOCCHI)]
    contenuto = [dati[OFF_DATI + i*16 : OFF_DATI + i*16 + 16] for i in range(MAX_BLOCCHI)]
    return letti, contenuto

# ─── Helper struttura settori ────────────────────────────────────────────────

def blocco_trailer(settore: int) -> int:
    if settore < 32:
        return settore * 4 + 3
    return 32 * 4 + (settore - 32) * 16 + 15

def primo_blocco(settore: int) -> int:
    if settore < 32:
        return settore * 4
    return 32 * 4 + (settore - 32) * 16

def blocchi_nel_settore(settore: int) -> int:
    return 4 if settore < 32 else 16

# ─── Formattazione bytes ─────────────────────────────────────────────────────

def hex_str(b: bytes, sep=" ") -> str:
    return sep.join(f"{x:02X}" for x in b)

def ascii_str(b: bytes) -> str:
    return "".join(chr(x) if 32 <= x < 127 else "." for x in b)

def formatta_blocco(indice: int, dati: bytes, letto: bool) -> str:
    """Restituisce la riga formattata di un blocco: offset hex ascii."""
    stato = " " if letto else "?"
    hex_p = hex_str(dati)
    asc_p = ascii_str(dati)
    return f"  [{stato}] Blocco {indice:>3}  {hex_p}  |{asc_p}|"

# ─── Sezione: Riepilogo tag ───────────────────────────────────────────────────

def mostra_riepilogo(h: dict, letti: list, num_settori: int):
    titolo("RIEPILOGO TAG")
    campo("UID",         hex_str(h["uid"], ":"), C.VERDE)
    campo("Lunghezza UID", f"{h['lung_uid']} byte")
    campo("SAK",         f"0x{h['sak']:02X}")
    campo("ATQA",        f"0x{h['atqa']:04X}")
    campo("Tipo",        h["tipo"], C.CIANO)
    campo("Settori",     str(h["num_settori"]))

    # Conta i blocchi effettivamente letti
    n_letti = sum(1 for s in range(num_settori)
                  for b in range(primo_blocco(s),
                                 primo_blocco(s) + blocchi_nel_settore(s))
                  if letti[b])
    totale  = sum(blocchi_nel_settore(s) for s in range(num_settori))
    perc    = n_letti / totale * 100 if totale else 0
    colore  = C.VERDE if perc == 100 else C.GIALLO if perc > 50 else C.ROSSO
    campo("Blocchi letti", f"{n_letti}/{totale}  ({perc:.0f}%)", colore)

# ─── Sezione: Chiavi ─────────────────────────────────────────────────────────

def mostra_chiavi(chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok, num_settori: int):
    titolo("CHIAVI PER SETTORE")
    print(f"  {'Sett':>4}  {'Key A':>14}  {'Key B':>14}")
    print(f"  {'────':>4}  {'──────────────':>14}  {'──────────────':>14}")

    for s in range(num_settori):
        # Key A
        if chiavi_a_ok[s]:
            str_a = hex_str(chiavi_a[s], "")
            colore_a = C.VERDE
        else:
            str_a    = "?? ?? ?? ?? ?? ??"
            colore_a = C.DIM

        # Key B
        if chiavi_b_ok[s]:
            str_b    = hex_str(chiavi_b[s], "")
            colore_b = C.VERDE
        else:
            str_b    = "?? ?? ?? ?? ?? ??"
            colore_b = C.DIM

        print(f"  {s:>4}  {colore_a}{str_a:<14}{C.RESET}  {colore_b}{str_b:<14}{C.RESET}")

# ─── Sezione: Dump completo ───────────────────────────────────────────────────

def mostra_dump(blocchi, letti, num_settori: int, filtro_settore=None, filtro_blocco=None):
    titolo("DUMP COMPLETO")

    for s in range(num_settori):
        if filtro_settore is not None and s != filtro_settore:
            continue

        trailer_idx = blocco_trailer(s)
        primo       = primo_blocco(s)
        quanti      = blocchi_nel_settore(s)

        print(f"\n  {C.BOLD}{C.VIOLA}Settore {s}{C.RESET}")

        for i in range(quanti):
            nb = primo + i
            if filtro_blocco is not None and nb != filtro_blocco:
                continue

            riga = formatta_blocco(nb, blocchi[nb], letti[nb])

            # Il trailer ha le chiavi: evidenzialo
            if nb == trailer_idx:
                print(f"{C.GIALLO}{riga}  ← trailer{C.RESET}")
            # Il blocco 0 contiene UID e dati produttore
            elif nb == 0:
                print(f"{C.CIANO}{riga}  ← blocco 0 (UID){C.RESET}")
            elif not letti[nb]:
                print(f"{C.DIM}{riga}{C.RESET}")
            else:
                print(riga)

# ─── Sezione: Analisi trailer ────────────────────────────────────────────────

def analizza_access_bits(trailer: bytes) -> str:
    """Decodifica i 3 byte degli access bits (byte 6, 7, 8 del trailer)."""
    b6, b7, b8 = trailer[6], trailer[7], trailer[8]
    # I bit di accesso sono codificati in modo ridondante (C1,C2,C3 e i loro NOT)
    # Estrae C1x, C2x, C3x per ogni blocco del settore
    righe = []
    for blk in range(4):
        c1 = (b7 >> (4 + blk)) & 1
        c2 = (b8 >> blk) & 1
        c3 = (b8 >> (4 + blk)) & 1
        righe.append(f"    Blocco {blk}: C1={c1} C2={c2} C3={c3}")
    return "\n".join(righe)

def mostra_trailer(blocchi, letti, num_settori: int):
    titolo("ANALISI TRAILER")
    for s in range(num_settori):
        t = blocco_trailer(s)
        if not letti[t]:
            continue
        dati = blocchi[t]
        chiave_a_hex = hex_str(dati[0:6])
        chiave_b_hex = hex_str(dati[10:16])
        print(f"\n  {C.BOLD}Settore {s}{C.RESET}  (blocco trailer = {t})")
        print(f"    Key A (mascherata): {C.DIM}{chiave_a_hex}{C.RESET}")
        print(f"    Access Bits:        {C.GIALLO}{hex_str(dati[6:9])}{C.RESET}")
        print(f"    GPB:                {hex_str(dati[9:10])}")
        print(f"    Key B:              {C.VERDE}{chiave_b_hex}{C.RESET}")
        print(analizza_access_bits(dati))

# ─── Sezione: Microel ─────────────────────────────────────────────────────────

def microel_kdf(uid: bytes) -> tuple:
    """Riproduce il KDF Microel: UID → Key A → Key B."""
    xor_key = [0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9]

    # Step 1: somma mod 256, parità pari
    somma = sum(uid) % 256
    if somma % 2 == 1:
        somma += 2
    sum_hex = bytes(somma ^ xor_key[i] for i in range(6))

    # Step 2: secondo XOR in base al nibble alto del primo byte
    primo_nibble = (sum_hex[0] >> 4) & 0x0F
    if primo_nibble in (0x2, 0x3, 0xA, 0xB):
        chiave_a = bytes(0x40 ^ b for b in sum_hex)
    elif primo_nibble in (0x6, 0x7, 0xE, 0xF):
        chiave_a = bytes(0xC0 ^ b for b in sum_hex)
    else:
        chiave_a = sum_hex

    # Step 3: Key B = NOT di Key A
    chiave_b = bytes(0xFF ^ b for b in chiave_a)
    return chiave_a, chiave_b

def leggi_credito_blocco(dati: bytes) -> int:
    """Legge il credito in centesimi dal blocco dati Microel (byte 5 LSB, 6 MSB)."""
    return dati[5] | (dati[6] << 8)

def mostra_microel(h: dict, blocchi, letti):
    if h["lung_uid"] != 4:
        avviso("L'UID non è di 4 byte: questo tag potrebbe non essere Microel.")

    titolo("ANALISI MICROEL")

    uid = h["uid"]
    chiave_a, chiave_b = microel_kdf(uid)

    campo("UID",    hex_str(uid, ":"), C.VERDE)
    campo("Key A (KDF)", hex_str(chiave_a, " "), C.VERDE)
    campo("Key B (KDF)", hex_str(chiave_b, " "), C.VERDE)

    # Blocco 4: credito corrente
    if letti[4]:
        credito = leggi_credito_blocco(blocchi[4])
        euro    = credito / 100
        checksum_atteso = sum(blocchi[4][:15]) % 256
        checksum_file   = blocchi[4][15]
        ok_cs = checksum_atteso == checksum_file
        cs_col = C.VERDE if ok_cs else C.ROSSO

        campo("Credito corrente",  f"{euro:.2f} EUR  ({credito} cent)", C.CIANO)
        campo("Checksum blocco 4", f"0x{checksum_file:02X} (atteso 0x{checksum_atteso:02X})",
              C.VERDE if ok_cs else C.ROSSO)
    else:
        campo("Credito corrente", "Blocco 4 non letto", C.ROSSO)

    # Blocco 5: credito precedente
    if letti[5]:
        credito_prec = leggi_credito_blocco(blocchi[5])
        euro_prec    = credito_prec / 100
        campo("Credito precedente", f"{euro_prec:.2f} EUR  ({credito_prec} cent)", C.GIALLO)
    else:
        campo("Credito precedente", "Blocco 5 non letto", C.DIM)

    # Dettaglio struttura blocco 4 (se disponibile)
    if letti[4]:
        d = blocchi[4]
        print(f"\n  {C.BOLD}Struttura blocco 4 (raw):{C.RESET}")
        n_op   = d[0] | (d[1] << 8)
        totale = d[2] | (d[3] << 8)
        dep    = d[4]
        data_t = d[7] | (d[8]<<8) | (d[9]<<16) | (d[10]<<24)
        punti  = d[11] | (d[12] << 8)
        ultima = d[13] | (d[14] << 8)
        print(f"    N. operazione:      {n_op}")
        print(f"    Totale input:       {totale / 100:.2f} EUR")
        print(f"    Deposito:           0x{dep:02X}")
        print(f"    Credito:            {(d[5]|(d[6]<<8)) / 100:.2f} EUR")
        print(f"    Data transazione:   0x{data_t:08X}")
        print(f"    Punti fedeltà:      {punti}")
        print(f"    Ultima transazione: {ultima / 100:.2f} EUR")

# ─── Sezione: Statistiche ────────────────────────────────────────────────────

def mostra_statistiche(blocchi, letti, num_settori: int):
    titolo("STATISTICHE")
    totale_blocchi = sum(blocchi_nel_settore(s) for s in range(num_settori))
    n_letti        = sum(1 for i in range(totale_blocchi) if letti[i])
    n_zero         = sum(1 for i in range(totale_blocchi)
                        if letti[i] and all(b == 0x00 for b in blocchi[i]))
    n_ff           = sum(1 for i in range(totale_blocchi)
                        if letti[i] and all(b == 0xFF for b in blocchi[i]))
    n_dati         = n_letti - n_zero - n_ff

    campo("Totale blocchi",    str(totale_blocchi))
    campo("Letti",             f"{n_letti}  ({n_letti/totale_blocchi*100:.0f}%)", C.VERDE)
    campo("Non letti",         str(totale_blocchi - n_letti), C.ROSSO)
    campo("Blocchi tutti 0x00", str(n_zero), C.DIM)
    campo("Blocchi tutti 0xFF", str(n_ff),   C.DIM)
    campo("Con dati reali",    str(n_dati),  C.CIANO)

# ─── Esportazione testo ───────────────────────────────────────────────────────

def esporta_txt(percorso_out: Path, h: dict, chiavi_a, chiavi_a_ok,
                chiavi_b, chiavi_b_ok, blocchi, letti):
    righe = []
    righe.append("=" * 60)
    righe.append("  DUMP MIFARE - TESSERE MODULE")
    righe.append("=" * 60)
    righe.append(f"UID:          {hex_str(h['uid'], ':')}")
    righe.append(f"SAK:          0x{h['sak']:02X}")
    righe.append(f"ATQA:         0x{h['atqa']:04X}")
    righe.append(f"Tipo:         {h['tipo']}")
    righe.append(f"Settori:      {h['num_settori']}")
    righe.append("")
    righe.append("CHIAVI:")
    for s in range(h["num_settori"]):
        ka = hex_str(chiavi_a[s], "") if chiavi_a_ok[s] else "????????????"
        kb = hex_str(chiavi_b[s], "") if chiavi_b_ok[s] else "????????????"
        righe.append(f"  Settore {s:>2}: A={ka}  B={kb}")
    righe.append("")
    righe.append("DUMP:")

    for s in range(h["num_settori"]):
        trailer_idx = blocco_trailer(s)
        primo       = primo_blocco(s)
        quanti      = blocchi_nel_settore(s)
        righe.append(f"\n  --- Settore {s} ---")
        for i in range(quanti):
            nb    = primo + i
            stato = " " if letti[nb] else "?"
            nota  = "  <- trailer" if nb == trailer_idx else ("  <- UID/MFG" if nb == 0 else "")
            righe.append(f"  [{stato}] {nb:>3}: {hex_str(blocchi[nb])}  |{ascii_str(blocchi[nb])}|{nota}")

    percorso_out.write_text("\n".join(righe), encoding="utf-8")
    print(f"\n{C.VERDE}Esportato in: {percorso_out}{C.RESET}")

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Analizza un dump MIFARE .bin prodotto dal modulo Tessere di Bruce.",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument("file",             help="Percorso del file .bin")
    parser.add_argument("--settore",  type=int, default=None,
                        metavar="N",  help="Mostra solo il settore N nel dump")
    parser.add_argument("--blocco",   type=int, default=None,
                        metavar="N",  help="Mostra solo il blocco N nel dump")
    parser.add_argument("--microel",  action="store_true",
                        help="Mostra l'analisi specifica per tessere Microel")
    parser.add_argument("--raw",      action="store_true",
                        help="Mostra anche l'analisi dei trailer (access bits)")
    parser.add_argument("--esporta",  default=None, metavar="FILE",
                        help="Esporta il dump in un file .txt leggibile")
    parser.add_argument("--no-color", action="store_true",
                        help="Disabilita i colori ANSI nell'output")

    args = parser.parse_args()

    # Disabilita colori se richiesto o se stdout non è un terminale
    if args.no_color or not sys.stdout.isatty():
        for attr in dir(C):
            if not attr.startswith("_"):
                setattr(C, attr, "")

    percorso = Path(args.file)

    # Lettura e validazione
    dati = leggi_file(percorso)
    valida_header(dati)

    # Parsing
    h                                     = estrai_header(dati)
    chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok = estrai_chiavi(dati)
    letti, blocchi                        = estrai_blocchi(dati)

    print(f"\n{C.BOLD}{C.BIANCO}File: {percorso.name}  ({percorso.stat().st_size} byte){C.RESET}")

    # Output
    mostra_riepilogo(h, letti, h["num_settori"])
    mostra_chiavi(chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok, h["num_settori"])
    mostra_dump(blocchi, letti, h["num_settori"], args.settore, args.blocco)
    mostra_statistiche(blocchi, letti, h["num_settori"])

    if args.raw:
        mostra_trailer(blocchi, letti, h["num_settori"])

    if args.microel:
        mostra_microel(h, blocchi, letti)

    if args.esporta:
        esporta_txt(Path(args.esporta), h, chiavi_a, chiavi_a_ok,
                    chiavi_b, chiavi_b_ok, blocchi, letti)

    print()

if __name__ == "__main__":
    main()
