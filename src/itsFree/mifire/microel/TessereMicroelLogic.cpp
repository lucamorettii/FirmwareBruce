/* Eseguirlo con g++ -o TessereMicroelLogic.exe TessereMicroelLogic.cpp */

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std;

#define KEY_LENGTH 6
#define UID_LENGTH 4

void calculateSumHex(const uint8_t *uid, size_t uidSize, uint8_t sumHex[]) {
    const uint8_t xorKey[] = {0x01, 0x92, 0xA7, 0x75, 0x2B, 0xF9};
    int sum = 0;

    for (size_t i = 0; i < uidSize; i++) sum += uid[i];

    int sumTwoDigits = sum % 256;

    if (sumTwoDigits % 2 == 1) sumTwoDigits += 2;

    for (size_t i = 0; i < sizeof(xorKey); i++) sumHex[i] = sumTwoDigits ^ xorKey[i];
}

void generateKeyA(const uint8_t *uid, uint8_t uidSize, uint8_t keyA[]) {
    uint8_t sumHex[6];
    calculateSumHex(uid, uidSize, sumHex);
    uint8_t firstCharacter = (sumHex[0] >> 4) & 0xF;

    if (firstCharacter == 0x2 || firstCharacter == 0x3 || firstCharacter == 0xA || firstCharacter == 0xB) {
        for (size_t i = 0; i < KEY_LENGTH; i++) keyA[i] = 0x40 ^ sumHex[i];
    } else if (firstCharacter == 0x6 || firstCharacter == 0x7 || firstCharacter == 0xE ||
               firstCharacter == 0xF) {
        for (size_t i = 0; i < KEY_LENGTH; i++) keyA[i] = 0xC0 ^ sumHex[i];
    } else {
        for (size_t i = 0; i < KEY_LENGTH; i++) keyA[i] = sumHex[i];
    }
}

void generateKeyB(const uint8_t keyA[], uint8_t keyB[]) {
    for (size_t i = 0; i < KEY_LENGTH; i++) keyB[i] = 0xFF ^ keyA[i];
}

bool parseUID(const string &input, uint8_t uid[]) {
    string clean;
    for (char c : input)
        if (c != ' ' && c != ':' && c != '-') clean += c;

    if (clean.size() != UID_LENGTH * 2) {
        cerr << "Errore: l'UID deve essere di 4 byte (8 caratteri hex)." << endl;
        return false;
    }

    for (size_t i = 0; i < UID_LENGTH; i++) {
        string byteStr = clean.substr(i * 2, 2);
        try {
            uid[i] = static_cast<uint8_t>(stoul(byteStr, nullptr, 16));
        } catch (...) {
            cerr << "Errore: caratteri non validi nell'UID." << endl;
            return false;
        }
    }
    return true;
}

void printKey(const char *label, const uint8_t key[]) {
    cout << label;
    for (size_t i = 0; i < KEY_LENGTH; i++)
        cout << uppercase << hex << setw(2) << setfill('0') << (int)key[i];
    cout << endl;
}

int main() {
    string input;
    cout << "Inserisci l'UID (4 byte hex, es: A1B2C3D4): ";
    cin >> input;

    uint8_t uid[UID_LENGTH];
    if (!parseUID(input, uid)) return 1;

    cout << "\nUID: ";
    for (size_t i = 0; i < UID_LENGTH; i++)
        cout << uppercase << hex << setw(2) << setfill('0') << (int)uid[i];
    cout << endl;

    uint8_t keyA[KEY_LENGTH];
    uint8_t keyB[KEY_LENGTH];

    generateKeyA(uid, UID_LENGTH, keyA);
    generateKeyB(keyA, keyB);

    printKey("Key A: ", keyA);
    printKey("Key B: ", keyB);

    cout << "\nPremi INVIO per uscire...";
    cin.ignore();
    cin.get();

    return 0;
}
