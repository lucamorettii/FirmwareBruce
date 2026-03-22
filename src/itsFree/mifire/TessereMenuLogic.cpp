/**
 * @file TessereMenuLogic.cpp
 * @brief Implementazione delle azioni di menu Tessere (UI + bridge verso TessereLogic).
 *
 * Questo file contiene tutta la logica di presentazione: titoli, feedback
 * di avanzamento, selezione file da SD e gestione degli errori a schermo.
 * Nessuna logica NFC risiede qui: ogni operazione sul tag è delegata
 * all'API dichiarata in TessereLogic.h e TessereMicroelLogic.h.
 *
 * Helper UI interni:
 *   - showMessage() → mostra titolo + corpo e attende un tasto
 *   - showInfo()    → mostra titolo + corpo senza attendere (stati intermedi)
 *
 * Azioni implementate:
 *   - InfoTessera()      → mostra UID, SAK, ATQA, tipo e gestore del tag
 *   - ReadTessera()      → legge dump, salva su SD, chiede associazione gestore
 *   - WriteTessera()     → scrive dump selezionato dalla SD
 *   - AutoWriteTessera() → scrive automaticamente il dump per l'UID rilevato
 *   - GestoriMenu()      → gestisce la lista gestori (aggiungi/modifica/elimina)
 *   - MicroelTessera()   → sottomenu Microel (Info KDF, Read con KDF, Write + blocco 0)
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunto #include "TessereMicroelLogic.h" per le funzioni Microel.
 *   - Aggiunta implementazione completa di MicroelTessera().
 */

#include "TessereMenuLogic.h"
#include "TessereLogic.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "microel/TessereMicroelLogic.h" // microelInfoCard, microelReadCard, microelWriteCard, ecc.
#include <SD.h>

// ─── Helper UI (statici interni) ─────────────────────────────────────────────

/**
 * @brief Mostra un pannello con titolo e corpo testuale, poi attende un tasto.
 *
 * Spezza il corpo sulle righe (separatore '\n') e rispetta i margini
 * dello schermo di Bruce tramite padprintln.
 * Attende minimo 300 ms per evitare letture accidentali di tasti già premuti.
 * Da usare per risultati finali, errori, riepiloghi e conferme.
 *
 * @param title Titolo del pannello (barra superiore Bruce).
 * @param body  Testo del corpo, righe separate da '\n'.
 */
static void showMessage(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    String tmp = body;
    int start = 0, idx;
    // Scorre il testo cercando '\n' come separatore di riga
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx)); // stampa la riga senza '\n'
        start = idx + 1;
    }
    padprintln(tmp.substring(start)); // stampa l'ultima riga (priva di '\n')

    delay(300); // pausa minima per evitare lettura immediata

    // Blocca finché l'utente non preme un tasto
    while (!AnyKeyPress) {
        InputHandler();
        delay(50);
    }
}

/**
 * @brief Mostra un pannello con titolo e corpo testuale senza attendere input.
 *
 * Usata per messaggi di stato intermedi durante operazioni NFC lunghe:
 * es. "Place tag on reader...", "Reading sectors...", "Writing...".
 * Non blocca il flusso: il programma prosegue immediatamente dopo il disegno.
 *
 * @param title Titolo del pannello.
 * @param body  Testo del corpo.
 */
static void showInfo(const String &title, const String &body) {
    drawMainBorderWithTitle(title);
    setPadCursor(1, 2);

    String tmp = body;
    int start = 0, idx;
    while ((idx = tmp.indexOf('\n', start)) != -1) {
        padprintln(tmp.substring(start, idx));
        start = idx + 1;
    }
    padprintln(tmp.substring(start));
    // Nessuna attesa: il chiamante prosegue immediatamente
}

// ─── Azioni menu standard ─────────────────────────────────────────────────────

/**
 * @brief Identifica il tag sul lettore e mostra UID, SAK, ATQA, tipo e gestore.
 *
 * Non legge i blocchi dati: si limita al rilevamento tramite waitForMifareTag(),
 * quindi è veloce e non richiede chiavi di autenticazione.
 * Controlla anche se esiste un dump salvato su SD per questo UID.
 */
