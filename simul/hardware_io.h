#ifndef HARDWARE_IO_H
#define HARDWARE_IO_H

// --- Prototypy funkcji symulujących/sterujących sprzętem ---
void wyswietl_cyfry_na_ekranie_siedmiosegmentowym(int cyfra1, int cyfra2, int cyfra3);
void wlacz_diody_LED_wygrana(int wartosc_wygranej, int mnoznik_zakrecenia);
void wlacz_diody_LED_gra_w_trakcie();
void wylacz_diody_LED_wszystkie();

#endif // HARDWARE_IO_H