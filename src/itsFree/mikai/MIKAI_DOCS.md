# Modulo Mikai — Struttura e Architettura

## Panoramica

Il modulo **Mikai** integra in [Bruce](https://github.com/pr3y/Bruce) la gestione dei tag NFC **SRIX4K**, comunemente noti come _MyKey_, utilizzati nei sistemi di pagamento con chiave a ricarica.

---

## Struttura dei file

```
itsfree/mikai/
├── Mikai.h             # Dichiarazione della classe Bruce (MenuItemInterface)
├── Mikai.cpp           # Menu principale e icona launcher
├── MikaiLogica.h       # API core mikai_*, strutture dati e bitmap flag
├── MikaiLogica.cpp     # Crittografia, bridge NFC e logica core
├── MikaiMenu.h         # Dichiarazione delle azioni di menu
└── MikaiMenu.cpp       # Implementazione delle azioni UI
```

---

## Menu principale

| Voce                 | Descrizione                                                                 |
| -------------------- | --------------------------------------------------------------------------- |
| **Info**             | Legge il tag e mostra credito, SK, data di produzione e storico transazioni |
| **Aggiungi credito** | Aggiunge un importo predefinito (0.05–50.00 EUR) al credito corrente        |
| **Imposta credito**  | Imposta il credito a un valore esatto azzerando lo storico                  |
| **Reset**            | Riporta il tag allo stato di reset di fabbrica                              |
| **Esporta vendor**   | Salva i blocchi vendor (0x18–0x19) su SD                                    |
| **Importa vendor**   | Carica blocchi vendor da file e ricalcola la cifratura                      |
| **Dump**             | Esporta l'intera EEPROM + UID su SD                                         |

---

## Struttura SD

```
SD/
└── rfid/
    └── mikai/
        ├── vendor/
        │   └── mikai_<nome>.bin   # 8 byte: blocco 0x18 (4B) + blocco 0x19 (4B)
        └── dump/
            └── <UID_HEX>.bin      # 520 byte: UID 8B (little-endian) + EEPROM 512B
```

---

## Flusso operativo (prima configurazione di una chiave)

```
1. Reset            → porta il tag allo stato di fabbrica
2. Scrivi sul tag   → conferma la scrittura del reset
3. Importa vendor   → seleziona il file .bin da /rfid/mikai/vendor/
4. Aggiungi credito → sceglie l'importo da aggiungere
5. Scrivi sul tag   → conferma e scrive il credito sul tag
```

---

## Mappa dei blocchi SRIX4K

| Blocco(i)   | Contenuto                                                                 |
| ----------- | ------------------------------------------------------------------------- |
| `0x00–0x04` | Contatori OTP (non azzerabili senza costo)                                |
| `0x05`      | Flag Lock-ID (byte `[3] == 0x7F` → tag in sola lettura)                   |
| `0x06`      | Contatore OTP principale (determina SK insieme ai vendor)                 |
| `0x07`      | Parametri di sistema (usati come chiave XOR per il puntatore transazioni) |
| `0x08`      | Data di produzione in formato BCD                                         |
| `0x10–0x17` | Metadati di configurazione (giorni produzione, config)                    |
| `0x18–0x19` | **Blocchi vendor** — identificano il gestore del sistema                  |
| `0x1A–0x1F` | Dati di sistema e mirror vendor                                           |
| `0x20–0x27` | Credito corrente (`0x21`, `0x25`) e precedente (`0x23`, `0x27`)           |
| `0x34–0x3B` | **Log transazioni** — ring buffer di 8 slot                               |
| `0x3C`      | Puntatore all'ultima transazione (cifrato con blocco `0x07`)              |
| `0x40–0x56` | Mirror dei blocchi di configurazione                                      |
| `0x57–0x7F` | Non utilizzati (`0xFF`)                                                   |

> **Nota:** I blocchi `0x21` e `0x25` contengono il credito cifrato con la chiave di sessione (SK).
> I blocchi `0x23` e `0x27` contengono il credito precedente **non cifrato** (solo permutazione bit).

---

## Chiave di sessione (SK)

La SK è un valore `uint32_t` calcolato da tre componenti hardware:

```
OTP        = (~blocco_0x06_LE) + 1                  // contatore OTP
vendorVal  = decodifica(b18)[2..3] | decodifica(b19)[2..3]
masterKey  = UID × (vendorVal + 1)                  // troncato a 32 bit
SK         = masterKey × OTP                         // troncato a 32 bit
```

La funzione `decodifica` è una **permutazione bit simmetrica** (applicarla due volte = identità):
ogni coppia di bit `[7:6]`, `[5:4]`, `[3:2]`, `[1:0]` di ciascun byte viene distribuita
sui 4 byte di output per colonna.

> **Conseguenza:** cambiare i blocchi vendor (Importa vendor) richiede di ri-cifrare
> i blocchi credito con la nuova SK, operazione eseguita automaticamente da `mikai_import_vendor()`.

---

## Formato blocco transazione

Ogni slot nel ring buffer (`0x34` – `0x3B`) occupa 4 byte:

| Bit                           | Contenuto                         |
| ----------------------------- | --------------------------------- |
| `byte[0][7:3]`                | Giorno (dd)                       |
| `byte[0][2:0]` + `byte[1][7]` | Mese (mm)                         |
| `byte[1][6:0]`                | Anno − 2000 (yy)                  |
| `byte[2..3]`                  | Credito in centesimi (big-endian) |

Il puntatore al blocco corrente è in `0x3C`, cifrato XOR con i byte `[1..3]` del blocco `0x07`,
dopo permutazione bit.

---

## Formato file dump (`.bin`, 520 byte)

| Offset | Dimensione | Contenuto                               |
| ------ | ---------- | --------------------------------------- |
| `0`    | 8 B        | UID del tag (`uint64_t`, little-endian) |
| `8`    | 512 B      | EEPROM completa (128 blocchi × 4 byte)  |

Il nome del file è `<UID_HEX>.bin` (es. `D002AABBCCDDEEFF.bin`).

---

## Formato file vendor (`.bin`, 8 byte)

| Offset | Dimensione | Contenuto         |
| ------ | ---------- | ----------------- |
| `0`    | 4 B        | Blocco `0x18` raw |
| `4`    | 4 B        | Blocco `0x19` raw |

---

## Analisi offline

Usa lo script Python incluso per analizzare i file salvati sulla SD:

```bash
# Solo dump
python3 mikai_info.py --dump D002AABBCCDDEEFF.bin

# Solo vendor
python3 mikai_info.py --vendor mikai_gestore.bin

# Entrambi
python3 mikai_info.py --dump D002AABBCCDDEEFF.bin --vendor mikai_gestore.bin
```

Lo script ricalcola la SK, mostra il credito decifrato, lo storico transazioni
e stampa il dump completo dei blocchi in esadecimale.
