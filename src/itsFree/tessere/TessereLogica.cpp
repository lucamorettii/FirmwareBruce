/**
 * @file TessereLogica.cpp
 */

#include "TessereLogica.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <Adafruit_PN532.h>
#include <Wire.h>

// ─── Stato del modulo ─────────────────────────────────────────────────────────

static Adafruit_PN532 nfc(255, 255); // driver PN532 in modalità I²C (IRQ/RST non usati)
static bool nfcPronto = false;       // flag di inizializzazione lazy

DumpMifare dump_globale; // immagine RAM del tag in lavorazione (extern in .h)

// ─── Helper UI ────────────────────────────────────────────────────────────────

// Stampa il corpo riga per riga usando '\n' come separatore
static void stampaCOrpo(const String &corpo) {
    String tmp = corpo;
    int inizio = 0, idx;
    while ((idx = tmp.indexOf('\n', inizio)) != -1) {
        padprintln(tmp.substring(inizio, idx));
        inizio = idx + 1;
    }
    padprintln(tmp.substring(inizio));
}

void mostraMessaggio(const String &titolo, const String &corpo) {
    drawMainBorderWithTitle(titolo);
    setPadCursor(1, 2);
    stampaCOrpo(corpo);
    delay(300); // pausa minima per evitare lettura accidentale del tasto
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

void mostraInfo(const String &titolo, const String &corpo) {
    drawMainBorderWithTitle(titolo);
    setPadCursor(1, 2);
    stampaCOrpo(corpo);
    // nessuna attesa: il chiamante prosegue subito
}

// ─── Helper tipo tag ─────────────────────────────────────────────────────────

String descrizioneTag(uint8_t sak) {
    if (sak == 0x08) return "Mifare 1K";
    if (sak == 0x18) return "Mifare 4K";
    if (sak == 0x09) return "Mifare Mini";
    if (sak == 0x00) return "Mifare UL";
    if (sak == 0x28) return "Mifare 1K (SmartMX)";
    if (sak == 0x38) return "Mifare 4K (SmartMX)";
    return "Sconosciuto (SAK:" + String(sak, HEX) + ")";
}

uint8_t numSettoriDaSak(uint8_t sak) {
    if (sak == 0x18) return 40; // 4K: 32 + 8 estesi
    if (sak == 0x09) return 5;  // Mini
    return 16;                  // 1K e altri
}

uint8_t bloccoTrailer(uint8_t settore) {
    if (settore < 32) return settore * 4 + 3;
    return 32 * 4 + (settore - 32) * 16 + 15; // zona estesa 4K
}

uint8_t primoBlocco(uint8_t settore) {
    if (settore < 32) return settore * 4;
    return 32 * 4 + (settore - 32) * 16;
}

uint8_t blocchiNelSettore(uint8_t settore) { return (settore < 32) ? 4 : 16; }

// ─── Utilità UID ─────────────────────────────────────────────────────────────

String uidInHex(const uint8_t *uid, uint8_t lunghezza) {
    String s;
    s.reserve(lunghezza * 2);
    for (uint8_t i = 0; i < lunghezza; i++) {
        if (uid[i] < 0x10) s += '0'; // padding per byte < 0x10
        s += String(uid[i], HEX);
    }
    s.toUpperCase();
    return s;
}

// ─── Inizializzazione PN532 ──────────────────────────────────────────────────

bool inizializzaNfc() {
    if (nfcPronto) return true; // già pronto, non reinizializzare

    // Legge i pin I²C dalla configurazione hardware di Bruce
    Wire.begin(bruceConfigPins.i2c_bus.sda, bruceConfigPins.i2c_bus.scl);
    Wire.setClock(100000); // 100 kHz, modalità standard

    nfc.begin();

    // getFirmwareVersion() ritorna 0 se il PN532 non risponde
    nfcPronto = (nfc.getFirmwareVersion() != 0);

    if (!nfcPronto) {
        displayError("PN532 non trovato.", true);
        return false;
    }

    nfc.SAMConfig(); // configura SAM per la lettura passiva ISO14443A
    return true;
}

// ─── Rilevazione tag ─────────────────────────────────────────────────────────

bool attesaTag() {
    drawMainBorderWithTitle("Mifare");
    setPadCursor(1, 2);
    padprintln("Avvicina la tessera...");

    uint8_t uid[7], lunghezza;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &lunghezza, 6000)) {
        mostraMessaggio("Mifare", "Nessun tag trovato.");
        return false;
    }

    // Copia l'identità del tag in dump_globale
    memcpy(dump_globale.uid, uid, lunghezza);
    dump_globale.lunghezzaUid = lunghezza;
    dump_globale.sak = nfc.getLastSAK();
    dump_globale.atqa = nfc.getLastATQA();
    dump_globale.tipoTag = descrizioneTag(dump_globale.sak);
    dump_globale.numSettori = numSettoriDaSak(dump_globale.sak);

    // Azzera i flag di stato della sessione precedente
    memset(dump_globale.bloccLetto, 0, sizeof(dump_globale.bloccLetto));
    memset(dump_globale.chiaveATrovata, 0, sizeof(dump_globale.chiaveATrovata));
    memset(dump_globale.chiaveBTrovata, 0, sizeof(dump_globale.chiaveBTrovata));
    memset(dump_globale.chiaveA, 0, sizeof(dump_globale.chiaveA));
    memset(dump_globale.chiaveB, 0, sizeof(dump_globale.chiaveB));

    return true;
}

