#!/usr/bin/env python3
"""
leggi_dump.py  —  Analizzatore dump MIFARE per il modulo Tessere di Bruce.

Uso:
    python leggi_dump.py <file.bin>
    python leggi_dump.py <file.bin> --settore 0
    python leggi_dump.py <file.bin> --blocco 4
    python leggi_dump.py <file.bin> --microel
    python leggi_dump.py <file.bin> --raw
    python leggi_dump.py <file.bin> --esporta dump.txt
    python leggi_dump.py <file.bin> --no-color
"""

import sys
import struct
import argparse
from pathlib import Path

# ─── Costanti formato dump ────────────────────────────────────────────────────

DUMP_FIRMA    = b"MFDR"
DUMP_VERSIONE = 1
MAX_BLOCCHI   = 256
MAX_SETTORI   = 40

OFF_FIRMA       = 0
OFF_VERSIONE    = 4
OFF_LUNG_UID    = 5
OFF_UID         = 6
OFF_SAK         = 13
OFF_ATQA        = 14
OFF_NUM_SETTORI = 16
OFF_CHIAVE_A    = 17
OFF_CHIAVE_A_OK = 17 + 240
OFF_CHIAVE_B    = OFF_CHIAVE_A_OK + 40
OFF_CHIAVE_B_OK = OFF_CHIAVE_B + 240
OFF_BLOCC_LETTO = OFF_CHIAVE_B_OK + 40
OFF_DATI        = OFF_BLOCC_LETTO + 256
DIMENSIONE_ATTESA = OFF_DATI + MAX_BLOCCHI * 16

TIPO_DA_SAK = {
    0x08: "MIFARE Classic 1K",
    0x18: "MIFARE Classic 4K",
    0x09: "MIFARE Mini",
    0x00: "MIFARE Ultralight",
    0x28: "MIFARE Classic 1K (SmartMX)",
    0x38: "MIFARE Classic 4K (SmartMX)",
}

# ─── Colori e stili ANSI ─────────────────────────────────────────────────────

class C:
    RESET     = "\033[0m"
    BOLD      = "\033[1m"
    DIM       = "\033[2m"
    ITALIC    = "\033[3m"
    NERO      = "\033[30m"
    ROSSO     = "\033[31m"
    VERDE     = "\033[32m"
    GIALLO    = "\033[33m"
    BLU       = "\033[34m"
    VIOLA     = "\033[35m"
    CIANO     = "\033[36m"
    BIANCO    = "\033[37m"
    B_NERO    = "\033[90m"
    B_ROSSO   = "\033[91m"
    B_VERDE   = "\033[92m"
    B_GIALLO  = "\033[93m"
    B_BLU     = "\033[94m"
    B_VIOLA   = "\033[95m"
    B_CIANO   = "\033[96m"
    B_BIANCO  = "\033[97m"
    BG_NERO   = "\033[40m"
    BG_ROSSO  = "\033[41m"
    BG_VERDE  = "\033[42m"
    BG_GIALLO = "\033[43m"
    BG_BLU    = "\033[44m"
    BG_VIOLA  = "\033[45m"
    BG_CIANO  = "\033[46m"
    BG_BIANCO = "\033[47m"

LARGHEZZA = 68

# ─── Primitivi di layout ──────────────────────────────────────────────────────

def riga_piena(char="─", colore=C.B_NERO):
    return f"{colore}{char * LARGHEZZA}{C.RESET}"

def sezione(titolo_testo, icona=""):
    testo = f" {icona}  {titolo_testo} " if icona else f"  {titolo_testo} "
    pad   = LARGHEZZA - 2 - len(testo)
    print()
    print(f"{C.B_CIANO}┌{'─' * (LARGHEZZA - 2)}┐{C.RESET}")
    print(f"{C.B_CIANO}│{C.BOLD}{C.B_BIANCO}{testo}{C.RESET}{C.B_CIANO}{'─' * pad}│{C.RESET}")
    print(f"{C.B_CIANO}└{'─' * (LARGHEZZA - 2)}┘{C.RESET}")

def riga_campo(etichetta, valore, colore_v=C.B_BIANCO, largh_e=22):
    e = f"{C.DIM}{C.BIANCO}{etichetta:<{largh_e}}{C.RESET}"
    v = f"{colore_v}{valore}{C.RESET}"
    print(f"  {e}{v}")

