# Modulo Tessere — Struttura e Documentazione

Plugin per Bruce Firmware che aggiunge la gestione di tag NFC MIFARE
(Classic 1K / 4K / Mini, Ultralight) tramite lettore PN532 via I²C.

---

## Struttura dei file sorgente

```
Tessere/
├── Tessere.h                  # Classe Bruce: dichiarazione voce di menu e icona
├── Tessere.cpp                # Classe Bruce: menu principale + init PN532 + icona
│
├── TessereLogica.h            # Strutture dati, costanti, dichiarazione API core NFC/SD
├── TessereLogica.cpp          # Implementazione: NFC, chiavi, gestori, lettura/scrittura/SD
│
├── TessereMenu.h              # Dichiarazione azioni di menu (bridge UI → logica)
├── TessereMenu.cpp            # Implementazione azioni: Info, Leggi, Scrivi, Microel, Config
│
├── microel/
│   ├── TessereMicroel.h       # Strutture e API KDF/credito Microel
│   └── TessereMicroel.cpp     # Implementazione KDF, parser blocco, lettura/scrittura Microel
│
└── STRUTTURA.md               # Questo file
```

---

## Struttura del menu in Bruce

```
Tessere
├── Info
│     Mostra: UID, SAK, ATQA, Tipo tag, Gestore associato, Dump salvato in SD (Si/No)
│
├── Read
│     Legge tutti i settori con le chiavi da /rfid/tag/chiavi.txt
│     Salva il dump in /rfid/tag/dump/<UIDHEX>.bin
│     Chiede se associare un gestore al termine
│
├── Write
│   ├── Scrivi
│   │     Elenca i dump .bin in /rfid/tag/dump/
│   │     Chiede conferma mostrando l'UID del dump
│   │     Avvisa se l'UID del tag fisico è diverso da quello del dump
│   │     Scrive il dump sul tag fisico
│   │
│   └── Scrivi Auto
│         Legge l'UID del tag fisico
│         Cerca /rfid/tag/dump/<UIDHEX>.bin
│         Se trovato, scrive automaticamente senza ulteriori interazioni
│
├── Microel
│   ├── Info
│   │     Mostra: UID, Key A, Key B (generate via KDF, non lette dal tag),
│   │     Credito corrente, Credito precedente, Gestore, Dump salvato in SD
│   │
│   ├── Leggi
│   │     Genera le chiavi KDF dall'UID
│   │     Legge tutti i settori con le chiavi KDF
│   │     Salva il dump in /rfid/tag/dump/<UIDHEX>.bin
│   │     Chiede se associare un gestore al termine
│   │
│   ├── Imposta Credito
│   │     Legge la tessera con chiavi KDF
│   │     Mostra il credito attuale
│   │     Presenta il menu di selezione importo (5/10/15/20/25 EUR)
│   │     Aggiorna il credito in RAM (blocco 4 + blocco 5 precedente)
│   │     Scrive sul tag fisico (incluso blocco 0 su tag magic)
│   │
│   └── Genera Chiavi
│         Inserimento UID via tastiera (8 caratteri hex, es. 1E733840)
│         Calcola Key A e Key B tramite KDF
│         Mostra le chiavi su display
│         Opzione: salva in /rfid/tag/chiavi.txt
│
└── Config
    └── Gestori
        ├── Visualizza   → mostra la lista di tutti i gestori
        ├── Aggiungi     → inserisce un nuovo nome gestore
        ├── Modifica     → rinomina un gestore (aggiorna anche le associazioni UID)
        └── Elimina      → rimuove un gestore e tutte le sue associazioni UID
```

---

## Struttura della SD

```
/rfid/
└── tag/
    ├── chiavi.txt          # Chiavi di autenticazione MIFARE
    │                       # Formato per riga:
    │                       #   AABBCCDD,FFEEDDCCBBAA  → chiave specifica per UID
    │                       #   FFEEDDCCBBAA           → chiave generica
    │
    ├── gestori.txt         # Lista nomi gestori, uno per riga
    │                       # Esempio:
    │                       #   Sto&Bene
    │                       #   Serist
    │
    ├── gestori_mappa.txt   # Associazioni UID → gestore (CSV)
    │                       # Formato: UIDHEX,NomeGestore
    │                       # Esempio:
    │                       #   1E733840,Sto&Bene
    │                       #   AABBCCDD,Serist
    │
    └── dump/               # Dump binari dei tag letti
        ├── 1E733840.bin
        ├── AABBCCDD.bin
        └── ...
```