bool attesaTagQualsiasi(uint8_t *uid, uint8_t *lunghezza) {
    // Lettura passiva senza toccare dump_globale
    return nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, lunghezza, 6000);
}

// ─── Gestione chiavi ─────────────────────────────────────────────────────────

std::vector<std::array<uint8_t, 6>> caricaChiavi(const uint8_t *uid, uint8_t lunghezza) {
    std::vector<std::array<uint8_t, 6>> chiavi;

    // Chiavi di fabbrica sempre presenti e provate per prime
    chiavi.push_back({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    chiavi.push_back({0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    String uidStr = uidInHex(uid, lunghezza);

    if (!SD.exists(PERCORSO_CHIAVI)) return chiavi;
    File f = SD.open(PERCORSO_CHIAVI, FILE_READ);
    if (!f) return chiavi;

    while (f.available()) {
        String riga = f.readStringUntil('\n');
        riga.trim();
        if (riga.isEmpty()) continue;

        String hexChiave;
        int virgola = riga.indexOf(',');

        if (virgola >= 0) {
            // Formato: uid,chiave
            String rigaUid = riga.substring(0, virgola);
            hexChiave = riga.substring(virgola + 1);
            rigaUid.trim();
            hexChiave.trim();
            rigaUid.toUpperCase();
            hexChiave.toUpperCase();
            // Salta se l'UID non corrisponde al tag corrente
            if (!rigaUid.isEmpty() && rigaUid != uidStr) continue;
        } else {
            // Formato legacy: solo la chiave (valida per tutti)
            hexChiave = riga;
            hexChiave.toUpperCase();
        }

        if (hexChiave.length() != 12) continue; // deve essere esattamente 6 byte

        std::array<uint8_t, 6> chiave;
        bool valida = true;
        for (int i = 0; i < 6 && valida; i++) {
            String bs = hexChiave.substring(i * 2, i * 2 + 2);
            for (char c : bs) {
                if (!isHexadecimalDigit(c)) {
                    valida = false;
                    break;
                }
            }
            if (valida) chiave[i] = (uint8_t)strtol(bs.c_str(), nullptr, 16);
        }
        if (valida) chiavi.push_back(chiave);
    }
    f.close();
    return chiavi;
}

// ─── Gestione gestori ─────────────────────────────────────────────────────────

// Helper interno: sostituisce il file dst con il contenuto di src tramite file temporaneo
static void sostituisciFile(const String &src, const String &dst) {
    SD.remove(dst);
    File fr = SD.open(src, FILE_READ);
    File fw = SD.open(dst, FILE_WRITE);
    if (fr && fw) {
        while (fr.available()) fw.write(fr.read());
    }
    if (fr) fr.close();
    if (fw) fw.close();
    SD.remove(src);
}

std::vector<String> caricaGestori() {
    std::vector<String> lista;
    if (!SD.exists(PERCORSO_GESTORI)) return lista;
    File f = SD.open(PERCORSO_GESTORI, FILE_READ);
    if (!f) return lista;
    while (f.available()) {
        String riga = f.readStringUntil('\n');
        riga.trim();
        if (!riga.isEmpty()) lista.push_back(riga);
    }
    f.close();
    return lista;
}

bool aggiungiGestore(const String &nome) {
    for (auto &g : caricaGestori()) {
        if (g == nome) return false; // già presente
    }
    if (!SD.exists(PERCORSO_RFID)) SD.mkdir(PERCORSO_RFID);
    if (!SD.exists(PERCORSO_TAG)) SD.mkdir(PERCORSO_TAG);
    File f = SD.open(PERCORSO_GESTORI, FILE_APPEND);
    if (!f) return false;
    f.println(nome);
    f.close();
    return true;
}

bool eliminaGestore(const String &nome) {
    auto lista = caricaGestori();
    bool trovato = false;

    // Riscrive gestori.txt saltando il nome da eliminare
    File fw = SD.open("/rfid/tag/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : lista) {
        if (g == nome) {
            trovato = true;
            continue;
        }
        fw.println(g);
    }
    fw.close();
    sostituisciFile("/rfid/tag/gestori_tmp.txt", PERCORSO_GESTORI);

    // Rimuove le associazioni di questo gestore dalla mappa
    if (SD.exists(PERCORSO_MAPPA)) {
        File fm = SD.open(PERCORSO_MAPPA, FILE_READ);
        File fm2 = SD.open("/rfid/tag/mappa_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String riga = fm.readStringUntil('\n');
                riga.trim();
                int virgola = riga.indexOf(',');
                if (virgola >= 0 && riga.substring(virgola + 1) == nome) continue;
                if (!riga.isEmpty()) fm2.println(riga);
            }
        }
        if (fm) fm.close();
        if (fm2) fm2.close();
        sostituisciFile("/rfid/tag/mappa_tmp.txt", PERCORSO_MAPPA);
    }
    return trovato;
}

bool modificaGestore(const String &vecchioNome, const String &nuovoNome) {
    auto lista = caricaGestori();
    bool trovato = false;

    // Aggiorna gestori.txt con il nuovo nome
    File fw = SD.open("/rfid/tag/gestori_tmp.txt", FILE_WRITE);
    if (!fw) return false;
    for (auto &g : lista) {
        if (g == vecchioNome) {
            fw.println(nuovoNome);
            trovato = true;
        } else fw.println(g);
    }
    fw.close();
    sostituisciFile("/rfid/tag/gestori_tmp.txt", PERCORSO_GESTORI);

    // Aggiorna la mappa sostituendo il vecchio nome con il nuovo
    if (SD.exists(PERCORSO_MAPPA)) {
        File fm = SD.open(PERCORSO_MAPPA, FILE_READ);
        File fm2 = SD.open("/rfid/tag/mappa_tmp.txt", FILE_WRITE);
        if (fm && fm2) {
            while (fm.available()) {
                String riga = fm.readStringUntil('\n');
                riga.trim();
                if (riga.isEmpty()) continue;
                int virgola = riga.indexOf(',');
                if (virgola >= 0 && riga.substring(virgola + 1) == vecchioNome) fm2.println(riga.substring(0, virgola + 1) + nuovoNome);
                else fm2.println(riga);
            }
        }
        if (fm) fm.close();
        if (fm2) fm2.close();
        sostituisciFile("/rfid/tag/mappa_tmp.txt", PERCORSO_MAPPA);
    }
    return trovato;
}

bool associaGestore(const String &uidHex, const String &nome) {
    if (!SD.exists(PERCORSO_RFID)) SD.mkdir(PERCORSO_RFID);
    if (!SD.exists(PERCORSO_TAG)) SD.mkdir(PERCORSO_TAG);

    std::vector<String> righe;
    bool aggiornato = false;

    if (SD.exists(PERCORSO_MAPPA)) {
        File f = SD.open(PERCORSO_MAPPA, FILE_READ);
        if (f) {
            while (f.available()) {
                String riga = f.readStringUntil('\n');
                riga.trim();
                if (riga.isEmpty()) continue;
                int virgola = riga.indexOf(',');
                if (virgola >= 0 && riga.substring(0, virgola) == uidHex) {
                    righe.push_back(uidHex + "," + nome); // sovrascrive l'associazione
                    aggiornato = true;
                } else {
                    righe.push_back(riga);
                }
            }
            f.close();
        }
    }
    if (!aggiornato) righe.push_back(uidHex + "," + nome);

    SD.remove(PERCORSO_MAPPA);
    File fw = SD.open(PERCORSO_MAPPA, FILE_WRITE);
    if (!fw) return false;
    for (auto &l : righe) fw.println(l);
    fw.close();
    return true;
}

String cercaGestore(const String &uidHex) {
    if (!SD.exists(PERCORSO_MAPPA)) return "";
    File f = SD.open(PERCORSO_MAPPA, FILE_READ);
    if (!f) return "";
    while (f.available()) {
        String riga = f.readStringUntil('\n');
        riga.trim();
        int virgola = riga.indexOf(',');
        if (virgola < 0) continue;
        String uid = riga.substring(0, virgola);
        String nome = riga.substring(virgola + 1);
        uid.trim();
        nome.trim();
        uid.toUpperCase();
        if (uid == uidHex) {
            f.close();
            return nome;
        }
    }
    f.close();
    return "";
}

// ─── Lettura dump (MIFARE Classic) ───────────────────────────────────────────

static bool leggiDumpClassic(uint8_t &settoriLetti) {
    auto chiavi = caricaChiavi(dump_globale.uid, dump_globale.lunghezzaUid);
    settoriLetti = 0;

    for (uint8_t s = 0; s < dump_globale.numSettori; s++) {
        uint8_t trailer = bloccoTrailer(s);
        uint8_t primo = primoBlocco(s);
        uint8_t quanti = blocchiNelSettore(s);
        bool autenticato = false;

        // Prova tutte le chiavi come Key A
        for (auto &chiave : chiavi) {
            if (nfc.mifareclassic_AuthenticateBlock(dump_globale.uid, dump_globale.lunghezzaUid, trailer, 0, chiave.data())) {
                memcpy(dump_globale.chiaveA[s], chiave.data(), 6);
                dump_globale.chiaveATrovata[s] = true;
                autenticato = true;
                break;
            }
            // Re-selezione del tag dopo un'auth fallita
            nfc.inRelease(1);
            nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, dump_globale.uid, &dump_globale.lunghezzaUid, 1000);
        }

        // Prova tutte le chiavi come Key B (indipendentemente da Key A)
        for (auto &chiave : chiavi) {
            if (dump_globale.chiaveATrovata[s])
                nfc.mifareclassic_AuthenticateBlock(dump_globale.uid, dump_globale.lunghezzaUid, trailer, 0, dump_globale.chiaveA[s]);
            if (nfc.mifareclassic_AuthenticateBlock(dump_globale.uid, dump_globale.lunghezzaUid, trailer, 1, chiave.data())) {
                memcpy(dump_globale.chiaveB[s], chiave.data(), 6);
                dump_globale.chiaveBTrovata[s] = true;
                break;
            }
            nfc.inRelease(1);
            nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, dump_globale.uid, &dump_globale.lunghezzaUid, 1000);
        }

        if (!autenticato) continue; // nessuna chiave trovata: salta il settore

        // Legge tutti i blocchi del settore
        for (uint8_t b = 0; b < quanti; b++) {
            uint8_t numBlocco = primo + b;
            if (nfc.mifareclassic_ReadDataBlock(numBlocco, dump_globale.dati[numBlocco])) dump_globale.bloccLetto[numBlocco] = true;
        }

        // Ripristina le chiavi nel trailer (l'hardware le oscura durante la lettura)
        if (dump_globale.chiaveATrovata[s]) memcpy(dump_globale.dati[trailer], dump_globale.chiaveA[s], 6);      // Key A: byte 0–5
        if (dump_globale.chiaveBTrovata[s]) memcpy(dump_globale.dati[trailer] + 10, dump_globale.chiaveB[s], 6); // Key B: byte 10–15

        settoriLetti++;
    }
    return settoriLetti > 0;
}

// Lettura MIFARE Ultralight (nessuna autenticazione richiesta)
static bool leggiDumpUltralight(uint8_t &pagineLette) {
    pagineLette = 0;
    for (uint8_t pagina = 0; pagina < MAX_PAGINE_UL; pagina++) {
        uint8_t buf[4];
        if (!nfc.ntag2xx_ReadPage(pagina, buf)) break; // fine della memoria
        memcpy(dump_globale.dati[pagina], buf, 4);
        memset(dump_globale.dati[pagina] + 4, 0, 12); // padding a 16 byte
        dump_globale.bloccLetto[pagina] = true;
        pagineLette++;
    }
    return pagineLette > 0;
}

bool leggiDump(uint8_t &settoriLetti) {
    if (dump_globale.sak == 0x00) return leggiDumpUltralight(settoriLetti);
    return leggiDumpClassic(settoriLetti);
}

// Variante che usa le chiavi già in dump_globale (per Microel dopo KDF inject)
static bool leggiDumpClassicConChiavi(uint8_t &settoriLetti) {
    settoriLetti = 0;

    for (uint8_t s = 0; s < dump_globale.numSettori; s++) {
        uint8_t trailer = bloccoTrailer(s);
        uint8_t primo = primoBlocco(s);
        uint8_t quanti = blocchiNelSettore(s);
        bool autenticato = false;

        // Tentativo con Key A dal dump
        if (dump_globale.chiaveATrovata[s]) {
            if (nfc.mifareclassic_AuthenticateBlock(dump_globale.uid, dump_globale.lunghezzaUid, primo, 0, dump_globale.chiaveA[s]))
                autenticato = true;
            else {
                nfc.inRelease(1);
                nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, dump_globale.uid, &dump_globale.lunghezzaUid, 1000);
            }
        }

        // Fallback con Key B dal dump
        if (!autenticato && dump_globale.chiaveBTrovata[s]) {
            if (nfc.mifareclassic_AuthenticateBlock(dump_globale.uid, dump_globale.lunghezzaUid, primo, 1, dump_globale.chiaveB[s]))
                autenticato = true;
            else {
                nfc.inRelease(1);
                nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, dump_globale.uid, &dump_globale.lunghezzaUid, 1000);
            }
        }

        if (!autenticato) continue;

        for (uint8_t b = 0; b < quanti; b++) {
            uint8_t numBlocco = primo + b;
            if (nfc.mifareclassic_ReadDataBlock(numBlocco, dump_globale.dati[numBlocco])) {
                dump_globale.bloccLetto[numBlocco] = true;
            } else {
                nfc.inRelease(1);
                nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, dump_globale.uid, &dump_globale.lunghezzaUid, 1000);
                break;
            }
        }

        // Ripristina le chiavi nel trailer
        if (dump_globale.chiaveATrovata[s]) memcpy(dump_globale.dati[trailer], dump_globale.chiaveA[s], 6);
        if (dump_globale.chiaveBTrovata[s]) memcpy(dump_globale.dati[trailer] + 10, dump_globale.chiaveB[s], 6);

        settoriLetti++;
    }
    return settoriLetti > 0;
}