void InfoTessera() {
    if (!mifareInit()) return;       // inizializza PN532 se necessario
    if (!waitForMifareTag()) return; // attende il tag (timeout 6 s)

    // Costruisce la stringa UID per la ricerca del dump e del gestore
    String uidStr = buildUIDHex(g_dump.uid, g_dump.uidLen);

    // Formatta SAK e ATQA come stringhe esadecimali
    char sakStr[5], atqaStr[5];
    snprintf(sakStr, sizeof(sakStr), "%02X", g_dump.sak);
    snprintf(atqaStr, sizeof(atqaStr), "%04X", g_dump.atqa);

    // Controlla se esiste già un dump salvato per questo UID
    String dumpPath = "/rfid/dumps/" + uidStr + ".bin";
    String dumpStatus = SD.exists(dumpPath) ? "Saved: YES" : "Saved: NO";

    // Cerca il gestore associato all'UID nella mappa su SD
    String gestore = lookupGestore(uidStr);
    String gestoreStr = gestore.isEmpty() ? "unknown" : gestore;

    // Mostra tutte le informazioni in un pannello e attende un tasto
    showMessage(
        "Info",
        "UID:  " + uidStr + "\n" + "SAK:  0x" + String(sakStr) + "\n" + "ATQA: 0x" + String(atqaStr) + "\n" +
            "Type: " + g_dump.tagType + "\n" + "Gestore: " + gestoreStr + "\n" + dumpStatus
    );
}

/**
 * @brief Legge il dump completo del tag, lo salva su SD e chiede di associare un gestore.
 *
 * Flusso:
 *   1. Inizializza il PN532 e rileva il tag.
 *   2. Mostra avanzamento a schermo (senza bloccare su tasto).
 *   3. Chiama mifareReadDump() che prova le chiavi da /rfid/chiavi.txt.
 *   4. Salva il dump in /rfid/dumps/<UIDHEX>.bin.
 *   5. Mostra il riepilogo e chiede se associare un gestore.
 *      Se era già associato, mostra il gestore corrente e chiede se cambiarlo.
 */
void ReadTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);

    // Mostra stato intermedio: lettura in corso (non blocca su tasto)
    showInfo(
        "Read",
        "UID: " + uidHex + "\n" + "Type: " + g_dump.tagType + "\n" +
            "Reading sectors...\n"
            "Keep tag on reader!"
    );

    // Legge tutti i settori leggibili con le chiavi da SD
    uint8_t sectorsRead = 0;
    if (!mifareReadDump(sectorsRead)) {
        showMessage("Read", "No sectors readable.\nCheck /rfid/chiavi.txt");
        return;
    }

    // Salva il dump su SD
    if (!saveDumpToSD(g_dump, uidHex)) {
        showMessage("Read", "Read ok but SD save\nfailed!");
        return;
    }

    // Mostra il riepilogo della lettura
    showMessage(
        "Read",
        "Done!\nSectors: " + String(sectorsRead) + "/" + String(g_dump.numSectors) +
            "\nSaved:\n/rfid/dumps/" + uidHex + ".bin"
    );

    // Chiede se associare/cambiare il gestore per questa tessera
    String gestoreAttuale = lookupGestore(uidHex);
    String prompt = gestoreAttuale.isEmpty()
                        ? "Associate gestore?" // nessun gestore: chiede di aggiungerne uno
                        : "Gestore: " + gestoreAttuale + "\nChange?"; // già presente: chiede se cambiarlo

    std::vector<Option> gestoreOpts = {
        {"Yes",
         [uidHex]() {
             // Carica la lista dei gestori disponibili da SD
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage(
                     "Gestori",
                     "No gestori found.\n"
                     "Add one from\n"
                     "Gestori menu first."
                 );
                 return;
             }

             // Mostra la lista e lascia scegliere il gestore da associare
             std::vector<Option> gOpts;
             for (auto &g : list) {
                 gOpts.push_back(
                     {g.c_str(),
                      [uidHex, g]() {
                          // Salva l'associazione UID→gestore in gestori_map.txt
                          if (associateGestore(uidHex, g))
                              showMessage("Read", "Associated:\n" + uidHex + "\nGestore: " + g);
                          else showMessage("Read", "Error saving\nassociation.");
                      },
                      false}
                 );
             }
             loopOptions(gOpts, MENU_TYPE_SUBMENU, "Select gestore");
         },              false},
        {"No",  []() {}, false}, // utente rinuncia: non fa nulla
    };
    loopOptions(gestoreOpts, MENU_TYPE_SUBMENU, prompt.c_str());
}