### Formato file dump (.bin)

File binario di circa 4.9 KB con il seguente layout:

| Offset | Dimensione | Contenuto              |
| ------ | ---------- | ---------------------- |
| 0      | 4 byte     | Firma magic `MFDR`     |
| 4      | 1 byte     | Versione formato (= 1) |
| 5      | 1 byte     | Lunghezza UID          |
| 6      | 7 byte     | UID (padding con zeri) |
| 13     | 1 byte     | SAK                    |
| 14     | 2 byte     | ATQA                   |
| 16     | 1 byte     | Numero settori         |
| 17     | 240 byte   | chiaveA[40][6]         |
| 257    | 40 byte    | chiaveATrovata[40]     |
| 297    | 240 byte   | chiaveB[40][6]         |
| 537    | 40 byte    | chiaveBTrovata[40]     |
| 577    | 256 byte   | bloccLetto[256]        |
| 833    | 4096 byte  | dati[256][16]          |

---

## KDF Microel

Le tessere Microel non usano chiavi fisse. Key A e Key B vengono derivate
dall'UID del tag in 3 passi:

**Step 1 — calcolaSumHex(UID)**

1. Somma i 4 byte dell'UID → riduce mod 256
2. Se il risultato è dispari, incrementa di 2 (parità pari richiesta)
3. XOR con la chiave fissa `{0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9}`
   → produce `sumHex` (6 byte)

**Step 2 — generaChiaveA(UID)**
Estrae il nibble alto del primo byte di `sumHex`:

- Nibble 2, 3, A, B → `chiaveA = 0x40 XOR sumHex`
- Nibble 6, 7, E, F → `chiaveA = 0xC0 XOR sumHex`
- Tutti gli altri → `chiaveA = sumHex`

**Step 3 — generaChiaveB(chiaveA)**
`chiaveB[i] = 0xFF XOR chiaveA[i]` per ogni byte `i`
→ Key A e Key B sono sempre complementari a 1

---

## Struttura blocco dati Microel (blocco 4)

| Byte  | Tipo   | Contenuto                                                       |
| ----- | ------ | --------------------------------------------------------------- |
| 0–1   | uint16 | Numero operazione (little-endian)                               |
| 2–3   | uint16 | Somma totale input                                              |
| 4     | uint8  | Deposito                                                        |
| 5–6   | uint16 | **Credito corrente in centesimi** ← modificato durante ricarica |
| 7–10  | uint32 | Data ultima transazione                                         |
| 11–12 | uint16 | Punti fedeltà                                                   |
| 13–14 | uint16 | Importo ultima transazione                                      |
| 15    | uint8  | Checksum (somma byte 0–14 mod 256)                              |

Il blocco 5 ha la stessa struttura e contiene il credito precedente.

---

## Note tecniche

- **Inizializzazione PN532**: avviene una sola volta all'ingresso del menu
  `Tessere` (in `Tessere::optionsMenu()`). Le operazioni successive non
  reinizializzano l'hardware.

- **Stack ESP32**: la struttura `DumpMifare` occupa ~4.9 KB. Nelle funzioni
  che la dichiarano localmente viene usato `static` per evitare l'overflow
  dello stack (default ~8 KB su ESP32).

- **Re-selezione del tag**: dopo ogni autenticazione fallita il PN532 porta
  il tag in stato di errore. Prima di ritentare con un'altra chiave si
  esegue `inRelease(1)` + `readPassiveTargetID` per ri-selezionare il tag.

- **Trailer del settore**: Key A viene sempre ripristinata nel trailer dopo
  la lettura perché l'hardware la restituisce come `00×6` per sicurezza.

- **Tag magic**: `scriviBloc0()` scrive il blocco 0 (UID/produttore) solo
  su tag magic (CUID, GEN2, ecc.). Su tag originali MIFARE la scrittura
  viene ignorata dall'hardware senza causare errori.