bool leggiDumpConChiavi(uint8_t &settoriLetti) {
    if (dump_globale.sak == 0x00) return false; // UL non usa chiavi
    return leggiDumpClassicConChiavi(settoriLetti);
}

// ─── Scrittura dump ───────────────────────────────────────────────────────────

static bool scriviDumpClassic(const DumpMifare &src, uint8_t &settoriScritti) {
    settoriScritti = 0;

    for (uint8_t s = 0; s < src.numSettori; s++) {
        uint8_t trailer = bloccoTrailer(s);
        uint8_t primo = primoBlocco(s);
        uint8_t quanti = blocchiNelSettore(s);
        bool autenticato = false;

        if (src.chiaveATrovata[s]) {
            if (nfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.lunghezzaUid, trailer, 0, const_cast<uint8_t *>(src.chiaveA[s])
                ))
                autenticato = true;
        }
        if (!autenticato && src.chiaveBTrovata[s]) {
            if (nfc.mifareclassic_AuthenticateBlock(
                    const_cast<uint8_t *>(src.uid), src.lunghezzaUid, trailer, 1, const_cast<uint8_t *>(src.chiaveB[s])
                ))
                autenticato = true;
        }

        if (!autenticato) continue;

        bool settoreOk = true;
        for (uint8_t b = 0; b < quanti; b++) {
            uint8_t numBlocco = primo + b;
            if (numBlocco == 0) continue;             // blocco 0: read-only su tag originali
            if (!src.bloccLetto[numBlocco]) continue; // blocco non presente nel dump

            if (!nfc.mifareclassic_WriteDataBlock(numBlocco, const_cast<uint8_t *>(src.dati[numBlocco]))) settoreOk = false;
        }
        if (settoreOk) settoriScritti++;
    }
    return settoriScritti > 0;
}

