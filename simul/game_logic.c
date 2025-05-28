#include "game_logic.h" // Dołącz plik nagłówkowy dla tego modułu
#include "hardware_io.h" // Dołącz plik nagłówkowy dla funkcji sprzętowych
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// --- Definicje zmiennych globalnych stanu gry ---
long long konto_gracza = 0;
int wartosc_zakrecenia_mnoznik = DOMYSLNY_MNOZNIK_ZAKRECENIA;
bool gra_w_trakcie = false;

// --- Implementacje funkcji logiki gry ---

void inicjalizuj_gre() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych
    konto_gracza = 0;
    wartosc_zakrecenia_mnoznik = DOMYSLNY_MNOZNIK_ZAKRECENIA;
    gra_w_trakcie = false;
    printf("Witamy w grze slotowej Black Jack! Twoje konto zostało wyzerowane.\n");
    wylacz_diody_LED_wszystkie(); // Upewnij się, że diody są wyłączone na początku
}

void wplac_stawke() {
    long long stawka;
    printf("Wpłać dowolną stawkę: ");
    if (scanf("%lld", &stawka) == 1 && stawka > 0) {
        konto_gracza += stawka;
        printf("Pomyślnie wpłacono %lld. Twoje konto: %lld\n", stawka, konto_gracza);
        gra_w_trakcie = true;
        wlacz_diody_LED_gra_w_trakcie();
    } else {
        printf("Nieprawidłowa stawka. Musisz wpłacić dodatnią kwotę.\n");
    }
    while (getchar() != '\n'); // Wyczyść bufor wejścia
}

void zwieksz_mnoznik_zakrecenia() {
    wartosc_zakrecenia_mnoznik++;
    printf("Mnożnik zakręcenia zwiększony do %d.\n", wartosc_zakrecenia_mnoznik);
}

void resetuj_mnoznik_zakrecenia() {
    wartosc_zakrecenia_mnoznik = DOMYSLNY_MNOZNIK_ZAKRECENIA;
    printf("Mnożnik zakręcenia zresetowany do %d.\n", wartosc_zakrecenia_mnoznik);
}

void zakrec() {
    if (konto_gracza <= 0) {
        printf("Brak środków na koncie. Wpłać stawkę, aby grać.\n");
        return;
    }

    printf("--- Kręcenie! ---\n");
    int cyfra1 = rand() % (MAX_CYFRA + 1);
    int cyfra2 = rand() % (MAX_CYFRA + 1);
    int cyfra3 = rand() % (MAX_CYFRA + 1);

    wyswietl_cyfry_na_ekranie_siedmiosegmentowym(cyfra1, cyfra2, cyfra3);

    if (cyfra1 == cyfra2 && cyfra2 == cyfra3) {
        long long wygrana = (long long)WARTOSC_WYGRANEJ_BAZOWA * wartosc_zakrecenia_mnoznik;
        konto_gracza += wygrana;
        printf("!!! WYGRANA !!! Wszystkie cyfry są takie same! Wygrana: %lld\n", wygrana);
        wlacz_diody_LED_wygrana(WARTOSC_WYGRANEJ_BAZOWA, wartosc_zakrecenia_mnoznik);
    } else {
        printf("Spróbuj ponownie! Brak wygranej.\n");
    }
    printf("Twoje aktualne konto: %lld\n", konto_gracza);
}

void automatyczne_krecenie() {
    printf("Rozpoczęto automatyczne kręcenie (do implementacji).\n");
    // Tutaj należy zaimplementować logikę automatycznego kręcenia
}

void zakoncz_gre() {
    printf("Dziękujemy za grę! Twoje konto zostało wyzerowane (w prawdziwym kasynie dostałbyś pieniądze).\n");
    konto_gracza = 0;
    gra_w_trakcie = false;
    wylacz_diody_LED_wszystkie();
}

void wyswietl_menu() {
    printf("\n--- MENU GRY --- \n");
    printf("1. Wpłać stawkę\n");
    printf("2. Zwiększ mnożnik zakręcenia (aktualnie: %d)\n", wartosc_zakrecenia_mnoznik);
    printf("3. Resetuj mnożnik zakręcenia\n");
    printf("4. Zakręć bębnami\n");
    printf("5. Automatyczne kręcenie (do implementacji)\n");
    printf("6. Zakończ grę\n");
    printf("Twoje konto: %lld\n", konto_gracza);
    printf("Wybierz opcję: ");
}