/**
 * @file TessereMenuLogic.h
 * @brief Dichiarazione delle azioni di menu Tessere per Bruce.
 *
 * Ogni funzione corrisponde a una voce del sottomenu Tessere ed è collegata
 * tramite lambda in Tessere::optionsMenu(). Le implementazioni si trovano
 * in TessereMenuLogic.cpp e fanno uso dell'API dichiarata in TessereLogic.h.
 *
 * Flusso tipico di un'azione NFC:
 *   1. mifareInit()       → inizializza il PN532 (lazy, una sola volta).
 *   2. waitForMifareTag() → attende e identifica il tag sul lettore.
 *   3. Operazione         → chiama la funzione core appropriata.
 *   4. Feedback UI        → mostra il risultato a schermo.
 *
 * GestoriMenu() non richiede NFC: opera solo su file SD.
 *
 * Modifiche rispetto alla versione originale:
 *   - Aggiunta dichiarazione di MicroelTessera().
 */

#pragma once

// ─── Azioni menu standard ─────────────────────────────────────────────────────

/**
 * @brief Mostra UID, SAK, ATQA, tipo e gestore del tag senza leggere i blocchi dati.
 *
 * Questa funzione è veloce perché non autentica né legge i settori: si limita
 * al rilevamento del tag tramite waitForMifareTag() che popola solo l'identità.
 * Mostra anche se esiste un dump salvato su SD per questo UID.
 */
void InfoTessera();

/**
 * @brief Legge il dump completo del tag e lo salva su SD.
 *
 * Il file viene salvato in /rfid/dumps/<UIDHEX>.bin.
 * Le chiavi di autenticazione vengono caricate automaticamente da /rfid/chiavi.txt
 * (formato CSV uid,chiave oppure solo chiave per chiavi generiche).
 * Al termine chiede se associare un gestore dalla lista in /rfid/gestori.txt.
 */
void ReadTessera();

/**
 * @brief Scrive sul tag fisico un dump selezionato dalla lista di file su SD.
 *
 * Elenca i file .bin presenti in /rfid/dumps/ e lascia scegliere all'utente.
 * Prima di scrivere mostra l'UID contenuto nel dump e chiede conferma.
 * Avvisa se l'UID del tag fisico differisce dall'UID del dump (utile per cloni).
 */
void WriteTessera();

/**
 * @brief Legge l'UID del tag e scrive automaticamente il dump corrispondente.
 *
 * Cerca /rfid/dumps/<UIDHEX>.bin sulla SD: se trovato lo scrive
 * senza ulteriore interazione, altrimenti avvisa l'utente di usare "Read" prima.
 * Caso d'uso: ripristino rapido di una tessera di cui si possiede già il dump.
 */
void AutoWriteTessera();

// ─── Azioni menu Microel ──────────────────────────────────────────────────────

/**
 * @brief Sottomenu dedicato alle tessere Microel.
 *
 * Le tessere Microel usano un KDF (Key Derivation Function) che genera
 * Key A e Key B direttamente dall'UID: non è necessario conoscere le chiavi
 * in anticipo né averle su /rfid/chiavi.txt.
 *
 * Voci del sottomenu:
 *
 *   Info  → Legge il tag, mostra su display:
 *             - UID del tag
 *             - Key A e Key B generate dal KDF (non lette dal tag)
 *             - Credito corrente e credito precedente (blocchi 4 e 5)
 *             - Gestore associato da /rfid/gestori_map.txt
 *
 *   Read  → Legge tutti i settori usando le chiavi KDF, salva il dump su SD
 *             in /rfid/dumps/<UIDHEX>.bin, chiede di associare un gestore.
 *
 *   Write → Scrive sul tag un dump .bin scelto dalla SD. A differenza di
 *             WriteTessera(), scrive ANCHE il blocco 0 (UID/produttore)
 *             tramite mifareWriteBlock0(). Funziona solo su tag magic
 *             (CUID, GEN2, ecc.); su tag originali il blocco 0 viene ignorato
 *             dall'hardware ma il resto della scrittura procede normalmente.
 */
void MicroelTessera();

/**
 * @brief Menu di gestione dei gestori: Aggiungi, Modifica, Elimina.
 *
 * Non richiede NFC. Gestisce:
 *   - /rfid/gestori.txt     → lista nomi gestori (uno per riga)
 *   - /rfid/gestori_map.txt → associazioni UID→gestore (CSV uid,nome)
 *
 * Le modifiche ai nomi vengono propagate automaticamente a tutte
 * le associazioni UID esistenti in gestori_map.txt.
 *
 * Voci del sottomenu:
 *   - Add    → inserisce un nuovo nome da tastiera
 *   - Edit   → rinomina un gestore esistente (aggiorna anche le associazioni)
 *   - Delete → rimuove un gestore e tutte le sue associazioni UID
 */
void GestoriMenuTessera();
