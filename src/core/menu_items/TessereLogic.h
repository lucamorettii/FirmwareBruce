#pragma once

#include <Arduino.h>
#include <SD.h>
#include <array>
#include <vector>

// ─── Costanti ────────────────────────────────────────────────────────────────
#define MIFARE_MAX_BLOCKS 256

// ─── Struttura dump ──────────────────────────────────────────────────────────
struct MifareDump {
    uint8_t uid[7];
    uint8_t uidLen;
    uint8_t sak;
    uint16_t atqa;
    String tagType;
    uint8_t numSectors;
    uint8_t data[MIFARE_MAX_BLOCKS][16];
    bool blockRead[MIFARE_MAX_BLOCKS];
    uint8_t keyA[40][6];
    bool keyAFound[40];
    uint8_t keyB[40][6];
    bool keyBFound[40];
};

// Stato globale condiviso
extern MifareDump g_dump;

// ─── API pubblica ─────────────────────────────────────────────────────────────

/** Inizializza il modulo PN532 via I2C. Ritorna false se fallisce. */
bool mifareInit();

/**
 * Aspetta un tag MIFARE sul lettore (timeout 6 s).
 * Popola g_dump con UID, SAK, ATQA e tipo.
 * Ritorna false se nessun tag viene rilevato.
 */
bool waitForMifareTag();

/**
 * Mostra a schermo UID, SAK, ATQA e tipo del tag corrente (g_dump).
 * Chiama mifareInit() e waitForMifareTag() internamente.
 */
void InfoTessera();

/**
 * Legge tutti i settori leggibili del tag, chiede un nome file
 * e salva il dump in /mifare/dump/<nome>.dump sulla SD.
 * Chiama mifareInit() e waitForMifareTag() internamente.
 */
void ReadTessera();

// ─── Helper interni (utili anche da Tessere.cpp se necessario) ───────────────

/** Carica le chiavi da /mifare/chiavi.txt; aggiunge sempre FF FF FF FF FF FF. */
std::vector<std::array<uint8_t, 6>> loadKeysFromSD();

/** Ritorna il tipo di tag dalla stringa SAK. */
String getTagType(uint8_t sak);

/** Numero di settori in base al SAK. */
uint8_t getSectorCount(uint8_t sak);

/** Indice del blocco trailer del settore dato. */
uint8_t trailerBlock(uint8_t sector);

/** Indice del primo blocco del settore dato. */
uint8_t firstBlock(uint8_t sector);

/** Numero di blocchi in un settore. */
uint8_t blocksInSector(uint8_t sector);