def riga_sep():
    print(f"  {C.B_NERO}{'·' * (LARGHEZZA - 4)}{C.RESET}")

def badge(testo, colore_bg, colore_fg=None):
    fg = colore_fg if colore_fg else C.BOLD + C.NERO
    return f"{colore_bg}{fg} {testo} {C.RESET}"

def barra_progresso(valore, totale, largh=24):
    if totale == 0:
        return ""
    perc  = valore / totale
    pieni = int(perc * largh)
    vuoti = largh - pieni
    colore = C.VERDE if perc == 1.0 else C.GIALLO if perc >= 0.5 else C.ROSSO
    barra  = f"{colore}{'█' * pieni}{C.B_NERO}{'░' * vuoti}{C.RESET}"
    return f"[{barra}] {colore}{valore}/{totale}{C.RESET} {C.DIM}({perc*100:.0f}%){C.RESET}"

# ─── Banner ───────────────────────────────────────────────────────────────────

BANNER_LINES = [
    r"  ████████╗███████╗███████╗███████╗███████╗██████╗ ███████╗",
    r"     ██╔══╝██╔════╝██╔════╝██╔════╝██╔════╝██╔══██╗██╔════╝",
    r"     ██║   █████╗  ███████╗███████╗█████╗  ██████╔╝█████╗  ",
    r"     ██║   ██╔══╝  ╚════██║╚════██║██╔══╝  ██╔══██╗██╔══╝  ",
    r"     ██║   ███████╗███████║███████║███████╗██║  ██║███████╗",
    r"     ╚═╝   ╚══════╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝╚══════╝",
]
SUBTITLE = "MIFARE Dump Analyzer  ·  Tessere Module for Bruce"

def stampa_banner():
    gradient = [C.B_CIANO, C.CIANO, C.B_BLU, C.BLU, C.VIOLA, C.B_VIOLA]
    print()
    print(riga_piena("═", C.B_NERO))
    for i, riga in enumerate(BANNER_LINES):
        print(f"{C.BOLD}{gradient[i % len(gradient)]}{riga}{C.RESET}")
    pad = (LARGHEZZA - len(SUBTITLE)) // 2
    print(f"{' ' * pad}{C.DIM}{C.ITALIC}{C.B_BIANCO}{SUBTITLE}{C.RESET}")
    print(riga_piena("═", C.B_NERO))

# ─── Parsing file ─────────────────────────────────────────────────────────────

def leggi_file(percorso):
    if not percorso.exists():
        print(f"\n  {badge('ERRORE', C.BG_ROSSO, C.BOLD + C.B_BIANCO)}  File non trovato: {percorso}\n")
        sys.exit(1)
    return percorso.read_bytes()

def valida_header(dati):
    if dati[OFF_FIRMA:OFF_FIRMA + 4] != DUMP_FIRMA:
        print(f"\n  {badge('ERRORE', C.BG_ROSSO, C.BOLD + C.B_BIANCO)}  Firma non valida: {dati[0:4]}\n")
        sys.exit(1)
    if dati[OFF_VERSIONE] != DUMP_VERSIONE:
        print(f"\n  {badge('ERRORE', C.BG_ROSSO, C.BOLD + C.B_BIANCO)}  Versione non supportata: {dati[OFF_VERSIONE]}\n")
        sys.exit(1)

def estrai_header(dati):
    lung_uid    = dati[OFF_LUNG_UID]
    uid         = dati[OFF_UID:OFF_UID + lung_uid]
    sak         = dati[OFF_SAK]
    atqa        = struct.unpack_from("<H", dati, OFF_ATQA)[0]
    num_settori = dati[OFF_NUM_SETTORI]
    return {
        "uid": uid, "lung_uid": lung_uid, "sak": sak,
        "atqa": atqa, "num_settori": num_settori,
        "tipo": TIPO_DA_SAK.get(sak, f"Sconosciuto  SAK=0x{sak:02X}"),
    }

def estrai_chiavi(dati):
    chiavi_a    = [dati[OFF_CHIAVE_A    + s*6: OFF_CHIAVE_A    + s*6+6] for s in range(MAX_SETTORI)]
    chiavi_a_ok = [bool(dati[OFF_CHIAVE_A_OK + s]) for s in range(MAX_SETTORI)]
    chiavi_b    = [dati[OFF_CHIAVE_B    + s*6: OFF_CHIAVE_B    + s*6+6] for s in range(MAX_SETTORI)]
    chiavi_b_ok = [bool(dati[OFF_CHIAVE_B_OK + s]) for s in range(MAX_SETTORI)]
    return chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok

