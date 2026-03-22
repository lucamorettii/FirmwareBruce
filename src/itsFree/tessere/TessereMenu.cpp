/**
 * @file TessereMenu.cpp
 */

#include "TessereMenu.h"
#include "TessereLogica.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "microel/TessereMicroel.h"
#include <SD.h>

// ─── Helper UI ────────────────────────────────────────────────────────────────
// mostraMessaggio() e mostraInfo() sono definite in TessereLogica.cpp

// Helper: presenta il sottomenu di associazione gestore dopo una lettura
static void sottomenAAssociaGestore(const String &uidHex) {
    String gestoreCorrente = cercaGestore(uidHex);
    String prompt = gestoreCorrente.isEmpty() ? "Associa gestore?" : "Gestore: " + gestoreCorrente + "\nCambiare?";

    std::vector<Option> opzioni = {
        {"Si",
         [uidHex]() {
             auto lista = caricaGestori();
             if (lista.empty()) {
                 mostraMessaggio(
                     "Gestori",
                     "Nessun gestore trovato.\n"
                     "Aggiungine uno dal\n"
                     "menu Config > Gestori."
                 );
                 return;
             }
             // Mostra la lista dei gestori e lascia scegliere
             std::vector<Option> listaOpt;
             for (auto &g : lista) {
                 listaOpt.push_back(
                     {g.c_str(),
                      [uidHex, g]() {
                          if (associaGestore(uidHex, g)) mostraMessaggio("Gestori", "Associato:\n" + uidHex + "\nGestore: " + g);
                          else mostraMessaggio("Gestori", "Errore salvataggio.");
                      },
                      false}
                 );
             }
             loopOptions(listaOpt, MENU_TYPE_SUBMENU, "Seleziona gestore");
         },             false},
        {"No", []() {}, false},
    };
    loopOptions(opzioni, MENU_TYPE_SUBMENU, prompt.c_str());
}

// ─── Info ─────────────────────────────────────────────────────────────────────

void mostraInfoTag() {
    if (!attesaTag()) return;

    String uid = uidInHex(dump_globale.uid, dump_globale.lunghezzaUid);

    // Formatta SAK e ATQA come hex
    char strSak[5], strAtqa[5];
    snprintf(strSak, sizeof(strSak), "%02X", dump_globale.sak);
    snprintf(strAtqa, sizeof(strAtqa), "%04X", dump_globale.atqa);

    String gestore = cercaGestore(uid);
    String strGestore = gestore.isEmpty() ? "N/A" : gestore;

    // Controlla se esiste un dump salvato per questo UID
    String percorsoDump = String(PERCORSO_DUMP_DIR) + "/" + uid + ".bin";
    String statoSD = SD.exists(percorsoDump) ? "Si" : "No";

    mostraMessaggio(
        "Info",
        "UID:     " + uid + "\n" + "SAK:     0x" + String(strSak) + "\n" + "ATQA:    0x" + String(strAtqa) + "\n" +
            "Tipo:    " + dump_globale.tipoTag + "\n" + "Gestore: " + strGestore + "\n" + "Dump SD: " + statoSD
    );
}

// ─── Leggi ────────────────────────────────────────────────────────────────────

void leggiTessera() {
    if (!attesaTag()) return;

    String uid = uidInHex(dump_globale.uid, dump_globale.lunghezzaUid);

    // Mostra stato di avanzamento (non blocca su tasto)
    mostraInfo(
        "Leggi",
        "UID: " + uid + "\n" + "Tipo: " + dump_globale.tipoTag + "\n" +
            "Lettura settori...\n"
            "Non rimuovere la tessera!"
    );

    uint8_t settoriLetti = 0;
    if (!leggiDump(settoriLetti)) {
        mostraMessaggio(
            "Leggi",
            "Nessun settore leggibile.\n"
            "Controlla " PERCORSO_CHIAVI
        );
        return;
    }

    // Salva il dump su SD
    if (!salvaDump(dump_globale, uid)) {
        mostraMessaggio("Leggi", "Lettura OK\nma salvataggio SD\nfallito!");
        return;
    }

    mostraMessaggio(
        "Leggi",
        "Fatto!\n"
        "Settori: " +
            String(settoriLetti) + "/" + String(dump_globale.numSettori) +
            "\n"
            "Salvato:\n" +
            String(PERCORSO_DUMP_DIR) + "/" + uid + ".bin"
    );

    sottomenAAssociaGestore(uid);
}

// ─── Scrivi ───────────────────────────────────────────────────────────────────

