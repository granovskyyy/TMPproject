#include "game_logic.h"  // Dołącz nagłówki z logiką gry
#include "hardware_io.h" // Dołącz nagłówki z funkcjami sprzętowymi
#include <stdio.h>       // Potrzebne dla printf i scanf

int main() {
    inicjalizuj_gre(); // Inicjalizacja gry

    int wybor;
    do {
        wyswietl_menu(); // Wyświetlenie menu z game_logic.h
        if (scanf("%d", &wybor) != 1) {
            printf("Nieprawidłowy wybór. Proszę wprowadzić cyfrę.\n");
            while (getchar() != '\n'); // Wyczyść bufor wejścia
            continue;
        }
        while (getchar() != '\n'); // Wyczyść bufor wejścia po scanf

        switch (wybor) {
            case 1:
                wplac_stawke();
                break;
            case 2:
                zwieksz_mnoznik_zakrecenia();
                break;
            case 3:
                resetuj_mnoznik_zakrecenia();
                break;
            case 4:
                zakrec();
                break;
            case 5:
                automatyczne_krecenie();
                break;
            case 6:
                zakoncz_gre();
                break;
            default:
                printf("Nieznana opcja. Spróbuj ponownie.\n");
                break;
        }
    } while (wybor != 6);

    return 0;
}