def estrai_blocchi(dati):
    letti     = [bool(dati[OFF_BLOCC_LETTO + i]) for i in range(MAX_BLOCCHI)]
    contenuto = [dati[OFF_DATI + i*16: OFF_DATI + i*16+16] for i in range(MAX_BLOCCHI)]
    return letti, contenuto

# ─── Helper struttura settori ─────────────────────────────────────────────────

def blocco_trailer(s):
    return s*4+3 if s < 32 else 32*4+(s-32)*16+15

def primo_blocco(s):
    return s*4 if s < 32 else 32*4+(s-32)*16

def blocchi_nel_settore(s):
    return 4 if s < 32 else 16

def hex_str(b, sep=" "):
    return sep.join(f"{x:02X}" for x in b)

def ascii_str(b):
    return "".join(chr(x) if 32 <= x < 127 else "·" for x in b)

# ─── Sezione: Riepilogo ───────────────────────────────────────────────────────

def mostra_riepilogo(h, letti, path):
    sezione("RIEPILOGO TAG", "◈")
    print()
    riga_campo("File",          path.name,          C.B_BIANCO)
    riga_campo("Dimensione",    f"{path.stat().st_size} byte", C.B_NERO)
    riga_sep()
    riga_campo("UID",           hex_str(h["uid"], ":"), C.B_VERDE + C.BOLD)
    riga_campo("Lunghezza UID", f"{h['lung_uid']} byte")
    riga_campo("SAK",           f"0x{h['sak']:02X}")
    riga_campo("ATQA",          f"0x{h['atqa']:04X}")
    riga_campo("Tipo tag",      h["tipo"],           C.B_CIANO + C.BOLD)
    riga_campo("Settori",       str(h["num_settori"]))
    riga_sep()
    n_set   = h["num_settori"]
    totale  = sum(blocchi_nel_settore(s) for s in range(n_set))
    n_letti = sum(1 for s in range(n_set)
                  for b in range(primo_blocco(s), primo_blocco(s) + blocchi_nel_settore(s))
                  if letti[b])
    print(f"  {C.DIM}{C.BIANCO}{'Blocchi letti':<22}{C.RESET}{barra_progresso(n_letti, totale)}")

# ─── Sezione: Chiavi ─────────────────────────────────────────────────────────

def mostra_chiavi(chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok, num_settori):
    sezione("CHIAVI PER SETTORE", "🔑")
    print()
    print(f"  {C.BOLD}{C.B_NERO}  {'S':>2}  {'Key A (6 byte)':^17}  {'Key B (6 byte)':^17}  {'Stato':^8}{C.RESET}")
    print(f"  {C.B_NERO}  {'──':>2}  {'─'*17}  {'─'*17}  {'─'*8}{C.RESET}")
    for s in range(num_settori):
        ka_ok = chiavi_a_ok[s]
        kb_ok = chiavi_b_ok[s]
        str_a = hex_str(chiavi_a[s]) if ka_ok else "?? ?? ?? ?? ?? ??"
        str_b = hex_str(chiavi_b[s]) if kb_ok else "?? ?? ?? ?? ?? ??"
        col_a = C.B_VERDE if ka_ok else C.B_NERO
        col_b = C.B_VERDE if kb_ok else C.B_NERO
        if ka_ok and kb_ok:   stato = badge(" OK ", C.BG_VERDE)
        elif ka_ok or kb_ok:  stato = badge("PARZ", C.BG_GIALLO)
        else:                 stato = badge(" -- ", C.BG_NERO + C.B_NERO, C.DIM + C.BIANCO)
        print(f"  {C.B_NERO}{s:>3}{C.RESET}  {col_a}{str_a}{C.RESET}  {col_b}{str_b}{C.RESET}  {stato}")

# ─── Sezione: Dump ────────────────────────────────────────────────────────────

