#pragma once

/**
 * @file TessereMenu.h
 * @brief Dichiarazione delle azioni di menu Tessere per Bruce.
 *
 * Ogni funzione corrisponde a una voce del menu Tessere ed è collegata
 * tramite lambda in Tessere.cpp. Le implementazioni si trovano in
 * TessereMenu.cpp e usano l'API di TessereLogica.h e TessereMicroel.h.
 *
 * Flusso tipico di un'azione NFC:
 *   1. Il PN532 è già inizializzato (fatto all'ingresso del menu Tessere).
 *   2. attesaTag() → attende e identifica il tag sul lettore.
 *   3. Operazione   → chiama la funzione core appropriata.
 *   4. Feedback UI  → mostra il risultato su display.
 *
 * menuGestori() non richiede NFC: opera solo su file SD.
 */

// ─── Azioni menu principale ───────────────────────────────────────────────────

/**
 * @brief Mostra UID, SAK, ATQA, tipo, gestore e presenza dump SD del tag.
 *
 * Non legge i blocchi dati: si limita al rilevamento tramite attesaTag().
 * Veloce e non richiede chiavi di autenticazione.
 */
void mostraInfoTag();

/**
 * @brief Legge il dump completo del tag e lo salva su SD.
 *
 * Salva in PERCORSO_DUMP_DIR/<UIDHEX>.bin usando le chiavi da PERCORSO_CHIAVI.
 * Al termine chiede se associare un gestore.
 */
void leggiTessera();

/**
 * @brief Scrive sul tag fisico un dump scelto dalla lista su SD.
 *
 * Elenca i file .bin presenti in PERCORSO_DUMP_DIR e lascia scegliere.
 * Prima di scrivere mostra l'UID del dump e chiede conferma.
 */
void scriviTessera();

/**
 * @brief Legge l'UID del tag e scrive automaticamente il dump corrispondente.
 *
 * Cerca PERCORSO_DUMP_DIR/<UIDHEX>.bin: se trovato lo scrive senza
 * ulteriore interazione. Altrimenti avvisa di usare "Read" prima.
 */
void scriviAutoTessera();

// ─── Azioni menu Microel ──────────────────────────────────────────────────────

/**
 * @brief Sottomenu Microel: Info, Leggi, Imposta Credito, Genera Chiavi.
 */
void menuMicroel();

// ─── Azioni menu Config ───────────────────────────────────────────────────────

/**
 * @brief Menu gestione gestori: Visualizza, Aggiungi, Modifica, Elimina.
 *
 * Non richiede NFC. Gestisce PERCORSO_GESTORI e PERCORSO_MAPPA.
 */
void menuGestori();