void scriviTessera() {
    if (!SD.exists(PERCORSO_DUMP_DIR)) {
        mostraMessaggio("Scrivi", "Cartella\n" PERCORSO_DUMP_DIR "\nnon trovata sulla SD.");
        return;
    }

    File dir = SD.open(PERCORSO_DUMP_DIR);
    std::vector<Option> fileOpts;

    while (true) {
        File voce = dir.openNextFile();
        if (!voce) break;
        String nome = String(voce.name());
        voce.close();
        if (!nome.endsWith(".bin")) continue;

        fileOpts.push_back(
            {nome.c_str(),
             [nome]() {
                 String percorso = String(PERCORSO_DUMP_DIR) + "/" + nome;

                 static DumpMifare dumpCaricato; // static per evitare ~5 KB di overflow stack
                 if (!caricaDump(dumpCaricato, percorso)) {
                     mostraMessaggio("Scrivi", "Impossibile caricare:\n" + percorso);
                     return;
                 }

                 String uidDump = uidInHex(dumpCaricato.uid, dumpCaricato.lunghezzaUid);

                 std::vector<Option> conferma = {
                     {"Si, scrivi",
                      []() {
                          mostraInfo("Scrivi", "Avvicina il tag\ntarget al lettore...");

                          uint8_t uid[7], lunghezza;
                          if (!attesaTagQualsiasi(uid, &lunghezza)) {
                              mostraMessaggio("Scrivi", "Nessun tag trovato.");
                              return;
                          }

                          // Avvisa se l'UID del tag fisico ≠ UID del dump
                          String uidTarget = uidInHex(uid, lunghezza);
                          String uidSorgente = uidInHex(dumpCaricato.uid, dumpCaricato.lunghezzaUid);
                          if (uidTarget != uidSorgente) {
                              mostraMessaggio(
                                  "Scrivi",
                                  "Attenzione: UID diverso!\n"
                                  "Dump: " +
                                      uidSorgente +
                                      "\n"
                                      "Tag:  " +
                                      uidTarget +
                                      "\n"
                                      "Continuo..."
                              );
                          }

                          mostraInfo("Scrivi", "Scrittura...\nNon rimuovere il tag!");

                          uint8_t settoriScritti = 0;
                          if (!scriviDump(dumpCaricato, settoriScritti)) {
                              mostraMessaggio("Scrivi", "Scrittura fallita!\nNessun settore scritto.");
                              return;
                          }

                          mostraMessaggio("Scrivi", "Fatto!\nSettori: " + String(settoriScritti) + "/" + String(dumpCaricato.numSettori));
                      },                     false},
                     {"Annulla",    []() {}, false},
                 };
                 loopOptions(conferma, MENU_TYPE_SUBMENU, ("Scrivi " + uidDump + "?").c_str());
             },
             false}
        );
    }
    dir.close();

    if (fileOpts.empty()) {
        mostraMessaggio("Scrivi", "Nessun file .bin in\n" PERCORSO_DUMP_DIR);
        return;
    }

    loopOptions(fileOpts, MENU_TYPE_SUBMENU, "Seleziona dump");
}

// ─── Scrivi Auto ──────────────────────────────────────────────────────────────

void scriviAutoTessera() {
    if (!attesaTag()) return;

    String uid = uidInHex(dump_globale.uid, dump_globale.lunghezzaUid);
    String percorso = String(PERCORSO_DUMP_DIR) + "/" + uid + ".bin";

    if (!SD.exists(percorso)) {
        mostraMessaggio("Scrivi Auto", "Dump non trovato per:\n" + uid + "\nUsa 'Read' prima per\ncrearlo.");
        return;
    }

    static DumpMifare dumpCaricato; // static per evitare ~5 KB di overflow stack
    if (!caricaDump(dumpCaricato, percorso)) {
        mostraMessaggio("Scrivi Auto", "Impossibile caricare il dump.");
        return;
    }

    mostraInfo(
        "Scrivi Auto",
        "UID: " + uid +
            "\n"
            "Dump trovato!\n"
            "Scrittura...\n"
            "Non rimuovere il tag!"
    );

    uint8_t settoriScritti = 0;
    if (!scriviDump(dumpCaricato, settoriScritti)) {
        mostraMessaggio("Scrivi Auto", "Scrittura fallita!\nNessun settore scritto.");
        return;
    }

    mostraMessaggio(
        "Scrivi Auto",
        "Fatto!\nUID: " + uid +
            "\n"
            "Settori: " +
            String(settoriScritti) + "/" + String(dumpCaricato.numSettori)
    );
}

// ─── Menu Microel ─────────────────────────────────────────────────────────────