/**
 * @brief Scrive sul tag fisico un dump scelto dall'utente dalla lista su SD.
 *
 * Flusso:
 *   1. Inizializza il PN532.
 *   2. Elenca i file .bin presenti in /rfid/dumps/.
 *   3. L'utente seleziona il file desiderato.
 *   4. Mostra l'UID del dump e chiede conferma.
 *   5. Attende il tag target (waitForAnyMifareTag: non sovrascrive g_dump).
 *   6. Se l'UID del tag fisico ≠ UID del dump, avvisa ma continua.
 *   7. Scrive il dump sul tag e mostra il riepilogo.
 *
 * @note loadedDump è dichiarata static per evitare ~5 KB di overflow sullo stack ESP32.
 */
void WriteTessera() {
    if (!mifareInit()) return;

    // Verifica che la cartella dei dump esista
    if (!SD.exists("/rfid/dumps")) {
        showMessage("Write", "No /rfid/dumps folder\non SD.");
        return;
    }

    // Elenca tutti i file .bin nella cartella dumps
    File dir = SD.open("/rfid/dumps");
    std::vector<Option> fileOpts;

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break; // fine della directory

        String name = String(entry.name());
        entry.close();

        if (!name.endsWith(".bin")) continue; // salta i file non .bin

        // Aggiunge una voce per ogni file .bin trovato
        fileOpts.push_back(
            {name.c_str(),
             [name]() {
                 String path = "/rfid/dumps/" + name;

                 // static: evita ~5 KB di overhead sullo stack dell'ESP32
                 static MifareDump loadedDump;
                 if (!loadDumpFromSD(loadedDump, path)) {
                     showMessage("Write", "Cannot load dump:\n" + path);
                     return;
                 }

                 String dumpUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);

                 // Chiede conferma prima di scrivere (operazione irreversibile sul tag)
                 std::vector<Option> confirm = {
                     {"Yes, write",
                      []() {
                          // Attende il tag target senza sovrascrivere g_dump (che contiene il dump sorgente)
                          showInfo("Write", "Place target tag\non reader...");
                          uint8_t uid[7], uidLen;
                          if (!waitForAnyMifareTag(uid, &uidLen)) {
                              showMessage("Write", "No tag found.");
                              return;
                          }

                          // Avvisa se l'UID del tag fisico ≠ UID del dump (scrittura su tag diverso)
                          String targetUID = buildUIDHex(uid, uidLen);
                          String srcUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                          if (targetUID != srcUID) {
                              showMessage(
                                  "Write",
                                  "Warning: UID mismatch!\n"
                                  "Dump: " +
                                      srcUID + "\n" + "Tag:  " + targetUID +
                                      "\n"
                                      "Continuing..."
                              );
                          }

                          // Scrittura in corso: mostra stato senza attendere tasto
                          showInfo("Write", "Writing...\nKeep tag on reader!");

                          uint8_t sectorsWritten = 0;
                          if (!mifareWriteDump(loadedDump, sectorsWritten)) {
                              showMessage("Write", "Write failed!\nNo sectors written.");
                              return;
                          }

                          // Riepilogo scrittura completata
                          showMessage(
                              "Write",
                              "Done!\nSectors: " + String(sectorsWritten) + "/" +
                                  String(loadedDump.numSectors)
                          );
                      },                     false},
                     {"Cancel",     []() {}, false}, // annulla senza scrivere
                 };
                 loopOptions(confirm, MENU_TYPE_SUBMENU);
             },
             false}
        );
    }
    dir.close();

    // Se non ci sono file .bin, avvisa l'utente
    if (fileOpts.empty()) {
        showMessage("Write", "No .bin files in\n/rfid/dumps/");
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Select dump");
}

