/**
 * @file TessereMicroel.cpp
 */

#include "TessereMicroel.h"

// ─── Helper UI ────────────────────────────────────────────────────────────────
// mostraMessaggio() e mostraInfo() sono definite in TessereLogica.cpp

// ─── KDF: implementazione ─────────────────────────────────────────────────────

void calcolaSumHex(const uint8_t *uid, size_t dim, uint8_t sumHex[LUNGHEZZA_CHIAVE]) {
    // Chiave XOR fissa del protocollo Microel
    const uint8_t xorKey[LUNGHEZZA_CHIAVE] = {0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9};

    // Somma i byte dell'UID e riduce mod 256
    int somma = 0;
    for (size_t i = 0; i < dim; i++) somma += uid[i];
    int val = somma % 256;

    // Il protocollo richiede un valore pari; se dispari incrementa di 2
    if (val % 2 == 1) val += 2;

    // XOR con la chiave fissa per ottenere i 6 byte di sumHex
    for (size_t i = 0; i < LUNGHEZZA_CHIAVE; i++) sumHex[i] = (uint8_t)(val ^ xorKey[i]);
}

void generaChiaveA(const uint8_t *uid, uint8_t dim, uint8_t chiaveA[LUNGHEZZA_CHIAVE]) {
    uint8_t sumHex[LUNGHEZZA_CHIAVE];
    calcolaSumHex(uid, dim, sumHex);

    // Il nibble alto del primo byte determina il secondo XOR
    uint8_t primoNibble = (sumHex[0] >> 4) & 0x0F;

    if (primoNibble == 0x2 || primoNibble == 0x3 || primoNibble == 0xA || primoNibble == 0xB) {
        // Variante 1: secondo XOR con 0x40
        for (size_t i = 0; i < LUNGHEZZA_CHIAVE; i++) chiaveA[i] = 0x40 ^ sumHex[i];
    } else if (primoNibble == 0x6 || primoNibble == 0x7 || primoNibble == 0xE || primoNibble == 0xF) {
        // Variante 2: secondo XOR con 0xC0
        for (size_t i = 0; i < LUNGHEZZA_CHIAVE; i++) chiaveA[i] = 0xC0 ^ sumHex[i];
    } else {
        // Variante 3: Key A coincide con sumHex (nessun XOR aggiuntivo)
        for (size_t i = 0; i < LUNGHEZZA_CHIAVE; i++) chiaveA[i] = sumHex[i];
    }
}

void generaChiaveB(const uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]) {
    // Key B = NOT bit a bit di Key A → chiaveA XOR chiaveB = 0xFF×6
    for (size_t i = 0; i < LUNGHEZZA_CHIAVE; i++) chiaveB[i] = 0xFF ^ chiaveA[i];
}

void generaChiavi(const uint8_t *uid, uint8_t dim, uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]) {
    generaChiaveA(uid, dim, chiaveA);
    generaChiaveB(chiaveA, chiaveB);
}

bool generaChiaviDaStringa(const String &uidHex, uint8_t chiaveA[LUNGHEZZA_CHIAVE], uint8_t chiaveB[LUNGHEZZA_CHIAVE]) {
    // UID Microel = 4 byte = 8 caratteri hex
    if (uidHex.length() != LUNGHEZZA_UID * 2) return false;

    for (int i = 0; i < (int)uidHex.length(); i++) {
        if (!isHexadecimalDigit(uidHex[i])) return false;
    }

    // Converte la stringa hex in array di byte
    uint8_t uid[LUNGHEZZA_UID];
    for (int i = 0; i < LUNGHEZZA_UID; i++) { uid[i] = (uint8_t)strtol(uidHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16); }

    generaChiavi(uid, LUNGHEZZA_UID, chiaveA, chiaveB);
    return true;
}

