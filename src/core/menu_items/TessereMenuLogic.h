/**
 * @file TessereMenuLogic.h
 * @brief Dichiarazione delle azioni di menu Tessere per Bruce.
 *
 * Ogni funzione corrisponde a una voce del sottomenu Tessere ed è collegata
 * tramite lambda in Tessere::optionsMenu(). Le implementazioni si trovano
 * in TessereMenuLogic.cpp e fanno uso dell'API dichiarata in TessereLogic.h.
 *
 * Flusso tipico di un'azione NFC:
 *   1. mifareInit()       → inizializza il PN532 (lazy).
 *   2. waitForMifareTag() → attende e identifica il tag.
 *   3. Operazione         → chiama la funzione core appropriata.
 *   4. Feedback UI        → mostra il risultato a schermo.
 *
 * GestoriMenu() non richiede NFC: opera solo su file SD.
 */

#pragma once

/// Mostra UID, SAK, ATQA, tipo e gestore del tag senza leggere i dati EEPROM.
void InfoTessera();

/**
 * @brief Legge il dump completo del tag e lo salva su SD.
 *
 * Il file viene salvato in /rfid/dumps/<UIDHEX>.bin.
 * Le chiavi vengono caricate automaticamente da /rfid/chiavi.txt.
 * Al termine chiede se associare un gestore dalla lista in /rfid/gestori.txt.
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

/**
 * @brief Menu di gestione dei gestori: Aggiungi, Modifica, Elimina.
 *
 * Non richiede NFC. Gestisce la lista nomi in /rfid/gestori.txt e
 * le associazioni UID→gestore in /rfid/gestori_map.txt.
 *
 * Voci del sottomenu:
 *   - Aggiungi → inserisce un nuovo nome da tastiera
 *   - Modifica → rinomina un gestore esistente (aggiorna anche le associazioni)
 *   - Elimina  → rimuove un gestore e tutte le sue associazioni UID
 */
void GestoriMenu();
