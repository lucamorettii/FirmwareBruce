/**
 * @file MikaiMenu.h
 * @brief Dichiarazione delle azioni di menu Mikai per Bruce.
 *
 * Ogni funzione corrisponde a una voce del sottomenu Mikai ed è collegata
 * tramite lambda in Mikai::optionsMenu(). Le implementazioni si trovano
 * in MikaiMenu.cpp e fanno uso dell'API mikai_* di MikaiLogic.h.
 *
 * Flusso tipico di un'azione:
 *   1. loadTag()       → legge il tag NFC corrente.
 *   2. Logica di menu  → chiama la funzione mikai_* appropriata.
 *   3. Conferma/Scrivi → eventuale sottomenu con actionWrite().
 */

#pragma once

/// Mostra le informazioni complete del tag (credito, SK, data di produzione, transazioni).
void actionInfo();

/**
 * @brief Scrive sul tag fisico tutti i blocchi modificati in memoria.
 *
 * Viene richiamata dai sottomenu di conferma dopo actionAddCredit(),
 * actionSetCredit() e actionReset(). Può essere invocata anche direttamente
 * se esistono modifiche in sospeso.
 */
void actionWrite();

/// Aggiunge un importo preselezionato (0.05–50.00 EUR) al credito del tag, con conferma di scrittura.
void actionAddCredit();

/// Imposta il credito del tag a un valore esatto, azzerando lo storico, con conferma di scrittura.
void actionSetCredit();

/**
 * @brief Esporta i blocchi vendor (0x18–0x19) in un file .bin sulla SD.
 *
 * Il file viene salvato in /rfid/mikai/vendor/mikai_<nome>.bin (8 byte).
 */
void actionExportVendor();

/**
 * @brief Importa blocchi vendor da un file .bin in /rfid/mikai/vendor/ e aggiorna la cifratura del tag.
 *
 * Richiede che il tag sia in stato di reset prima dell'importazione.
 * Esegue una ri-lettura del tag nel sottomenu di conferma per garantire
 * che il tag non sia stato sostituito tra la selezione del file e la scrittura.
 */
void actionImportVendor();

/// Riporta il tag allo stato di reset di fabbrica, con conferma di scrittura.
void actionReset();

/**
 * @brief Esporta il dump completo dell'EEPROM del tag in un file .bin sulla SD.
 *
 * Il file viene salvato in /rfid/mikai/dump/<UID>.bin.
 * Formato: 8 byte UID (little-endian) + 512 byte EEPROM = 520 byte totali.
 */
void actionDump();