bool salvaChiaviSD(const String &uidHex, const uint8_t chiaveA[LUNGHEZZA_CHIAVE], const uint8_t chiaveB[LUNGHEZZA_CHIAVE]) {
    if (!SD.exists(PERCORSO_RFID)) SD.mkdir(PERCORSO_RFID);
    if (!SD.exists(PERCORSO_TAG)) SD.mkdir(PERCORSO_TAG);

    // Converte le chiavi in stringhe hex maiuscole
    String strA, strB;
    for (int i = 0; i < LUNGHEZZA_CHIAVE; i++) {
        if (chiaveA[i] < 0x10) strA += '0';
        strA += String(chiaveA[i], HEX);
        if (chiaveB[i] < 0x10) strB += '0';
        strB += String(chiaveB[i], HEX);
    }
    strA.toUpperCase();
    strB.toUpperCase();

    String rigaA = uidHex + "," + strA;
    String rigaB = uidHex + "," + strB;
    bool trovataA = false, trovataB = false;

    // Controlla se le righe esistono già nel file
    if (SD.exists(PERCORSO_CHIAVI)) {
        File f = SD.open(PERCORSO_CHIAVI, FILE_READ);
        if (f) {
            while (f.available()) {
                String riga = f.readStringUntil('\n');
                riga.trim();
                if (riga == rigaA) trovataA = true;
                if (riga == rigaB) trovataB = true;
                if (trovataA && trovataB) break;
            }
            f.close();
        }
    }

    // Aggiunge solo le righe mancanti
    if (!trovataA || !trovataB) {
        File f = SD.open(PERCORSO_CHIAVI, FILE_APPEND);
        if (!f) return false;
        if (!trovataA) f.println(rigaA);
        if (!trovataB) f.println(rigaB);
        f.close();
    }
    return true;
}

// ─── Integrazione con DumpMifare ─────────────────────────────────────────────