/**
 * @brief Legge l'UID del tag e scrive automaticamente il dump corrispondente.
 *
 * Cerca /rfid/dumps/<UIDHEX>.bin: se esiste, lo carica e lo scrive senza
 * ulteriori interazioni. Se non esiste, suggerisce di usare "Read" prima.
 *
 * Caso d'uso: tessere di cui si possiede già il dump, da ripristinare
 * rapidamente avvicinando il tag al lettore senza navigare menu.
 *
 * @note loadedDump è static per evitare ~5 KB di overhead sullo stack ESP32.
 */
void AutoWriteTessera() {
    if (!mifareInit()) return;
    if (!waitForMifareTag()) return;

    String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);
    String path = "/rfid/dumps/" + uidHex + ".bin";

    Serial.printf("[TESSERE] AutoWrite: cerco %s\n", path.c_str());

    // Verifica che il dump esista per questo UID
    if (!SD.exists(path)) {
        showMessage(
            "Auto Write", "No dump found for:\n" + uidHex + "\nUse 'Read' first to\ncreate the dump."
        );
        return;
    }

    // Carica il dump dalla SD
    static MifareDump loadedDump; // static per evitare overflow stack
    if (!loadDumpFromSD(loadedDump, path)) {
        showMessage("Auto Write", "Cannot load dump.");
        return;
    }

    // Mostra stato di scrittura senza attendere tasto
    showInfo(
        "Auto Write",
        "UID: " + uidHex +
            "\n"
            "Dump found!\n"
            "Writing...\n"
            "Keep tag on reader!"
    );

    // Scrive il dump sul tag
    uint8_t sectorsWritten = 0;
    if (!mifareWriteDump(loadedDump, sectorsWritten)) {
        showMessage("Auto Write", "Write failed!\nNo sectors written.");
        return;
    }

    // Riepilogo scrittura completata
    showMessage(
        "Auto Write",
        "Done!\n"
        "UID: " +
            uidHex + "\n" + "Sectors: " + String(sectorsWritten) + "/" + String(loadedDump.numSectors)
    );
}

/**
 * @brief Menu di gestione dei gestori: Aggiungi, Modifica, Elimina.
 *
 * Non richiede NFC: opera esclusivamente su file SD.
 * Le modifiche ai nomi vengono propagate automaticamente in gestori_map.txt.
 */
void GestoriMenuTessera() {
    std::vector<Option> opts = {

        // ── Aggiungi gestore ───────────────────────────────────────────────────
        {"Add",
         []() {
             // Apre la tastiera di Bruce per inserire il nome del nuovo gestore
             String nome = keyboard("", 20, "Nome gestore:");
             if (nome.isEmpty()) {
                 showMessage("Gestori", "Cancelled.");
                 return;
             }

             // Aggiunge il gestore a gestori.txt (controlla duplicati internamente)
             if (addGestore(nome)) showMessage("Gestori", "Added:\n" + nome);
             else showMessage("Gestori", "Already exists:\n" + nome);
         }, false},

        // ── Modifica gestore ───────────────────────────────────────────────────
        {"Edit",
         []() {
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage("Gestori", "No gestori found.");
                 return;
             }

             // Mostra la lista e lascia scegliere quale rinominare
             std::vector<Option> selectOpts;
             for (auto &g : list) {
                 selectOpts.push_back(
                     {g.c_str(),
                      [g]() {
                          // Pre-compila il campo con il nome attuale per comodità di editing
                          String newNome = keyboard(g, 20, "New name:");
                          if (newNome.isEmpty() || newNome == g) {
                              showMessage("Gestori", "Cancelled.");
                              return;
                          }
                          // Rinomina in gestori.txt e aggiorna tutte le associazioni
                          if (modifyGestore(g, newNome))
                              showMessage("Gestori", "Renamed:\nOld: " + g + "\nNew: " + newNome);
                          else showMessage("Gestori", "Error renaming.");
                      },
                      false}
                 );
             }
             loopOptions(selectOpts, MENU_TYPE_SUBMENU, "Select gestore");
         }, false},

        // ── Elimina gestore ───────────────────────────────────────────────────
        {"Delete",
         []() {
             auto list = loadGestori();
             if (list.empty()) {
                 showMessage("Gestori", "No gestori found.");
                 return;
             }

             // Mostra la lista e lascia scegliere quale eliminare
             std::vector<Option> selectOpts;
             for (auto &g : list) {
                 selectOpts.push_back(
                     {g.c_str(),
                      [g]() {
                          // Chiede conferma prima di eliminare (operazione irreversibile)
                          std::vector<Option> confirm = {
                              {"Yes, delete",
         [g]() {
                                   // Elimina da gestori.txt e rimuove tutte le associazioni
                                   if (deleteGestore(g)) showMessage("Gestori", "Deleted:\n" + g);
                                   else showMessage("Gestori", "Error deleting.");
                               }, false},
                              {"Cancel", []() {}, false}, // annulla senza eliminare
                          };
                          loopOptions(confirm, MENU_TYPE_SUBMENU, ("Delete " + g + "?").c_str());
                      },
                      false}
                 );
             }
             loopOptions(selectOpts, MENU_TYPE_SUBMENU, "Select gestore");
         },           false                },
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Gestori");
}

