# Tessere — Documentazione del progetto

Modulo firmware per **Bruce** che gestisce tag NFC MIFARE (Classic 1K/4K/Mini, Ultralight)
tramite il lettore **PN532** via I²C. Permette di leggere, scrivere, clonare e gestire
tessere di accesso e pagamento prepagato.

---

## Indice

1. [Struttura dei file](#1-struttura-dei-file)
2. [Architettura a livelli](#2-architettura-a-livelli)
3. [File per file](#3-file-per-file)
4. [La variabile g_dump](#4-la-variabile-g_dump)
5. [File sulla SD](#5-file-sulla-sd)
6. [Formato del dump .bin](#6-formato-del-dump-bin)
7. [Il modulo Microel](#7-il-modulo-microel)
8. [Come aggiungere un nuovo tipo di tessera](#8-come-aggiungere-un-nuovo-tipo-di-tessera)
9. [Flusso di una operazione NFC tipica](#9-flusso-di-una-operazione-nfc-tipica)
10. [Note e avvertenze](#10-note-e-avvertenze)

---

## 1. Struttura dei file

```
Tessere/
│
├── Tessere.h                   # Dichiarazione classe Bruce
├── Tessere.cpp                 # Menu principale + icona launcher
│
├── TessereMenuLogic.h          # Dichiarazione voci di menu
├── TessereMenuLogic.cpp        # UI: schermate, messaggi, selezione file
│
├── TessereLogic.h              # API core: strutture, costanti, dichiarazioni
├── TessereLogic.cpp            # Logica NFC + SD (valida per qualsiasi tessera)
│
├── TessereMicroelLogic.h       # API specifica Microel
└── TessereMicroelLogic.cpp     # KDF, credito, scrittura blocco 0
```

> Ogni nuovo tipo di tessera aggiunge una coppia di file
> `TessereXxxLogic.h` / `TessereXxxLogic.cpp` seguendo lo stesso schema.

---

## 2. Architettura a livelli

Il progetto è diviso in **3 livelli** che non si "saltano" mai:
ogni livello parla solo con quello adiacente.

```
┌─────────────────────────────────────────┐
│         LIVELLO 1 — Entry point         │
│   Tessere.h / Tessere.cpp               │
│   • Menu principale                     │
│   • Icona nel launcher Bruce            │
└───────────────┬─────────────────────────┘
                │ chiama
┌───────────────▼─────────────────────────┐
│      LIVELLO 2 — Presentazione          │
│   TessereMenuLogic.h / .cpp             │
│   • Schermate e messaggi a display      │
│   • Selezione file da SD                │
│   • Conferme utente                     │
│   • Sottomenu per tipo tessera          │
└───────────────┬─────────────────────────┘
                │ chiama
┌───────────────▼─────────────────────────┐
│         LIVELLO 3 — Logica core         │
│   TessereLogic.h / .cpp                 │
│   • Comunicazione con PN532             │
│   • Lettura / scrittura tag             │
│   • Gestione chiavi e gestori su SD     │
│   • Serializzazione dump                │
│                                         │
│   TessereMicroelLogic.h / .cpp          │
│   • KDF: derivazione chiavi da UID      │
│   • Parser blocco credito               │
│   • Scrittura con blocco 0              │
└─────────────────────────────────────────┘
```

**Regola fondamentale:** il livello 2 non parla mai direttamente con il PN532.
Il livello 1 non sa nulla di file o chiavi. Ogni livello ha un solo compito.

---

## 3. File per file

### `Tessere.h` / `Tessere.cpp`

**Cosa fa:** è la classe che Bruce carica nel launcher. Contiene solo:

- `optionsMenu()` → costruisce il menu principale con le voci
- `drawIcon()` → disegna l'icona nel launcher

**Quando lo modifichi:** solo quando aggiungi o rimuovi una voce dal menu principale.

```cpp
// Esempio: aggiungere una voce
{"Microel", []() { MicroelTessera(); }, false},
```

---

### `TessereMenuLogic.h` / `TessereMenuLogic.cpp`

**Cosa fa:** implementa le azioni visibili all'utente. Ogni funzione è una voce di menu.
Usa i due helper interni `showMessage()` (attende tasto) e `showInfo()` (non attende).

| Funzione             | Descrizione                                       |
| -------------------- | ------------------------------------------------- |
| `InfoTessera()`      | Mostra UID, SAK, ATQA, tipo, gestore              |
| `ReadTessera()`      | Legge dump, salva su SD, associa gestore          |
| `WriteTessera()`     | Scrive dump scelto dalla SD                       |
| `AutoWriteTessera()` | Scrive automaticamente il dump per l'UID rilevato |
| `GestoriMenu()`      | Aggiungi / modifica / elimina gestori             |
| `MicroelTessera()`   | Sottomenu Microel: Info, Read, Write              |

**Quando lo modifichi:** quando cambi una schermata, aggiungi un sottomenu
o aggiungi una nuova azione utente.

---

### `TessereLogic.h` / `TessereLogic.cpp`

**Cosa fa:** è il motore del progetto. Contiene tutta la logica NFC e SD
che è valida per qualsiasi tipo di tessera MIFARE.

| Funzione                | Descrizione                                         |
| ----------------------- | --------------------------------------------------- |
| `mifareInit()`          | Inizializza PN532 via I²C (lazy, una volta sola)    |
| `waitForMifareTag()`    | Attende tag e popola `g_dump` (timeout 6 s)         |
| `waitForAnyMifareTag()` | Attende tag senza sovrascrivere `g_dump`            |
| `mifareReadDump()`      | Legge tutti i settori del tag in `g_dump`           |
| `mifareWriteDump()`     | Scrive un dump sul tag (salta blocco 0)             |
| `mifareWriteBlock0()`   | Scrive il blocco 0 su tag magic                     |
| `loadKeysForUID()`      | Carica chiavi da `/rfid/chiavi.txt`                 |
| `saveDumpToSD()`        | Salva dump binario su SD                            |
| `loadDumpFromSD()`      | Carica dump binario da SD                           |
| `buildUIDHex()`         | Converte UID bytes → stringa hex (es. `"AABBCCDD"`) |
| `loadGestori()`         | Legge lista nomi da `gestori.txt`                   |
| `addGestore()`          | Aggiunge gestore a `gestori.txt`                    |
| `deleteGestore()`       | Elimina gestore e associazioni                      |
| `modifyGestore()`       | Rinomina gestore e aggiorna associazioni            |
| `associateGestore()`    | Salva associazione UID → gestore                    |
| `lookupGestore()`       | Cerca gestore per UID in `gestori_map.txt`          |

**Quando lo modifichi:** quando cambi la logica di lettura/scrittura,
il formato del dump, o aggiungi funzioni NFC generiche.

---

### `TessereMicroelLogic.h` / `TessereMicroelLogic.cpp`

**Cosa fa:** estende `TessereLogic` con la logica specifica per le tessere Microel.

| Funzione                     | Descrizione                                     |
| ---------------------------- | ----------------------------------------------- |
| `microelCalculateSumHex()`   | Step 1 KDF: UID → sumHex                        |
| `microelGenerateKeyA()`      | Step 2 KDF: sumHex → Key A                      |
| `microelGenerateKeyB()`      | Step 3 KDF: Key A → Key B (NOT bit a bit)       |
| `microelGenerateKeys()`      | Wrapper: UID → Key A + Key B in una chiamata    |
| `microelInjectKeys()`        | Inietta le chiavi KDF nei settori del dump      |
| `microelVerify()`            | Verifica se il dump è di una tessera Microel    |
| `microelReadCard()`          | Legge tessera Microel (wait + inject + read)    |
| `microelParseBlock()`        | Decodifica i 16 byte del blocco 4               |
| `microelBuildBlock()`        | Serializza la struct nel buffer a 16 byte       |
| `microelCalculateChecksum()` | Somma byte 0–14 mod 256                         |
| `microelGetCredit()`         | Legge credito dal blocco 4 in centesimi         |
| `microelSetCredit()`         | Aggiorna credito nei blocchi 4 e 5 in RAM       |
| `microelInfoCard()`          | Mostra UID, Key A/B, credito, gestore a display |
| `microelWriteCard()`         | Scrive dump + blocco 0 (per tag magic)          |
| `microelCreditMenu()`        | Menu Serial per scegliere importo ricarica      |
| `microelRechargeTessera()`   | Flusso completo ricarica (leggi→scegli→scrivi)  |

---

## 4. La variabile g_dump

`g_dump` è una variabile globale di tipo `MifareDump` definita in `TessereLogic.cpp`
e dichiarata `extern` in `TessereLogic.h`. È accessibile da tutti i moduli.

```
┌─────────────────────────────────────┐
│             MifareDump              │
├─────────────────────────────────────┤
│ uid[7]          → UID del tag       │
│ uidLen          → lunghezza UID     │
│ sak             → tipo tag          │
│ atqa            → risposta anti-col │
│ tagType         → stringa tipo      │
│ numSectors      → n. settori        │
├─────────────────────────────────────┤
│ data[256][16]   → contenuto blocchi │
│ blockRead[256]  → flag lettura      │
├─────────────────────────────────────┤
│ keyA[40][6]     → Key A per settore │
│ keyAFound[40]   → flag validità     │
│ keyB[40][6]     → Key B per settore │
│ keyBFound[40]   → flag validità     │
└─────────────────────────────────────┘
  Dimensione totale in RAM: ~4.9 KB
```

**Regola d'uso:**

- `waitForMifareTag()` lo popola con l'identità del tag (UID, SAK, ecc.)
- `mifareReadDump()` lo popola con i dati dei blocchi
- Quando carichi un dump dalla SD per scriverlo su un altro tag,
  lo metti in una variabile locale `static MifareDump loadedDump`
  per non sovrascrivere `g_dump` che potrebbe servire ancora.

> ⚠️ `MifareDump` pesa ~4.9 KB: non dichiararla mai come variabile locale
> normale sullo stack dell'ESP32. Usa sempre `static` oppure dichiara
> la variabile a livello di file.

---

## 5. File sulla SD

```
/rfid/
│
├── chiavi.txt          → chiavi di autenticazione per mifareReadDump()
│                         Formato CSV:
│                           AABBCCDD,FFEEDDCCBBAA  (specifica per UID)
│                           FFEEDDCCBBAA           (generica per tutti)
│
├── gestori.txt         → lista nomi gestori, uno per riga
│                         Esempio:
│                           Sto&Bene
│                           Bar Roma
│
├── gestori_map.txt     → associazioni UID → gestore
│                         Formato CSV:
│                           1E733840,Sto&Bene
│                           AABBCCDD,Bar Roma
│
└── dumps/
    ├── AABBCCDD.bin    → dump binario del tag con UID AABBCCDD
    ├── 1E733840.bin
    └── ...
```

> Le chiavi `FF FF FF FF FF FF` e `00 00 00 00 00 00` vengono sempre
> aggiunte automaticamente da `loadKeysForUID()` senza bisogno di
> scriverle nel file.

---

## 6. Formato del dump .bin

Il file è binario, little-endian, dimensione fissa ~4.9 KB.

```
Offset   Dimensione   Contenuto
──────   ──────────   ─────────────────────────────────
0        4 byte       Magic "MFDR" (identifica il file)
4        1 byte       Versione formato (attualmente: 1)
5        1 byte       uidLen
6        7 byte       uid (padding 0 per UID < 7 byte)
13       1 byte       sak
14       2 byte       atqa
16       1 byte       numSectors
17       240 byte     keyA[40][6]
257      40 byte      keyAFound[40]
297      240 byte     keyB[40][6]
537      40 byte      keyBFound[40]
577      256 byte     blockRead[256]
833      4096 byte    data[256][16]
──────   ──────────   ─────────────────────────────────
Totale   ~4929 byte
```

> Se in futuro cambi il formato, incrementa `DUMP_VERSION` in `TessereLogic.h`.
> `loadDumpFromSD()` controlla la versione e rifiuta file con versione diversa.

---

## 7. Il modulo Microel

Le tessere Microel sono MIFARE Classic 1K usate in distributori automatici
e sistemi di accesso. La loro particolarità è il **KDF** (Key Derivation Function):
le chiavi non sono fisse ma vengono calcolate dall'UID del tag.

### KDF — come funziona

```
UID (4 byte)
    │
    ▼
[ Somma i 4 byte ]  →  sum % 256  →  arrotonda al pari
    │
    ▼
[ XOR con chiave fissa {0x01,0x92,0xA7,0x75,0x2B,0xF9} ]
    │
    ▼
sumHex (6 byte)
    │
    ├── nibble alto byte[0] ∈ {2,3,A,B}  →  Key A = 0x40 XOR sumHex
    ├── nibble alto byte[0] ∈ {6,7,E,F}  →  Key A = 0xC0 XOR sumHex
    └── tutti gli altri                  →  Key A = sumHex
    │
    ▼
Key A (6 byte)
    │
    ▼
Key B = NOT(Key A)  →  keyB[i] = 0xFF XOR keyA[i]
```

### Struttura del blocco 4 (credito)

```
Byte   Tipo      Contenuto
────   ──────    ──────────────────────────────────
0–1    uint16    Numero operazione (little-endian)
2–3    uint16    Somma totale input (little-endian)
4      uint8     Deposito
5–6    uint16    Credito corrente in centesimi ← modificato dalla ricarica
7–10   uint32    Data ultima transazione (little-endian)
11–12  uint16    Punti accumulati (little-endian)
13–14  uint16    Importo ultima transazione in centesimi (little-endian)
15     uint8     Checksum = somma byte 0–14 mod 256
```

Il blocco 5 ha la stessa struttura ed è usato per il **credito precedente**:
ogni volta che si modifica il credito nel blocco 4, il vecchio valore
viene prima copiato nel blocco 5.

---

## 8. Come aggiungere un nuovo tipo di tessera

Segui questi 4 passi nell'ordine indicato.

### Passo 1 — Crea i file di logica specifica

```cpp
// TessereXxxLogic.h
#pragma once
#include "TessereLogic.h"
#include <Arduino.h>

// Strutture dati specifiche per il tipo Xxx
struct XxxBlockData { ... };

// Funzioni specifiche
void xxxGenerateKeys(...);
bool xxxReadCard(uint8_t &sectorsRead);
void xxxInfoCard();
// ecc.
```

```cpp
// TessereXxxLogic.cpp
#include "TessereXxxLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"

// Implementazione delle funzioni
```

### Passo 2 — Dichiara la funzione di sottomenu

In `TessereMenuLogic.h` aggiungi:

```cpp
/**
 * @brief Sottomenu per le tessere Xxx.
 * Voci: Info, Read, Write.
 */
void XxxTessera();
```

### Passo 3 — Implementa il sottomenu

In `TessereMenuLogic.cpp` aggiungi l'include e la funzione:

```cpp
#include "TessereXxxLogic.h"

void XxxTessera() {
    if (!mifareInit()) return;

    std::vector<Option> opts = {
        {"Info",  []() { xxxInfoCard(); },  false},
        {"Read",  []() { /* ... */ },       false},
        {"Write", []() { /* ... */ },       false},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Xxx");
}
```

### Passo 4 — Aggiungi la voce nel menu principale

In `Tessere.cpp`:

```cpp
void Tessere::optionsMenu() {
    std::vector<Option> options = {
        // ... voci esistenti ...
        {"Xxx", []() { XxxTessera(); }, false}, // ← aggiunta
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Tessere");
}
```

---

## 9. Flusso di una operazione NFC tipica

Questo è il flusso generico seguito da tutte le azioni (Read, Write, Info, ecc.):

```
Tessere.cpp
    │
    │  chiama
    ▼
TessereMenuLogic.cpp
    │
    ├─ showInfo()          → mostra "Place tag on reader..."
    │
    ├─ mifareInit()        → inizializza PN532 (se non già fatto)
    │
    ├─ waitForMifareTag()  → attende il tag (timeout 6 s)
    │                        popola g_dump.uid, sak, atqa, tagType
    │
    ├─ [modulo specifico]  → es. microelInjectKeys() per Microel
    │
    ├─ mifareReadDump()    → legge i settori con le chiavi
    │   oppure               popola g_dump.data[], blockRead[]
    │  mifareWriteDump()   → scrive i settori dal dump
    │
    ├─ saveDumpToSD()      → salva su /rfid/dumps/<UID>.bin
    │   oppure
    │  loadDumpFromSD()    → carica da /rfid/dumps/<UID>.bin
    │
    └─ showMessage()       → mostra risultato e attende tasto
```

---

## 10. Note e avvertenze

### Stack ESP32

`MifareDump` pesa ~4.9 KB. Lo stack di default dell'ESP32 è ~8 KB.
Dichiara sempre le variabili locali di questo tipo come `static`:

```cpp
// ✅ Corretto
static MifareDump loadedDump;

// ❌ Sbagliato: rischio stack overflow
MifareDump loadedDump;
```

### Blocco 0

Il blocco 0 contiene UID, BCC e dati produttore. Sui tag MIFARE originali
è read-only. `mifareWriteDump()` lo salta sempre. Per scriverlo su tag magic
(CUID, GEN2, ecc.) usa esplicitamente `mifareWriteBlock0()`.

### Sector trailer

Il blocco trailer di ogni settore contiene Key A, Access Bits e Key B.
Scrivere un trailer errato può rendere il settore **inaccessibile in modo permanente**.
Verificare sempre le chiavi prima di scrivere un trailer.

### Versione del dump

Se modifichi la struttura di `MifareDump` o il formato del file `.bin`,
incrementa `DUMP_VERSION` in `TessereLogic.h`. I dump salvati con la
versione precedente non saranno più caricabili finché non vengono rigenerati.

### Chiavi Microel

Le chiavi Microel si sommano a quelle su `/rfid/chiavi.txt`: non si escludono.
`microelInjectKeys()` inietta le chiavi KDF solo sui settori che hanno ancora
le chiavi di default (FF×6 o 00×6), lasciando intatte le chiavi custom.

### Tag magic

`mifareWriteBlock0()` ritorna `false` senza errori bloccanti su tag normali.
L'operazione di scrittura degli altri settori procede comunque.
Usa il parametro `block0Written` per sapere se la clonazione è stata completa.