void menuMicroel() {
    std::vector<Option> opzioni = {

        // Info: mostra UID, Key A/B, credito, gestore, dump SD
        {"Info",            []() { infoTesseraMicroel(); }, false},

        // Leggi: lettura con chiavi KDF, salva dump, chiede gestore
        {"Leggi",
         []() {
             mostraInfo(
                 "Microel - Leggi",
                 "Avvicina la tessera\nMicroel al lettore...\n"
                 "Non rimuovere!"
             );

             uint8_t settoriLetti = 0;
             if (!leggiTesseraMicroel(settoriLetti)) {
                 mostraMessaggio(
                     "Microel - Leggi",
                     "Lettura fallita.\n"
                     "Tessera non Microel\no UID non valido."
                 );
                 return;
             }

             String uid = uidInHex(dump_globale.uid, dump_globale.lunghezzaUid);

             if (!salvaDump(dump_globale, uid)) {
                 mostraMessaggio("Microel - Leggi", "Lettura OK\nma salvataggio SD\nfallito!");
                 return;
             }

             // Formatta il credito per il riepilogo
             String strCredito = "N/D";
             if (dump_globale.bloccLetto[BLOCCO_CREDITO]) {
                 uint16_t c = leggiCredito(dump_globale);
                 strCredito = String(c / 100) + "." + (c % 100 < 10 ? "0" : "") + String(c % 100) + " EUR";
             }

             mostraMessaggio(
                 "Microel - Leggi",
                 "Fatto!\n"
                 "UID: " +
                     uid +
                     "\n"
                     "Settori: " +
                     String(settoriLetti) + "/" + String(dump_globale.numSettori) +
                     "\n"
                     "Credito: " +
                     strCredito +
                     "\n"
                     "Salvato: " +
                     String(PERCORSO_DUMP_DIR) + "/" + uid + ".bin"
             );

             sottomenAAssociaGestore(uid);
         },                                                 false},

        // Imposta Credito: legge la tessera, mostra credito attuale, chiede nuovo importo
        {"Imposta Credito",
         []() {
             mostraInfo("Microel - Credito", "Avvicina la tessera\nMicroel al lettore...");

             uint8_t settoriLetti = 0;
             if (!leggiTesseraMicroel(settoriLetti)) {
                 mostraMessaggio(
                     "Microel - Credito",
                     "Lettura fallita.\n"
                     "Tessera non Microel."
                 );
                 return;
             }

             uint16_t creditoAttuale = leggiCredito(dump_globale);
             String strAttuale = String(creditoAttuale / 100) + "." + (creditoAttuale % 100 < 10 ? "0" : "") + String(creditoAttuale % 100) + " EUR";

             // Importi disponibili in centesimi e relative etichette
             const uint16_t importi[] = {500, 1000, 1500, 2000, 2500};
             const char *etichette[] = {" 5.00 EUR", "10.00 EUR", "15.00 EUR", "20.00 EUR", "25.00 EUR"};

             std::vector<Option> sceltaImporto;
             for (int i = 0; i < 5; i++) {
                 uint16_t imp = importi[i];
                 sceltaImporto.push_back(
                     {etichette[i],
                      [imp]() {
                          // Aggiorna il credito in RAM
                          impostaCredito(dump_globale, imp);

                          mostraInfo(
                              "Microel - Credito",
                              "Scrittura in corso...\n"
                              "Non rimuovere la tessera!"
                          );

                          uint8_t settoriScritti = 0;
                          bool blocco0Scritto = false;
                          if (!scriviTesseraMicroel(dump_globale, settoriScritti, blocco0Scritto)) {
                              mostraMessaggio(
                                  "Microel - Credito",
                                  "Scrittura fallita!\n"
                                  "Nessun settore scritto."
                              );
                              return;
                          }

                          String nuovoStr = String(imp / 100) + "." + (imp % 100 < 10 ? "0" : "") + String(imp % 100) + " EUR";
                          mostraMessaggio(
                              "Microel - Credito",
                              "Fatto!\n"
                              "Nuovo credito: " +
                                  nuovoStr +
                                  "\n"
                                  "Settori: " +
                                  String(settoriScritti)
                          );
                      },
                      false}
                 );
             }
             loopOptions(sceltaImporto, MENU_TYPE_SUBMENU, ("Credito: " + strAttuale).c_str());
         },                                                 false},

        // Genera Chiavi: calcola Key A e Key B da UID inserito manualmente
        {"Genera Chiavi",
         []() {
             // Tastiera Bruce per l'inserimento dell'UID
             String uidInput = keyboard("", 8, "UID (8 caratteri hex):");
             if (uidInput.isEmpty()) {
                 mostraMessaggio("Genera Chiavi", "Operazione annullata.");
                 return;
             }
             uidInput.toUpperCase();

             uint8_t kA[LUNGHEZZA_CHIAVE], kB[LUNGHEZZA_CHIAVE];
             if (!generaChiaviDaStringa(uidInput, kA, kB)) {
                 mostraMessaggio(
                     "Genera Chiavi",
                     "UID non valido.\n"
                     "Inserisci 8 caratteri\n"
                     "esadecimali.\n"
                     "Es: 1E733840"
                 );
                 return;
             }

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

             // Chiede se salvare le chiavi su SD
             std::vector<Option> salvata = {
                 {"Salva su SD",
         [uidInput, kA, kB]() {
                      bool ok = salvaChiaviSD(uidInput, kA, kB);
                      String strA2, strB2;
                      for (int i = 0; i < LUNGHEZZA_CHIAVE; i++) {
                          if (kA[i] < 0x10) strA2 += '0';
                          strA2 += String(kA[i], HEX);
                          if (kB[i] < 0x10) strB2 += '0';
                          strB2 += String(kB[i], HEX);
                      }
                      strA2.toUpperCase();
                      strB2.toUpperCase();
                      String stato = ok ? "Salvate in\n" PERCORSO_CHIAVI : "Errore salvataggio SD";
                      mostraMessaggio("Genera Chiavi", "UID:  " + uidInput + "\n" + "KeyA: " + strA2 + "\n" + "KeyB: " + strB2 + "\n" + stato);
                  }, false},
                 {"Solo mostra",
         [uidInput, strA, strB]() {
                      mostraMessaggio("Genera Chiavi", "UID:  " + uidInput + "\n" + "KeyA: " + strA + "\n" + "KeyB: " + strB);
                  }, false},
             };
             loopOptions(salvata, MENU_TYPE_SUBMENU, ("UID: " + uidInput).c_str());
         },                                                           false                                                                },
    };

    loopOptions(opzioni, MENU_TYPE_SUBMENU, "Microel");
}

