/**
 * @file TessereMenuLogic.h
 * @brief Dichiarazione delle azioni di menu Tessere per Bruce.
 *
 * Ogni funzione corrisponde a una voce del sottomenu Tessere ed è collegata
 * tramite lambda in Tessere::optionsMenu(). Le implementazioni si trovano
 * in TessereMenuLogic.cpp e fanno uso dell'API dichiarata in TessereLogic.h.
 *
 * Flusso tipico di un'azione:
 *   1. mifareInit()       → inizializza il PN532 (lazy).
 *   2. waitForMifareTag() → attende e identifica il tag.
 *   3. Operazione         → chiama la funzione core appropriata.
 *   4. Feedback UI        → mostra il risultato a schermo.
 */

#pragma once

/// Mostra UID, SAK, ATQA e tipo del tag senza leggere i dati.
void InfoTessera();

/**
 * @brief Legge il dump completo del tag e lo salva su SD.
 *
 * Il file viene salvato in /rfid/dumps/<UIDHEX>.bin.
 * Le chiavi vengono caricate automaticamente da /rfid/chiavi.txt.
 */
void ReadTessera();

/**
 * @brief Scrive sul tag un dump selezionato dalla lista dei file su SD.
 *
 * Elenca i file .bin in /rfid/dumps/ e lascia scegliere all'utente.
 * Prima di scrivere mostra l'UID del dump e chiede conferma.
 */
void WriteTessera();

/**
 * @brief Legge l'UID del tag e scrive automaticamente il dump corrispondente.
 *
 * Cerca /rfid/dumps/<UIDHEX>.bin sulla SD: se trovato lo scrive
 * senza ulteriore interazione, altrimenti avvisa l'utente.
 */
void AutoWriteTessera();