def mostra_dump(blocchi, letti, num_settori, filtro_settore=None, filtro_blocco=None):
    sezione("DUMP COMPLETO", "▦")
    intestazione = (f"  {C.BOLD}{C.B_NERO}  {'BL':>3}  "
                    f"{'── DATI HEX (16 byte) ──────────────────────':47}  "
                    f"{'ASCII':16}{C.RESET}")
    sep_b = f"  {C.B_NERO}  {'───':>3}  {'─'*47}  {'─'*16}{C.RESET}"

    for s in range(num_settori):
        if filtro_settore is not None and s != filtro_settore:
            continue
        trailer_idx = blocco_trailer(s)
        primo       = primo_blocco(s)
        quanti      = blocchi_nel_settore(s)
        print()
        print(f"  {C.BOLD}{C.BG_NERO}{C.B_CIANO}  Settore {s:>2}  {C.RESET}")
        print(intestazione)
        print(sep_b)
        for i in range(quanti):
            nb = primo + i
            if filtro_blocco is not None and nb != filtro_blocco:
                continue
            if not letti[nb]:
                print(f"  {C.DIM}  {nb:>3}  {'?? '*15+'??':47}  {'?'*16}{C.RESET}")
                continue
            hex_p    = hex_str(blocchi[nb])
            asc_p    = ascii_str(blocchi[nb])
            all_zero = all(b == 0 for b in blocchi[nb])
            all_ff   = all(b == 0xFF for b in blocchi[nb])
            if nb == 0:
                etichetta = f"{C.BG_BLU}{C.B_BIANCO} UID {C.RESET}"
                colore    = C.B_CIANO
            elif nb == trailer_idx:
                etichetta = f"{C.BG_GIALLO}{C.NERO} KEY {C.RESET}"
                colore    = C.B_GIALLO
            elif all_zero or all_ff:
                etichetta = ""
                colore    = C.B_NERO
            else:
                etichetta = ""
                colore    = C.B_BIANCO
            tag_str = f" {etichetta}" if etichetta else ""
            print(f"  {colore}  {nb:>3}  {hex_p}  {C.DIM}{asc_p}{C.RESET}{tag_str}")

# ─── Sezione: Analisi trailer ─────────────────────────────────────────────────

def analizza_access_bits(trailer):
    b6, b7, b8 = trailer[6], trailer[7], trailer[8]
    nomi = ["Blocco 0", "Blocco 1", "Blocco 2", "Trailer"]
    return [(nomi[blk], (b7>>(4+blk))&1, (b8>>blk)&1, (b8>>(4+blk))&1) for blk in range(4)]

def mostra_trailer(blocchi, letti, num_settori):
    sezione("ANALISI TRAILER / ACCESS BITS", "⚙")
    print()
    for s in range(num_settori):
        t = blocco_trailer(s)
        if not letti[t]:
            continue
        d = blocchi[t]
        print(f"  {C.BOLD}{C.BG_NERO}{C.B_VIOLA}  Settore {s:>2}  {C.RESET}"
              f"  {C.DIM}trailer = blocco {t}{C.RESET}")
        print(f"    {C.DIM}Key A{C.RESET}  {C.B_NERO}{hex_str(d[0:6])}{C.RESET}  {C.DIM}(mascherata nell'hardware){C.RESET}")
        print(f"    {C.DIM}AC   {C.RESET}  {C.B_GIALLO}{hex_str(d[6:9])}{C.RESET}  {C.DIM}GPB = 0x{d[9]:02X}{C.RESET}")
        print(f"    {C.DIM}Key B{C.RESET}  {C.B_VERDE}{hex_str(d[10:16])}{C.RESET}")
        print(f"    {C.B_NERO}{'·'*40}{C.RESET}")
        for nome, c1, c2, c3 in analizza_access_bits(d):
            colore = C.B_GIALLO if "Trailer" in nome else C.B_BIANCO
            print(f"    {colore}{nome:<10}{C.RESET}  C1={C.B_CIANO}{c1}{C.RESET}  C2={C.B_CIANO}{c2}{C.RESET}  C3={C.B_CIANO}{c3}{C.RESET}")
        print()

# ─── Sezione: Statistiche ────────────────────────────────────────────────────

