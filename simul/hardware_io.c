#include "hardware_io.h" // Dołącz plik nagłówkowy dla tego modułu
#include <stdio.h>       // Potrzebne do printf

// --- Implementacje funkcji symulujących/sterujących sprzętem ---

void wyswietl_cyfry_na_ekranie_siedmiosegmentowym(int cyfra1, int cyfra2, int cyfra3) {
    printf("Wyświetlane cyfry: %d %d %d\n", cyfra1, cyfra2, cyfra3);
    // TUTAJ DODAJ RZECZYWISTY KOD DO OBSLUGI WYSWIETLACZA SIEDMIOSEGMENTOWEGO
}

void wlacz_diody_LED_wygrana(int wartosc_wygranej, int mnoznik_zakrecenia) {
    printf("Włączono diody LED! Wygrana: %d (bazowa) * %d (mnożnik)\n", wartosc_wygranej, mnoznik_zakrecenia);
    // TUTAJ DODAJ RZECZYWISTY KOD DO OBSLUGI DIOD LED DLA WYGRANEJ
    // Np. różne wzory migania, kolory w zależności od wygranej.
}

void wlacz_diody_LED_gra_w_trakcie() {
    printf("Diody LED włączone na czas trwania gry.\n");
    // TUTAJ DODAJ RZECZYWISTY KOD DO UTRZYMANIA DIOD LED WŁĄCZONYCH
}

void wylacz_diody_LED_wszystkie() {
    printf("Wszystkie diody LED wyłączone.\n");
    // TUTAJ DODAJ RZECZYWISTY KOD DO WYLACZENIA WSZYSTKICH DIOD LED
}