static bool scriviDumpUltralight(const DumpMifare &src, uint8_t &pagineScritte) {
    pagineScritte = 0;
    for (uint8_t pagina = 4; pagina < MAX_PAGINE_UL; pagina++) {
        if (!src.bloccLetto[pagina]) continue; // pagina non nel dump
        uint8_t buf[4];
        memcpy(buf, src.dati[pagina], 4);
        if (nfc.ntag2xx_WritePage(pagina, buf)) pagineScritte++;
    }
    return pagineScritte > 0;
}

bool scriviDump(const DumpMifare &sorgente, uint8_t &settoriScritti) {
    if (sorgente.sak == 0x00) return scriviDumpUltralight(sorgente, settoriScritti);
    return scriviDumpClassic(sorgente, settoriScritti);
}

bool scriviBloc0(const DumpMifare &sorgente) {
    if (!sorgente.bloccLetto[0]) return false; // blocco 0 non nel dump

    bool autenticato = false;

    // Autentica il settore 0 (trailer = blocco 3)
    if (sorgente.chiaveATrovata[0]) {
        autenticato = nfc.mifareclassic_AuthenticateBlock(
            const_cast<uint8_t *>(sorgente.uid), sorgente.lunghezzaUid, 3, 0, const_cast<uint8_t *>(sorgente.chiaveA[0])
        );
    }
    if (!autenticato && sorgente.chiaveBTrovata[0]) {
        autenticato = nfc.mifareclassic_AuthenticateBlock(
            const_cast<uint8_t *>(sorgente.uid), sorgente.lunghezzaUid, 3, 1, const_cast<uint8_t *>(sorgente.chiaveB[0])
        );
    }

    if (!autenticato) return false;

    return nfc.mifareclassic_WriteDataBlock(0, const_cast<uint8_t *>(sorgente.dati[0]));
}