def mostra_statistiche(blocchi, letti, num_settori):
    sezione("STATISTICHE", "◉")
    print()
    totale     = sum(blocchi_nel_settore(s) for s in range(num_settori))
    n_letti    = sum(1 for i in range(totale) if letti[i])
    n_zero     = sum(1 for i in range(totale) if letti[i] and all(b == 0x00 for b in blocchi[i]))
    n_ff       = sum(1 for i in range(totale) if letti[i] and all(b == 0xFF for b in blocchi[i]))
    n_dati     = n_letti - n_zero - n_ff
    n_mancanti = totale - n_letti
    riga_campo("Totale blocchi",     str(totale))
    print(f"  {C.DIM}{C.BIANCO}{'Letti':<22}{C.RESET}{barra_progresso(n_letti, totale)}")
    riga_campo("Non letti",         str(n_mancanti), C.B_ROSSO if n_mancanti > 0 else C.B_NERO)
    riga_sep()
    riga_campo("Blocchi tutti 0x00", str(n_zero), C.B_NERO)
    riga_campo("Blocchi tutti 0xFF", str(n_ff),   C.B_NERO)
    riga_campo("Con dati reali",     str(n_dati), C.B_CIANO)

# ─── Sezione: Microel ─────────────────────────────────────────────────────────

def microel_kdf(uid):
    xor_key = [0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9]
    somma   = sum(uid) % 256
    if somma % 2 == 1:
        somma += 2
    sum_hex = bytes(somma ^ xor_key[i] for i in range(6))
    n = (sum_hex[0] >> 4) & 0x0F
    if n in (0x2, 0x3, 0xA, 0xB):   ka = bytes(0x40 ^ b for b in sum_hex)
    elif n in (0x6, 0x7, 0xE, 0xF): ka = bytes(0xC0 ^ b for b in sum_hex)
    else:                            ka = sum_hex
    return ka, bytes(0xFF ^ b for b in ka)

def fmt_eur(centesimi):
    return f"{centesimi/100:.2f} EUR  {C.DIM}({centesimi} cent){C.RESET}"

def mostra_microel(h, blocchi, letti):
    sezione("ANALISI TESSERA MICROEL", "◈")
    print()
    if h["lung_uid"] != 4:
        print(f"  {badge(' ATTENZIONE ', C.BG_GIALLO, C.BOLD + C.NERO)}  "
              f"UID non di 4 byte — potrebbe non essere Microel.\n")
    uid = h["uid"]
    ka, kb = microel_kdf(uid)
    riga_campo("UID",           hex_str(uid, ":"),  C.B_VERDE + C.BOLD)
    riga_sep()
    riga_campo("Key A  (KDF)",  hex_str(ka, " "),   C.B_CIANO)
    riga_campo("Key B  (KDF)",  hex_str(kb, " "),   C.B_VIOLA)
    riga_sep()
    if letti[4]:
        d4      = blocchi[4]
        credito = d4[5] | (d4[6] << 8)
        cs_calc = (0x21 + sum(d4[:15])) % 256
        cs_file = d4[15]
        cs_ok   = cs_calc == cs_file
        riga_campo("Credito corrente", fmt_eur(credito), C.B_GIALLO + C.BOLD)
        riga_campo("Checksum bl.4",
                   f"0x{cs_file:02X}  (atteso 0x{cs_calc:02X})  {'✓ valido' if cs_ok else '✗ NON VALIDO'}",
                   C.B_VERDE if cs_ok else C.B_ROSSO)
    else:
        riga_campo("Credito corrente", badge(" BLOCCO 4 NON LETTO ", C.BG_ROSSO, C.BOLD + C.B_BIANCO))
    if letti[5]:
        d5   = blocchi[5]
        prec = d5[5] | (d5[6] << 8)
        riga_campo("Credito precedente", fmt_eur(prec), C.B_GIALLO)
    else:
        riga_campo("Credito precedente", badge(" BLOCCO 5 NON LETTO ", C.BG_NERO, C.DIM + C.BIANCO))
    if letti[4]:
        d4 = blocchi[4]
        print()
        print(f"  {C.BOLD}{C.BG_NERO}{C.B_BIANCO}  Struttura blocco 4 (raw)  {C.RESET}")
        print()
        n_op   = d4[0]  | (d4[1]  << 8)
        totale = d4[2]  | (d4[3]  << 8)
        dep    = d4[4]
        data_t = d4[7]  | (d4[8]<<8) | (d4[9]<<16) | (d4[10]<<24)
        punti  = d4[11] | (d4[12] << 8)
        ultima = d4[13] | (d4[14] << 8)
        riga_campo("  N. operazione",      str(n_op),                   C.B_BIANCO, 26)
        riga_campo("  Totale caricato",    f"{totale/100:.2f} EUR",     C.B_BIANCO, 26)
        riga_campo("  Deposito",           f"0x{dep:02X}",              C.B_BIANCO, 26)
        riga_campo("  Credito corrente",   f"{(d4[5]|(d4[6]<<8))/100:.2f} EUR", C.B_GIALLO, 26)
        riga_campo("  Data transazione",   f"0x{data_t:08X}",           C.B_NERO,   26)
        riga_campo("  Punti fedeltà",      str(punti),                  C.B_BIANCO, 26)
        riga_campo("  Ultima transazione", f"{ultima/100:.2f} EUR",     C.B_BIANCO, 26)

