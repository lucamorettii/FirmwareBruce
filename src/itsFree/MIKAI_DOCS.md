# Mikai — Documentazione del progetto

Modulo firmware per **Bruce** che gestisce tag NFC **SRIX4K** (MyKey / Mikai)
tramite il lettore **PN532** via I²C. Permette di leggere, ricaricare, resettare
e gestire la configurazione vendor di chiavi di pagamento prepagato.

---

## Indice

1. [Struttura dei file](#1-struttura-dei-file)
2. [Architettura a livelli](#2-architettura-a-livelli)
3. [File per file](#3-file-per-file)
4. [Le strutture dati principali](#4-le-strutture-dati-principali)
5. [La chiave di sessione (encryptionKey)](#5-la-chiave-di-sessione-encryptionkey)
6. [Mappa della EEPROM SRIX4K](#6-mappa-della-eeprom-srix4k)
7. [Il sistema di flag (srix_flag)](#7-il-sistema-di-flag-srix_flag)
8. [Come funziona la ricarica (mikai_add_cents)](#8-come-funziona-la-ricarica-mikai_add_cents)
9. [Il vendor: cosa è e come gestirlo](#9-il-vendor-cosa-è-e-come-gestirlo)
10. [Come aggiungere una nuova azione al menu](#10-come-aggiungere-una-nuova-azione-al-menu)
11. [Flusso di una operazione tipica](#11-flusso-di-una-operazione-tipica)
12. [Note e avvertenze](#12-note-e-avvertenze)
13. [Miglioramenti](#13-miglioramenti)

---

## 1. Struttura dei file

```
Mikai/
│
├── Mikai.h                     # Dichiarazione classe Bruce
├── Mikai.cpp                   # Menu principale + icona launcher + init PN532
│
├── MikaiMenuLogic.h            # Dichiarazione voci di menu
├── MikaiMenuLogic.cpp          # UI: schermate, selezione importi, conferme
│
├── MikaiLogic.h                # API core: strutture, costanti, dichiarazioni
└── MikaiLogic.cpp              # Logica NFC + crittografia + I/O tag
```

> A differenza del modulo Tessere, qui non esistono moduli specifici per tipo
> di tessera perché SRIX4K è l'unico formato supportato. Se in futuro si
> aggiungesse un secondo tipo di tag, si seguirebbe lo stesso pattern di Tessere
> creando un file `MikaiXxxLogic.h/.cpp`.

---

## 2. Architettura a livelli

```
┌─────────────────────────────────────────┐
│         LIVELLO 1 — Entry point         │
│   Mikai.h / Mikai.cpp                   │
│   • Inizializzazione PN532 (lazy)       │
│   • Menu principale                     │
│   • Icona nel launcher Bruce            │
└───────────────┬─────────────────────────┘
                │ chiama
┌───────────────▼─────────────────────────┐
│      LIVELLO 2 — Presentazione          │
│   MikaiMenuLogic.h / .cpp               │
│   • Schermate e messaggi a display      │
│   • Selezione importi di ricarica       │
│   • Selezione file vendor dalla SD      │
│   • Sottomenu di conferma scrittura     │
└───────────────┬─────────────────────────┘
                │ chiama
┌───────────────▼─────────────────────────┐
│         LIVELLO 3 — Logica core         │
│   MikaiLogic.h / .cpp                   │
│   • Bridge NFC (lettura/scrittura tag)  │
│   • Crittografia (permutazione, XOR)    │
│   • Calcolo chiave di sessione          │
│   • Gestione credito e transazioni      │
│   • Reset e import/export vendor        │
└─────────────────────────────────────────┘
```

**Regola fondamentale:** il livello 2 non tocca mai l'EEPROM del tag direttamente.
Il livello 1 non conosce né la crittografia né il formato dei blocchi.
Ogni livello ha un solo compito.

---

## 3. File per file

### `Mikai.h` / `Mikai.cpp`

**Cosa fa:** è la classe che Bruce carica nel launcher. Contiene:

- `optionsMenu()` → inizializza il PN532 (una sola volta per sessione) e mostra il menu
- `drawIcon()` → disegna l'icona del lettore NFC nel launcher

L'inizializzazione del PN532 avviene qui (non in `MikaiLogic.cpp`) perché
dipende dalla configurazione hardware di Bruce (`bruceConfigPins`).

**Quando lo modifichi:** quando aggiungi/rimuovi voci dal menu principale,
o quando cambia il modo in cui il PN532 viene inizializzato.

```cpp
// Esempio: aggiungere una voce
{"Nuova azione", []() { actionNuova(); }, false},
```

---

### `MikaiMenuLogic.h` / `MikaiMenuLogic.cpp`

**Cosa fa:** implementa le azioni visibili all'utente. Ogni funzione è una voce di menu.
Usa due helper interni: `showMessage()` (attende tasto) e `loadTag()` (legge il tag).

| Funzione               | Descrizione                                              |
| ---------------------- | -------------------------------------------------------- |
| `actionInfo()`         | Mostra credito, SK, data produzione, storico transazioni |
| `actionWrite()`        | Scrive sul tag tutti i blocchi modificati in RAM         |
| `actionAddCredit()`    | Aggiunge un importo preselezionato al credito            |
| `actionSetCredit()`    | Imposta il credito a un valore esatto (azzera storico)   |
| `actionExportVendor()` | Salva i blocchi vendor in un file .bin sulla SD          |
| `actionImportVendor()` | Carica i blocchi vendor da un file .bin e ricifra        |
| `actionReset()`        | Riporta il tag allo stato di fabbrica                    |

**Quando lo modifichi:** quando cambi una schermata, aggiungi nuovi importi
di ricarica, o aggiungi una nuova azione utente.

---

### `MikaiLogic.h` / `MikaiLogic.cpp`

**Cosa fa:** è il motore del progetto. Contiene tutta la logica NFC
e crittografica specifica per il tag SRIX4K.

**Funzioni pubbliche (API `mikai_*`):**

| Funzione                        | Descrizione                                             |
| ------------------------------- | ------------------------------------------------------- |
| `mikai_read_tag()`              | Legge il tag e popola `srixKey` (UID + EEPROM + SK)     |
| `mikai_get_info_string()`       | Genera la stringa informativa del tag                   |
| `mikai_get_current_credit()`    | Restituisce il credito corrente in centesimi            |
| `mikai_add_cents()`             | Aggiunge centesimi al credito con log della transazione |
| `mikai_set_cents()`             | Imposta il credito esatto azzerando lo storico          |
| `mikai_write_modified_blocks()` | Scrive fisicamente i blocchi modificati sul tag         |
| `mikai_has_pending_writes()`    | Controlla se ci sono blocchi in attesa di scrittura     |
| `mikai_export_vendor()`         | Copia i blocchi vendor (0x18–0x19) in un buffer         |
| `mikai_import_vendor()`         | Sostituisce vendor e ri-cifra il credito                |
| `mikai_reset_key()`             | Ripristina i valori di fabbrica di tutti i blocchi      |
| `mikai_is_reset()`              | Verifica se il tag è nello stato di reset               |
| `mikai_check_lock_id()`         | Verifica se il Lock-ID è attivo (tag in sola lettura)   |
| `mikai_reset_otp()`             | Azzera i blocchi OTP (0x00–0x04)                        |
| `mikai_export_dump()`           | Esporta l'intera EEPROM e l'UID                         |
| `mikai_modify_block()`          | Sovrascrive un blocco arbitrario in RAM                 |

**Funzioni interne (statiche, non esposte):**

| Funzione                           | Descrizione                                         |
| ---------------------------------- | --------------------------------------------------- |
| `nfc_wait_and_select()`            | Attende e seleziona il tag (60 tentativi × 100 ms)  |
| `nfc_reselect()`                   | Ri-seleziona il tag dopo un errore di comunicazione |
| `read_block()`                     | Legge un blocco con retry automatico                |
| `write_block()`                    | Scrive un blocco con verifica e retry automatico    |
| `encode_decode_block()`            | Permutazione bit simmetrica (la propria inversa)    |
| `calculateBlockChecksum()`         | Calcola il checksum di un blocco                    |
| `calculateEncryptionKey()`         | Calcola la chiave di sessione da UID + vendor       |
| `get_current_transaction_offset()` | Legge il puntatore all'ultima transazione           |
| `days_difference()`                | Calcola giorni giuliani dalla data di produzione    |

---

## 4. Le strutture dati principali

### `srix_t` — immagine del tag in RAM

```
┌─────────────────────────────────────────────┐
│                  srix_t                     │
├─────────────────────────────────────────────┤
│ eeprom[128][4]  → copia dell'EEPROM         │
│                   128 blocchi × 4 byte      │
│                   = 512 byte totali         │
├─────────────────────────────────────────────┤
│ uid             → UID a 64 bit (uint64_t)   │
├─────────────────────────────────────────────┤
│ srixFlag        → bitmap blocchi modificati │
│                   (128 bit = 4 × uint32_t)  │
└─────────────────────────────────────────────┘
  Dimensione totale in RAM: ~520 byte
```

### `mykey_t` — handle di lavoro

```
┌─────────────────────────────────────────────┐
│                 mykey_t                     │
├─────────────────────────────────────────────┤
│ srix4k          → puntatore a srix_t        │
├─────────────────────────────────────────────┤
│ encryptionKey   → chiave di sessione        │
│                   (uint32_t)                │
└─────────────────────────────────────────────┘
```

Tutte le funzioni `mikai_*` ricevono un puntatore a `mykey_t`.
La variabile globale `srixKey` (definita in `MikaiLogic.cpp`) è
l'handle che viene usato da tutto il codice di menu.

---

## 5. La chiave di sessione (encryptionKey)

La chiave di sessione è un `uint32_t` calcolato da tre sorgenti:

```
UID del tag (uint64_t)
    │
    └──× (parte dei blocchi vendor 0x18/0x19 + 1)
            │
            ▼
        masterKey (uint32_t)
            │
            └──× OTP counter (blocco 0x06, complemento a 1 + 1)
                    │
                    ▼
              encryptionKey (uint32_t)
```

**Implicazioni pratiche:**

- Se cambi il vendor (con `mikai_import_vendor()`), la chiave cambia.
- Se il contatore OTP viene decrementato (con `mikai_reset_otp()`), la chiave cambia.
- Ogni tag ha una chiave diversa perché l'UID è diverso.
- La chiave viene ricalcolata automaticamente ogni volta che si chiama `mikai_read_tag()`.

**Come viene usata:** i blocchi che contengono credito (0x21 e 0x25) vengono
cifrati con XOR a 4 byte usando la chiave di sessione:

```cpp
block[0] ^= encryptionKey >> 24;
block[1] ^= encryptionKey >> 16;
block[2] ^= encryptionKey >> 8;
block[3] ^= encryptionKey;
```

---

## 6. Mappa della EEPROM SRIX4K

```
Blocco   Contenuto
──────   ───────────────────────────────────────────────────────────
0x00–0x04  Blocchi OTP (One Time Programmable, write-once)
0x05       Configurazione Lock-ID (byte[3] == 0x7F → Lock attivo)
0x06       Contatore OTP (little-endian, decrementabile)
0x07       Byte di configurazione sistema (usato come chiave XOR per 0x3C)
0x08       Data di produzione (BCD: gg/mm/aaaa)
0x09–0x0F  Riservati / sistema
──────   ───────────────────────────────────────────────────────────
0x10–0x13  Blocchi configurazione zona 1
0x14–0x17  Blocchi configurazione zona 2
0x18       Vendor block A  ← chiave per il calcolo di encryptionKey
0x19       Vendor block B  ← chiave per il calcolo di encryptionKey
0x1A–0x1B  Azzerati con permutazione
0x1C       Mirror cifrato di 0x18
0x1D       Mirror cifrato di 0x19
0x1E–0x1F  Azzerati con permutazione
──────   ───────────────────────────────────────────────────────────
0x20       Configurazione credito (valore fisso 0x0100)
0x21       Credito corrente (cifrato con encryptionKey) ← MODIFICATO
0x22       Data di produzione (codifica alternativa)
0x23       Credito precedente (non cifrato)             ← MODIFICATO
0x24       Mirror di 0x20
0x25       Mirror cifrato di 0x21                      ← MODIFICATO
0x26       Mirror di 0x22
0x27       Mirror di 0x23                              ← MODIFICATO
──────   ───────────────────────────────────────────────────────────
0x34–0x3B  Ring buffer transazioni (8 slot, 1 blocco per transazione)
0x3C       Puntatore all'ultima transazione (cifrato con blocco 0x07)
0x3D–0x3F  Configurazione / riservati
──────   ───────────────────────────────────────────────────────────
0x40–0x7F  Zona mirror / configurazione aggiuntiva
0x47–0x4C  Mirror dei blocchi vendor (0x18/0x19)
0x4F–0x56  Mirror dei blocchi credito (0x21/0x23/0x25/0x27)
──────   ───────────────────────────────────────────────────────────
```

> I blocchi marcati con ← MODIFICATO sono quelli scritti da `mikai_add_cents()`
> e `mikai_set_cents()`.

### Formato di un blocco transazione (0x34–0x3B)

```
Byte   Contenuto
────   ──────────────────────────────────────────
0      [7:3] giorno  [2:0] mese (bit alti)
1      [7]   mese (bit basso)  [6:0] anno (ultimi 2 cifre, es. 26 = 2026)
2      credito in centesimi (byte alto)
3      credito in centesimi (byte basso)

0xFF 0xFF 0xFF 0xFF → slot vuoto (nessuna transazione)
```

---

## 7. Il sistema di flag (srix_flag)

`srix_flag` è una bitmap a 128 bit (4 × `uint32_t`) che tiene traccia
di quali blocchi EEPROM sono stati modificati in RAM e devono essere
scritti sul tag fisico.

```
srix_flag.memory[0]  → blocchi 0x00–0x1F  (bit 0–31)
srix_flag.memory[1]  → blocchi 0x20–0x3F  (bit 32–63)
srix_flag.memory[2]  → blocchi 0x40–0x5F  (bit 64–95)
srix_flag.memory[3]  → blocchi 0x60–0x7F  (bit 96–127)
```

**API del flag-set:**

```cpp
srix_flag_init()           // azzera tutto (nessun blocco modificato)
srix_flag_add(&f, b)       // marca il blocco b come modificato
srix_flag_remove(&f, b)    // rimuove il flag dal blocco b
srix_flag_get(&f, b)       // true se il blocco b è modificato
srix_flag_isModified(&f)   // true se almeno un blocco è modificato
```

**Come funziona nel flusso di scrittura:**

```
mikai_add_cents()
    │
    ├─ modifica eeprom[0x21], [0x25], [0x23], [0x27], [0x3C], [0x34+i]
    └─ chiama srix_flag_add() per ognuno
            │
            ▼
mikai_write_modified_blocks()
    │
    ├─ scorre tutti i 128 blocchi
    ├─ per ogni srix_flag_get() == true → chiama write_block()
    └─ alla fine azzera il flag-set con srix_flag_init()
```

---

## 8. Come funziona la ricarica (mikai_add_cents)

`mikai_add_cents()` non aggiunge l'importo in un'unica operazione:
lo spezza in **step canonici** e registra ogni step come transazione separata.

### Step canonici (in centesimi)

```
200 → 100 → 50 → 20 → 10 → 5
```

### Esempio: aggiungere 3.75 EUR (375 centesimi)

```
375 = 200 + 100 + 50 + 20 + 5
       ↓     ↓    ↓    ↓    ↓
    tx[0] tx[1] tx[2] tx[3] tx[4]   → 5 slot nel ring buffer
```

### Ring buffer delle transazioni

Il tag ha 8 slot (blocchi 0x34–0x3B) organizzati in un ring buffer.
Il puntatore all'ultimo slot scritto è nel blocco 0x3C.

```
Slot 0 (0x34) ←── puntatore 0x3C
Slot 1 (0x35)
Slot 2 (0x36)
Slot 3 (0x37)
Slot 4 (0x38)
Slot 5 (0x39)
Slot 6 (0x3A)
Slot 7 (0x3B)
```

Quando il puntatore arriva a 7, riparte da 0 sovrascrivendo le transazioni più vecchie.

### Blocchi aggiornati dalla ricarica

```
0x21  → credito corrente cifrato con encryptionKey
0x25  → mirror cifrato di 0x21
0x23  → credito precedente (non cifrato)
0x27  → mirror di 0x23
0x3C  → puntatore all'ultima transazione (cifrato con blocco 0x07)
0x34+i → record della transazione (uno per ogni step)
```

---

## 9. Il vendor: cosa è e come gestirlo

Il **vendor** è la configurazione del gestore del sistema di pagamento.
È memorizzato nei blocchi 0x18 e 0x19 (8 byte totali) e determina
la chiave di sessione (`encryptionKey`) insieme all'UID del tag.

### Stati del tag rispetto al vendor

```
┌─────────────────┐    Reset       ┌─────────────────┐
│   Tag in reset  │ ─────────────→ │   Tag in reset  │
│ (vendor default)│                │ (vendor default)│
└────────┬────────┘                └─────────────────┘
         │ Import vendor
         ▼
┌─────────────────┐
│  Tag con vendor │  ← stato operativo normale
│   (bound)       │
└────────┬────────┘
         │ Reset
         ▼
┌─────────────────┐
│   Tag in reset  │
│ (vendor default)│
└─────────────────┘
```

### Flusso per configurare un nuovo tag

```
1. actionReset()         → porta il tag allo stato di reset
2. actionWrite()         → scrive il reset sul tag fisico
3. actionImportVendor()  → carica il vendor da /vendor/mikai_xxx.bin
                           (scrive automaticamente sul tag)
4. actionAddCredit()     → aggiunge il credito iniziale
5. actionWrite()         → scrive il credito sul tag fisico
```

### File vendor sulla SD

```
/vendor/
├── mikai_stoebene.bin    → 8 byte (blocco 0x18 + blocco 0x19)
├── mikai_barroma.bin
└── ...
```

I file vengono creati con `actionExportVendor()` leggendo un tag già configurato.

---

## 10. Come aggiungere una nuova azione al menu

Segui questi 3 passi nell'ordine indicato.

### Passo 1 — Dichiara la funzione in `MikaiMenuLogic.h`

```cpp
/**
 * @brief Descrizione della nuova azione.
 */
void actionNuovaFunzione();
```

### Passo 2 — Implementa la funzione in `MikaiMenuLogic.cpp`

```cpp
void actionNuovaFunzione() {
    // 1. Legge il tag se necessario
    if (!loadTag()) return;

    // 2. Chiama la funzione mikai_* appropriata
    int result = mikai_qualcosa(&srixKey, ...);

    // 3. Mostra il risultato
    if (result == 0) {
        // Eventuale sottomenu di conferma scrittura
        std::vector<Option> confirm = {
            {"Write to tag", []() { actionWrite(); }, false},
            {"Cancel",       []() {},                 false},
        };
        loopOptions(confirm, MENU_TYPE_SUBMENU, "Scrivi?");
    } else {
        char buf[40];
        snprintf(buf, sizeof(buf), "Error %d", result);
        showMessage("Nuova funzione", String(buf));
    }
}
```

### Passo 3 — Aggiungi la voce nel menu principale in `Mikai.cpp`

```cpp
void Mikai::optionsMenu() {
    std::vector<Option> options = {
        // ... voci esistenti ...
        {"Nuova funzione", []() { actionNuovaFunzione(); }, false}, // ← aggiunta
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Mikai");
}
```

> Se la nuova azione richiede logica NFC o crittografica nuova, aggiungi
> prima la funzione `mikai_*` in `MikaiLogic.h` e `MikaiLogic.cpp`,
> poi usala nel menu.

---

## 11. Flusso di una operazione tipica

```
Mikai.cpp
    │
    │  chiama (via lambda nel menu)
    ▼
MikaiMenuLogic.cpp
    │
    ├─ loadTag()
    │   ├─ drawMainBorderWithTitle("Mikai")
    │   ├─ padprintln("Place MyKey on reader...")
    │   └─ mikai_read_tag(&srixKey, &nfc)
    │           ├─ nfc_wait_and_select()     → attende tag (fino a 6 s)
    │           ├─ SRIX_get_uid()            → legge UID 64 bit
    │           ├─ read_block() × 128        → legge tutta la EEPROM
    │           └─ calculateEncryptionKey()  → calcola chiave di sessione
    │
    ├─ [logica di menu]
    │   └─ mikai_add_cents() / mikai_set_cents() / ecc.
    │           ├─ modifica srixKey.srix4k->eeprom[]
    │           └─ srix_flag_add() per ogni blocco modificato
    │
    ├─ [sottomenu conferma]
    │   └─ actionWrite()
    │           └─ mikai_write_modified_blocks(&srixKey, &nfc)
    │                   ├─ write_block() per ogni blocco flaggato
    │                   │   ├─ SRIX_write_block()
    │                   │   ├─ read_block()          → verifica
    │                   │   └─ retry se mismatch
    │                   └─ srix_flag_init()           → azzera i flag
    │
    └─ showMessage()   → mostra risultato e attende tasto
```

---

## 12. Note e avvertenze

### Separazione lettura / scrittura

A differenza del modulo Tessere dove `mifareWriteDump()` scrive tutto in un colpo,
qui la scrittura è **esplicita e separata**: le funzioni `mikai_*` modificano
solo la RAM (`srixKey`), e la scrittura fisica avviene solo quando l'utente
sceglie "Write to tag" nel sottomenu di conferma. Questo permette di annullare
qualsiasi modifica semplicemente non confermando.

### Il blocco 0x06 (contatore OTP)

Il blocco 0x06 è un contatore **monotono non-resettabile** dell'hardware SRIX4K.
Ogni volta che si chiama `mikai_reset_otp()`, il contatore viene decrementato
di `0x00200000` unità in modo **permanente e irreversibile**. Quando il contatore
arriva a 0, non è più possibile fare reset OTP. Usare con estrema cautela.

### I blocchi 0x00–0x04 (OTP)

I blocchi OTP sono **write-once**: ogni bit può essere portato da 1 a 0
ma non viceversa. Non vengono mai sovrascritti dalle operazioni normali.

### Retry automatico nella scrittura

`write_block()` non fallisce mai: in caso di errore di comunicazione
o mismatch nella verifica, ri-seleziona il tag e riprova indefinitamente.
Se il tag viene rimosso durante una scrittura, la funzione si bloccherà
finché non viene riposizionato.

### Lock-ID

Se `mikai_check_lock_id()` restituisce `true`, il tag è **in sola lettura**:
`mikai_add_cents()` e `mikai_set_cents()` restituiranno `-1` senza modificare nulla.
Non è possibile rimuovere il Lock-ID via software.

### La data nelle transazioni

Attualmente la data viene passata come parametro hardcoded (`01/01/26`)
in `actionAddCredit()` e `actionSetCredit()`. In futuro andrebbe letta
dall'RTC del dispositivo Bruce per avere date reali.

### Stato globale

Le variabili `nfc`, `srix` e `srixKey` sono globali definite in `MikaiLogic.cpp`.
Contengono sempre lo stato dell'ultimo tag letto. Se si chiama `loadTag()`
una seconda volta, lo stato precedente viene sovrascritto.

### Differenza tra `mikai_add_cents` e `mikai_set_cents`

|            | `mikai_add_cents`                  | `mikai_set_cents`                    |
| ---------- | ---------------------------------- | ------------------------------------ |
| Credito    | Aggiunge al corrente               | Imposta da zero                      |
| Storico    | Conserva le transazioni precedenti | Azzera tutto (0xFF)                  |
| Uso tipico | Ricarica normale                   | Prima configurazione o correzione    |
| Rollback   | No                                 | Sì (ripristina lo stato se fallisce) |

## 13. Miglioramenti

```
1. Reset
2. Import
```