// ─── Persistenza su SD ───────────────────────────────────────────────────────

bool salvaDump(const DumpMifare &dump, const String &uidHex) {
    if (!SD.exists(PERCORSO_RFID)) SD.mkdir(PERCORSO_RFID);
    if (!SD.exists(PERCORSO_TAG)) SD.mkdir(PERCORSO_TAG);
    if (!SD.exists(PERCORSO_DUMP_DIR)) SD.mkdir(PERCORSO_DUMP_DIR);

    String percorso = String(PERCORSO_DUMP_DIR) + "/" + uidHex + ".bin";
    File f = SD.open(percorso, FILE_WRITE);
    if (!f) return false;

    // Header
    f.write((const uint8_t *)DUMP_FIRMA, 4);
    uint8_t ver = DUMP_VERSIONE;
    f.write(&ver, 1);
    f.write(&dump.lunghezzaUid, 1);
    f.write(dump.uid, 7);
    f.write(&dump.sak, 1);
    f.write((const uint8_t *)&dump.atqa, 2);
    f.write(&dump.numSettori, 1);

    // Chiavi
    f.write((const uint8_t *)dump.chiaveA, sizeof(dump.chiaveA));
    f.write((const uint8_t *)dump.chiaveATrovata, sizeof(dump.chiaveATrovata));
    f.write((const uint8_t *)dump.chiaveB, sizeof(dump.chiaveB));
    f.write((const uint8_t *)dump.chiaveBTrovata, sizeof(dump.chiaveBTrovata));

    // Dati EEPROM
    f.write((const uint8_t *)dump.bloccLetto, sizeof(dump.bloccLetto));
    f.write((const uint8_t *)dump.dati, sizeof(dump.dati));

    f.close();
    return true;
}