// ─── Sottomenu Microel ────────────────────────────────────────────────────────

/**
 * @brief Sottomenu dedicato alle tessere Microel.
 *
 * Le tessere Microel usano un KDF: non servono chiavi su /rfid/chiavi.txt
 * perché Key A e Key B vengono derivate matematicamente dall'UID del tag.
 *
 * Voci del sottomenu:
 *
 *   Info  → Mostra su display: UID, Key A e Key B generate dal KDF,
 *             credito corrente, credito precedente, gestore associato.
 *             Operazione di sola lettura, non modifica nulla.
 *
 *   Read  → Legge tutti i settori usando le chiavi KDF, salva il dump su SD
 *             in /rfid/dumps/<UIDHEX>.bin, chiede associazione gestore.
 *             Identico a ReadTessera() ma usa microelReadCard() invece
 *             di mifareReadDump() direttamente.
 *
 *   Write → Scrive il dump scelto dalla SD sul tag fisico.
 *             A differenza di WriteTessera(), chiama microelWriteCard()
 *             che scrive ANCHE il blocco 0 (UID/produttore) tramite
 *             mifareWriteBlock0(). Funziona completamente solo su tag
 *             magic (CUID, GEN2, ecc.); su tag originali il blocco 0
 *             viene ignorato ma il resto della scrittura procede.
 */