// ─── Menu Gestori ─────────────────────────────────────────────────────────────

void menuGestori() {
    std::vector<Option> opzioni = {

        // Visualizza: mostra la lista di tutti i gestori
        {"Visualizza",
         []() {
             auto lista = caricaGestori();
             if (lista.empty()) {
                 mostraMessaggio("Gestori", "Nessun gestore presente.");
                 return;
             }
             String testo;
             for (auto &g : lista) testo += g + "\n";
             testo.trim();
             mostraMessaggio("Gestori", testo);
         }, false},

        // Aggiungi: inserisce un nuovo gestore via tastiera
        {"Aggiungi",
         []() {
             String nome = keyboard("", 20, "Nome gestore:");
             if (nome.isEmpty()) {
                 mostraMessaggio("Gestori", "Annullato.");
                 return;
             }
             if (aggiungiGestore(nome)) mostraMessaggio("Gestori", "Aggiunto:\n" + nome);
             else mostraMessaggio("Gestori", "Gia' presente:\n" + nome);
         }, false},

        // Modifica: rinomina un gestore esistente
        {"Modifica",
         []() {
             auto lista = caricaGestori();
             if (lista.empty()) {
                 mostraMessaggio("Gestori", "Nessun gestore trovato.");
                 return;
             }
             std::vector<Option> selezione;
             for (auto &g : lista) {
                 selezione.push_back(
                     {g.c_str(),
                      [g]() {
                          // Pre-compila con il nome attuale per comodità
                          String nuovoNome = keyboard(g, 20, "Nuovo nome:");
                          if (nuovoNome.isEmpty() || nuovoNome == g) {
                              mostraMessaggio("Gestori", "Annullato.");
                              return;
                          }
                          if (modificaGestore(g, nuovoNome)) mostraMessaggio("Gestori", "Rinominato:\n" + g + " → " + nuovoNome);
                          else mostraMessaggio("Gestori", "Errore durante la modifica.");
                      },
                      false}
                 );
             }
             loopOptions(selezione, MENU_TYPE_SUBMENU, "Seleziona gestore");
         }, false},

        // Elimina: rimuove un gestore e le sue associazioni
        {"Elimina",
         []() {
             auto lista = caricaGestori();
             if (lista.empty()) {
                 mostraMessaggio("Gestori", "Nessun gestore trovato.");
                 return;
             }
             std::vector<Option> selezione;
             for (auto &g : lista) {
                 selezione.push_back(
                     {g.c_str(),
                      [g]() {
                          // Chiede conferma prima di eliminare (operazione irreversibile)
                          std::vector<Option> conferma = {
                              {"Si, elimina",
         [g]() {
                                   if (eliminaGestore(g)) mostraMessaggio("Gestori", "Eliminato:\n" + g);
                                   else mostraMessaggio("Gestori", "Errore durante l'eliminazione.");
                               }, false},
                              {"Annulla", []() {}, false},
                          };
                          loopOptions(conferma, MENU_TYPE_SUBMENU, ("Elimina " + g + "?").c_str());
                      },
                      false}
                 );
             }
             loopOptions(selezione, MENU_TYPE_SUBMENU, "Seleziona gestore");
         },           false                },
    };

    loopOptions(opzioni, MENU_TYPE_SUBMENU, "Gestori");
}