bool caricaDump(DumpMifare &dump, const String &percorso) {
    File f = SD.open(percorso, FILE_READ);
    if (!f) return false;

    // Verifica firma magic
    char firma[4];
    f.read((uint8_t *)firma, 4);
    if (memcmp(firma, DUMP_FIRMA, 4) != 0) {
        f.close();
        return false;
    }

    // Verifica versione
    uint8_t ver;
    f.read(&ver, 1);
    if (ver != DUMP_VERSIONE) {
        f.close();
        return false;
    }

    // Legge header
    f.read(&dump.lunghezzaUid, 1);
    f.read(dump.uid, 7);
    f.read(&dump.sak, 1);
    f.read((uint8_t *)&dump.atqa, 2);
    f.read(&dump.numSettori, 1);

    // Ripristina campi derivati
    dump.tipoTag = descrizioneTag(dump.sak);

    // Chiavi
    f.read((uint8_t *)dump.chiaveA, sizeof(dump.chiaveA));
    f.read((uint8_t *)dump.chiaveATrovata, sizeof(dump.chiaveATrovata));
    f.read((uint8_t *)dump.chiaveB, sizeof(dump.chiaveB));
    f.read((uint8_t *)dump.chiaveBTrovata, sizeof(dump.chiaveBTrovata));

    // Dati EEPROM
    f.read((uint8_t *)dump.bloccLetto, sizeof(dump.bloccLetto));
    f.read((uint8_t *)dump.dati, sizeof(dump.dati));

    f.close();
    return true;
}