# ─── Esportazione testo ───────────────────────────────────────────────────────

def esporta_txt(out, h, chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok, blocchi, letti):
    righe = ["="*60, "  DUMP MIFARE — Tessere Module for Bruce", "="*60, "",
             f"UID:      {hex_str(h['uid'], ':')}", f"SAK:      0x{h['sak']:02X}",
             f"ATQA:     0x{h['atqa']:04X}", f"Tipo:     {h['tipo']}",
             f"Settori:  {h['num_settori']}", "", "CHIAVI:"]
    for s in range(h["num_settori"]):
        ka = hex_str(chiavi_a[s]) if chiavi_a_ok[s] else "??"*6
        kb = hex_str(chiavi_b[s]) if chiavi_b_ok[s] else "??"*6
        righe.append(f"  Settore {s:>2}:  A = {ka}   B = {kb}")
    righe += ["", "DUMP:", ""]
    for s in range(h["num_settori"]):
        righe.append(f"  --- Settore {s} ---")
        for i in range(blocchi_nel_settore(s)):
            nb    = primo_blocco(s) + i
            stato = " " if letti[nb] else "?"
            nota  = "  <- trailer" if nb == blocco_trailer(s) else ("  <- UID/MFG" if nb == 0 else "")
            righe.append(f"  [{stato}] {nb:>3}: {hex_str(blocchi[nb])}  |{ascii_str(blocchi[nb])}|{nota}")
        righe.append("")
    out.write_text("\n".join(righe), encoding="utf-8")
    print(f"\n  {badge(' SALVATO ', C.BG_VERDE, C.BOLD + C.NERO)}  {out}\n")

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("file",       nargs="?")
    parser.add_argument("--settore",  type=int, default=None, metavar="N")
    parser.add_argument("--blocco",   type=int, default=None, metavar="N")
    parser.add_argument("--microel",  action="store_true")
    parser.add_argument("--raw",      action="store_true")
    parser.add_argument("--esporta",  default=None, metavar="FILE")
    parser.add_argument("--no-color", action="store_true")
    parser.add_argument("-h", "--help", action="store_true")
    args = parser.parse_args()

    if args.no_color or not sys.stdout.isatty():
        for attr in [a for a in dir(C) if not a.startswith("_")]:
            setattr(C, attr, "")

    stampa_banner()

    if args.help or not args.file:
        print(f"""
  {C.BOLD}Uso:{C.RESET}
    python leggi_dump.py <file.bin>  [opzioni]

  {C.BOLD}Opzioni:{C.RESET}
    {C.B_CIANO}--settore N{C.RESET}     Mostra solo il settore N nel dump
    {C.B_CIANO}--blocco N{C.RESET}      Mostra solo il blocco N nel dump
    {C.B_CIANO}--microel{C.RESET}       Analisi specifica per tessere Microel
    {C.B_CIANO}--raw{C.RESET}           Analisi access bits di ogni trailer
    {C.B_CIANO}--esporta FILE{C.RESET}  Salva il dump in un file .txt
    {C.B_CIANO}--no-color{C.RESET}      Disabilita i colori ANSI
""")
        return

    percorso = Path(args.file)
    dati     = leggi_file(percorso)
    valida_header(dati)

    h                                            = estrai_header(dati)
    chiavi_a, chiavi_a_ok, chiavi_b, chiavi_b_ok = estrai_chiavi(dati)
    letti, blocchi                               = estrai_blocchi(dati)

    mostra_riepilogo(h, letti, percorso)
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

    print(f"\n{riga_piena('═', C.B_NERO)}\n")

if __name__ == "__main__":
    main()
