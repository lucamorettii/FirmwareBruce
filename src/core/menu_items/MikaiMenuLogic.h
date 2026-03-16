/**
 * @file MikaiMenuLogic.h
 * @brief Dichiarazione delle azioni di menu Mikai per Bruce.
 *
 * Ogni funzione corrisponde a una voce del sottomenu Mikai ed è collegata
 * tramite lambda in Mikai::optionsMenu(). Le implementazioni si trovano
 * in MikaiMenuLogic.cpp e fanno uso dell'API mikai_* di MikaiLogic.h.
 *
 * Flusso tipico di un'azione:
 *   1. loadTag()        → legge il tag NFC corrente.
 *   2. Logica di menu   → chiama la funzione mikai_* appropriata.
 *   3. Conferma/Scrivi  → eventuale sottomenu con actionWrite().
 */

#pragma once

/// Mostra le informazioni del tag (credito, SK, data di produzione, transazioni).
void actionInfo();

/**
 * @brief Scrive sul tag fisico tutti i blocchi modificati in memoria.
 *
 * Viene chiamata dai sottomenu di conferma dopo actionAddCredit(),
 * actionSetCredit() e actionReset(). Può essere invocata anche da sola
 * se ci sono modifiche pendenti.
 */
void actionWrite();

/// Aggiunge un importo preselezionato al credito del tag, con conferma di scrittura.
void actionAddCredit();

/// Imposta il credito del tag a un valore esatto, azzerando lo storico, con conferma.
void actionSetCredit();

/// Esporta i blocchi vendor (0x18–0x19) in un file .bin sulla SD.
void actionExportVendor();

/// Importa blocchi vendor da un file .bin sulla SD e aggiorna la cifratura del tag.
void actionImportVendor();

/// Riporta il tag allo stato di reset di fabbrica, con conferma di scrittura.
void actionReset();
