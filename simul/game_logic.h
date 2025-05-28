#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdbool.h> // Potrzebne dla typu bool

// --- Stałe do konfiguracji gry ---
#define DOMYSLNY_MNOZNIK_ZAKRECENIA 1
#define WARTOSC_WYGRANEJ_BAZOWA 100 // Bazowa wartość wygranej
#define MAX_CYFRA 9                 // Maksymalna cyfra do wylosowania (0-9)
#define LICZBA_CYFR 3               // Liczba cyfr wyświetlanych na ekranie

// --- Zmienne globalne stanu gry (deklaracje) ---
// Słowo kluczowe 'extern' informuje kompilator, że zmienna jest zdefiniowana gdzie indziej (w game_logic.c)
extern long long konto_gracza;
extern int wartosc_zakrecenia_mnoznik;
extern bool gra_w_trakcie;

// --- Prototypy funkcji logiki gry ---
void inicjalizuj_gre();
void wplac_stawke();
void zwieksz_mnoznik_zakrecenia();
void resetuj_mnoznik_zakrecenia();
void zakrec();
void automatyczne_krecenie(); // Do dalszej implementacji
void zakoncz_gre();
void wyswietl_menu(); // Możesz przenieść do main.c jeśli chcesz, ale dla spójności zostawię tutaj

#endif // GAME_LOGIC_H