void MicroelTessera() {
    if (!mifareInit()) return; // inizializza PN532 se necessario

    std::vector<Option> opts = {

        // ── Info Microel ───────────────────────────────────────────────────────
        // Delega completamente a microelInfoCard() che gestisce tutto internamente:
        // attesa tag, KDF, lettura credito, lookup gestore, visualizzazione.
        {"Info",
         []() {
             microelInfoCard(); // mostra UID, Key A/B, credito, gestore su display
         },        false                  },

        // ── Read Microel ───────────────────────────────────────────────────────
        // Come ReadTessera() ma usa le chiavi KDF invece di /rfid/chiavi.txt.
        // Salva il dump su SD e chiede di associare un gestore al termine.
        {"Read",
         []() {
             // Mostra stato di attesa mentre si aspetta il tag
             showInfo(
                 "Microel Read",
                 "Avvicina la tessera\nMicroel al lettore...\n"
                 "Keep tag on reader!"
             );

             // Legge la tessera con chiavi KDF (waitForMifareTag + injectKeys + readDump)
             uint8_t sectorsRead = 0;
             if (!microelReadCard(sectorsRead)) {
                 showMessage(
                     "Microel Read",
                     "Lettura fallita.\n"
                     "UID non valido o\n"
                     "tessera non Microel."
                 );
                 return;
             }

             String uidHex = buildUIDHex(g_dump.uid, g_dump.uidLen);

             // Salva il dump su SD in /rfid/dumps/<UIDHEX>.bin
             if (!saveDumpToSD(g_dump, uidHex)) {
                 showMessage("Microel Read", "Lettura OK\nma salvataggio SD\nfallito!");
                 return;
             }

             // Prepara la stringa del credito per il riepilogo
             String creditStr = "N/D";
             if (g_dump.blockRead[MICROEL_CREDIT_BLOCK]) {
                 uint16_t c = microelGetCredit(g_dump);
                 creditStr = String(c / 100) + "." + (c % 100 < 10 ? "0" : "") + String(c % 100) + " EUR";
             }

             // Mostra il riepilogo della lettura
             showMessage(
                 "Microel Read",
                 "Done!\n"
                 "UID: " +
                     uidHex + "\n" + "Settori: " + String(sectorsRead) + "/" + String(g_dump.numSectors) +
                     "\n" + "Credito: " + creditStr + "\n" + "Salvato:\n/rfid/dumps/" + uidHex + ".bin"
             );

             // Chiede se associare/cambiare il gestore per questa tessera
             String gestoreAttuale = lookupGestore(uidHex);
             String prompt =
                 gestoreAttuale.isEmpty() ? "Associate gestore?" : "Gestore: " + gestoreAttuale + "\nChange?";

             std::vector<Option> gestoreOpts = {
                 {       "Yes",
         [uidHex]() {
                      auto list = loadGestori();
                      if (list.empty()) {
                          showMessage(
                              "Gestori",
                              "Nessun gestore.\n"
                              "Aggiungine uno\n"
                              "dal menu Gestori."
                          );
                          return;
                      }
                      std::vector<Option> gOpts;
                      for (auto &g : list) {
                          gOpts.push_back(
                              {g.c_str(),
                               [uidHex, g]() {
                                   if (associateGestore(uidHex, g))
                                       showMessage(
                                           "Microel Read", "Associato:\n" + uidHex + "\nGestore: " + g
                                       );
                                   else showMessage("Microel Read", "Errore salvataggio.");
                               },
                               false}
                          );
                      }
                      loopOptions(gOpts, MENU_TYPE_SUBMENU, "Select gestore");
                  }, false},
                 {"No", []() {}, false},
             };
             loopOptions(gestoreOpts, MENU_TYPE_SUBMENU, prompt.c_str());
         },                  false                                         },

        // ── Write Microel ─────────────────────────────────────────────────────
        // Come WriteTessera() ma chiama microelWriteCard() invece di mifareWriteDump():
        // scrive ANCHE il blocco 0 su tag magic per clonazione completa.
        {"Write",
         []() {
             // Verifica che la cartella dei dump esista
             if (!SD.exists("/rfid/dumps")) {
                 showMessage("Microel Write", "No /rfid/dumps\nfolder on SD.");
                 return;
             }

             // Elenca i file .bin disponibili
             File dir = SD.open("/rfid/dumps");
             std::vector<Option> fileOpts;

             while (true) {
                 File entry = dir.openNextFile();
                 if (!entry) break;
                 String name = String(entry.name());
                 entry.close();
                 if (!name.endsWith(".bin")) continue; // salta file non .bin

                 fileOpts.push_back(
                     {name.c_str(),
                      [name]() {
                          String path = "/rfid/dumps/" + name;

                          // static: evita ~5 KB di overhead sullo stack ESP32
                          static MifareDump loadedDump;
                          if (!loadDumpFromSD(loadedDump, path)) {
                              showMessage("Microel Write", "Cannot load dump:\n" + path);
                              return;
                          }

                          // Recupera UID e gestore del dump per il menu di conferma
                          String dumpUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                          String gestore = lookupGestore(dumpUID);
                          String gStr = gestore.isEmpty() ? "N/A" : gestore;

                          // Chiede conferma mostrando UID e gestore del dump selezionato
                          std::vector<Option> confirm = {
                              {"Yes, write",
         []() {
                                   // Attende il tag target senza sovrascrivere g_dump
                                   showInfo("Microel Write", "Avvicina il tag\ntarget al lettore...");

                                   uint8_t uid[7], uidLen;
                                   if (!waitForAnyMifareTag(uid, &uidLen)) {
                                       showMessage("Microel Write", "Nessun tag trovato.");
                                       return;
                                   }

                                   // Avvisa se l'UID del tag fisico ≠ UID del dump
                                   String targetUID = buildUIDHex(uid, uidLen);
                                   String srcUID = buildUIDHex(loadedDump.uid, loadedDump.uidLen);
                                   if (targetUID != srcUID) {
                                       showMessage(
                                           "Microel Write",
                                           "UID diverso!\n"
                                           "Dump: " +
                                               srcUID + "\n" + "Tag:  " + targetUID +
                                               "\n"
                                               "Continuo..."
                                       );
                                   }

                                   // Mostra stato di scrittura (incluso blocco 0)
                                   showInfo(
                                       "Microel Write",
                                       "Scrittura...\n"
                                       "Keep tag on reader!\n"
                                       "(incluso blocco 0)"
                                   );

                                   // microelWriteCard = mifareWriteDump + mifareWriteBlock0
                                   uint8_t sectorsWritten = 0;
                                   bool block0Written = false;
                                   if (!microelWriteCard(loadedDump, sectorsWritten, block0Written)) {
                                       showMessage(
                                           "Microel Write",
                                           "Scrittura fallita!\n"
                                           "Nessun settore scritto."
                                       );
                                       return;
                                   }

                                   // Informa se il blocco 0 è stato scritto (tag magic) o no (tag originale)
                                   String b0Str = block0Written ? "SI (tag magic)" : "NO (tag originale)";

                                   showMessage(
                                       "Microel Write",
                                       "Done!\n"
                                       "Settori: " +
                                           String(sectorsWritten) + "/" + String(loadedDump.numSectors) +
                                           "\n" + "Blocco 0: " + b0Str
                                   );
                               }, false},
                              {"Cancel", []() {}, false}, // annulla senza scrivere
                          };

                          // Titolo del menu di conferma mostra UID e gestore del dump
                          loopOptions(
                              confirm,
                              MENU_TYPE_SUBMENU,
                              ("Scrivi " + dumpUID + "\nGestore: " + gStr + "?").c_str()
                          );
                      },
                      false}
                 );
             }
             dir.close();

             if (fileOpts.empty()) {
                 showMessage("Microel Write", "No .bin files in\n/rfid/dumps/");
                 return;
             }

             loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Select dump Microel");
         },        false                               },
         
        // ── Genera Chiavi da UID ──────────────────────────────────────────────────
        // Nessun tag fisico richiesto: calcola Key A e Key B da UID inserito
        // manualmente, mostra il risultato e salva in /rfid/chiavi.txt.
        {"Genera Chiavi",
         []() {
             // Apre la tastiera Bruce per l'inserimento dell'UID
             String uidInput = keyboard("", 8, "UID (8 char hex):");

             if (uidInput.isEmpty()) {
                 showMessage("Microel", "Operazione annullata.");
                 return;
             }

             uidInput.toUpperCase(); // normalizza per display e salvataggio

             // Genera le chiavi: valida internamente lunghezza e caratteri hex
             uint8_t keyA[MICROEL_KEY_LENGTH], keyB[MICROEL_KEY_LENGTH];
             if (!microelGenerateKeysFromString(uidInput, keyA, keyB)) {
                 showMessage(
                     "Genera Chiavi",
                     "UID non valido.\n"
                     "Inserisci esattamente\n"
                     "8 caratteri hex.\n"
                     "Es: 1E733840"
                 );
                 return;
             }

             // Converte le chiavi in stringhe hex per il display
             String keyAStr, keyBStr;
             for (int i = 0; i < MICROEL_KEY_LENGTH; i++) {
                 if (keyA[i] < 0x10) keyAStr += '0';
                 keyAStr += String(keyA[i], HEX);
                 if (keyB[i] < 0x10) keyBStr += '0';
                 keyBStr += String(keyB[i], HEX);
             }
             keyAStr.toUpperCase();
             keyBStr.toUpperCase();

             // Salva in chiavi.txt
             bool saved = microelSaveKeysToSD(uidInput, keyA, keyB);
             String savedStr = saved ? "Salvate in chiavi.txt" : "Errore salvataggio SD";

             // Mostra risultato e attende tasto
             showMessage(
                 "Genera Chiavi",
                 "UID:  " + uidInput + "\n" + "KeyA: " + keyAStr + "\n" + "KeyB: " + keyBStr + "\n" + savedStr
             );
         },false},
    };

    // Mostra il sottomenu Microel con titolo "Microel"
    loopOptions(opts, MENU_TYPE_SUBMENU, "Microel");
}