void iniettaChiavi(DumpMifare &dump) {
    if (dump.lunghezzaUid != LUNGHEZZA_UID) return; // solo UID da 4 byte

    // Genera le chiavi KDF dall'UID del dump
    uint8_t chiaveA[LUNGHEZZA_CHIAVE], chiaveB[LUNGHEZZA_CHIAVE];
    generaChiavi(dump.uid, dump.lunghezzaUid, chiaveA, chiaveB);

    // Chiavi di default che verranno sostituite
    const uint8_t defaultFF[LUNGHEZZA_CHIAVE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t default00[LUNGHEZZA_CHIAVE] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    for (uint8_t s = 0; s < dump.numSettori; s++) {
        // Sostituisce Key A solo se è ancora quella di default
        if (memcmp(dump.chiaveA[s], defaultFF, LUNGHEZZA_CHIAVE) == 0 || memcmp(dump.chiaveA[s], default00, LUNGHEZZA_CHIAVE) == 0) {
            memcpy(dump.chiaveA[s], chiaveA, LUNGHEZZA_CHIAVE);
            dump.chiaveATrovata[s] = true;
        }
        // Sostituisce Key B solo se è ancora quella di default
        if (memcmp(dump.chiaveB[s], defaultFF, LUNGHEZZA_CHIAVE) == 0 || memcmp(dump.chiaveB[s], default00, LUNGHEZZA_CHIAVE) == 0) {
            memcpy(dump.chiaveB[s], chiaveB, LUNGHEZZA_CHIAVE);
            dump.chiaveBTrovata[s] = true;
        }
    }
}

bool leggiTesseraMicroel(uint8_t &settoriLetti) {
    if (!attesaTag()) return false;

    // Solo UID da 4 byte (requisito Microel)
    if (dump_globale.lunghezzaUid != LUNGHEZZA_UID) return false;

    iniettaChiavi(dump_globale);
    return leggiDumpConChiavi(settoriLetti);
}

// ─── Parser blocco dati ───────────────────────────────────────────────────────

void decodificaBlocco(const uint8_t dati[16], DatiBloccoMicroel &out) {
    out.numeroOperazione = (uint16_t)(dati[0] | (dati[1] << 8));
    out.totaleCarichiInput = (uint16_t)(dati[2] | (dati[3] << 8));
    out.deposito = dati[4];
    out.credito = (uint16_t)(dati[5] | (dati[6] << 8)); // LSB=byte5, MSB=byte6
    out.dataTransazione = (uint32_t)(dati[7] | (dati[8] << 8) | (dati[9] << 16) | (dati[10] << 24));
    out.punti = (uint16_t)(dati[11] | (dati[12] << 8));
    out.importoUltimaOperaz = (uint16_t)(dati[13] | (dati[14] << 8));
    out.checksum = dati[15];
}

void costruisciBlocco(const DatiBloccoMicroel &in, uint8_t dati[16]) {
    dati[0] = (uint8_t)(in.numeroOperazione & 0xFF);
    dati[1] = (uint8_t)(in.numeroOperazione >> 8);
    dati[2] = (uint8_t)(in.totaleCarichiInput & 0xFF);
    dati[3] = (uint8_t)(in.totaleCarichiInput >> 8);
    dati[4] = in.deposito;
    dati[5] = (uint8_t)(in.credito & 0xFF); // LSB credito
    dati[6] = (uint8_t)(in.credito >> 8);   // MSB credito
    dati[7] = (uint8_t)(in.dataTransazione & 0xFF);
    dati[8] = (uint8_t)((in.dataTransazione >> 8) & 0xFF);
    dati[9] = (uint8_t)((in.dataTransazione >> 16) & 0xFF);
    dati[10] = (uint8_t)((in.dataTransazione >> 24) & 0xFF);
    dati[11] = (uint8_t)(in.punti & 0xFF);
    dati[12] = (uint8_t)(in.punti >> 8);
    dati[13] = (uint8_t)(in.importoUltimaOperaz & 0xFF);
    dati[14] = (uint8_t)(in.importoUltimaOperaz >> 8);
    dati[15] = calcolaChecksum(dati); // ricalcola il checksum come ultimo passo
}

uint8_t calcolaChecksum(const uint8_t dati[16]) {
    uint16_t somma = 0;
    for (int i = 0; i < 15; i++) somma += dati[i]; // byte 15 escluso
    return (uint8_t)(somma % 256);
}

// ─── Lettura / modifica credito ───────────────────────────────────────────────

uint16_t leggiCredito(const DumpMifare &dump) {
    if (!dump.bloccLetto[BLOCCO_CREDITO]) return 0;
    const uint8_t *b = dump.dati[BLOCCO_CREDITO];
    return (uint16_t)(b[BYTE_CREDITO_BASSO] | (b[BYTE_CREDITO_ALTO] << 8));
}

void impostaCredito(DumpMifare &dump, uint16_t nuovoCredito) {
    // Copia il credito attuale nel blocco precedente (blocco 5)
    if (dump.bloccLetto[BLOCCO_CREDITO] && dump.bloccLetto[BLOCCO_CREDITO_PREC]) {
        uint16_t creditoAttuale = leggiCredito(dump);
        dump.dati[BLOCCO_CREDITO_PREC][BYTE_CREDITO_BASSO] = (uint8_t)(creditoAttuale & 0xFF);
        dump.dati[BLOCCO_CREDITO_PREC][BYTE_CREDITO_ALTO] = (uint8_t)(creditoAttuale >> 8);
        dump.dati[BLOCCO_CREDITO_PREC][15] = calcolaChecksum(dump.dati[BLOCCO_CREDITO_PREC]);
    }

    // Scrive il nuovo credito nel blocco 4
    dump.dati[BLOCCO_CREDITO][BYTE_CREDITO_BASSO] = (uint8_t)(nuovoCredito & 0xFF);
    dump.dati[BLOCCO_CREDITO][BYTE_CREDITO_ALTO] = (uint8_t)(nuovoCredito >> 8);
    dump.dati[BLOCCO_CREDITO][15] = calcolaChecksum(dump.dati[BLOCCO_CREDITO]);
}

// ─── Info card su display Bruce ───────────────────────────────────────────────

void infoTesseraMicroel() {
    mostraInfo("Microel - Info", "Avvicina la tessera\nMicroel al lettore...");

    if (!attesaTag()) return;

    if (dump_globale.lunghezzaUid != LUNGHEZZA_UID) {
        mostraMessaggio("Microel - Info", "UID non valido.\nAttesi 4 byte.\nNon e' una tessera\nMicroel.");
        return;
    }

    String uid = uidInHex(dump_globale.uid, dump_globale.lunghezzaUid);

    // Genera Key A e Key B via KDF (non lette dal tag fisico)
    uint8_t kA[LUNGHEZZA_CHIAVE], kB[LUNGHEZZA_CHIAVE];
    generaChiavi(dump_globale.uid, dump_globale.lunghezzaUid, kA, kB);

    // Converte le chiavi in stringhe hex per il display
    String strA, strB;
    for (int i = 0; i < LUNGHEZZA_CHIAVE; i++) {
        if (kA[i] < 0x10) strA += '0';
        strA += String(kA[i], HEX);
        if (kB[i] < 0x10) strB += '0';
        strB += String(kB[i], HEX);
    }
    strA.toUpperCase();
    strB.toUpperCase();

    // Mostra stato intermedio prima della lettura
    mostraInfo("Microel - Info", "UID: " + uid + "\nKeyA: " + strA + "\nKeyB: " + strB + "\nLettura credito...");

    // Legge i blocchi con le chiavi KDF
    iniettaChiavi(dump_globale);
    uint8_t settoriLetti = 0;
    leggiDumpConChiavi(settoriLetti);

    // Formatta il credito corrente
    String strCredito = "N/D";
    if (dump_globale.bloccLetto[BLOCCO_CREDITO]) {
        uint16_t c = leggiCredito(dump_globale);
        strCredito = String(c / 100) + "." + (c % 100 < 10 ? "0" : "") + String(c % 100) + " EUR";
    }

    // Formatta il credito precedente
    String strPrecedente = "N/D";
    if (dump_globale.bloccLetto[BLOCCO_CREDITO_PREC]) {
        const uint8_t *b = dump_globale.dati[BLOCCO_CREDITO_PREC];
        uint16_t p = (uint16_t)(b[BYTE_CREDITO_BASSO] | (b[BYTE_CREDITO_ALTO] << 8));
        strPrecedente = String(p / 100) + "." + (p % 100 < 10 ? "0" : "") + String(p % 100) + " EUR";
    }

    // Recupera il gestore dalla mappa SD
    String gestore = cercaGestore(uid);
    if (gestore.isEmpty()) gestore = "N/A";

    // Controlla se esiste un dump salvato per questo UID
    String percorsoDump = String(PERCORSO_DUMP_DIR) + "/" + uid + ".bin";
    String statoSD = SD.exists(percorsoDump) ? "Si" : "No";

    mostraMessaggio(
        "Microel - Info",
        "UID: " + uid + "\n" + "KeyA: " + strA + "\n" + "KeyB: " + strB + "\n" + "Credito: " + strCredito + "\n" + "Precedente: " + strPrecedente +
            "\n" + "Gestore: " + gestore + "\n" + "Dump SD: " + statoSD
    );
}

// ─── Scrittura con blocco 0 ───────────────────────────────────────────────────

bool scriviTesseraMicroel(const DumpMifare &src, uint8_t &settoriScritti, bool &blocco0Scritto) {
    // Fase 1: scrittura di tutti i blocchi normali (blocco 0 saltato internamente)
    bool ok = scriviDump(src, settoriScritti);

    // Fase 2: scrittura specifica del blocco 0 (solo tag magic)
    blocco0Scritto = scriviBloc0(src);

    return ok;
}
