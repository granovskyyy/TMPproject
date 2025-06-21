#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h> // Do funkcji rand() i srand()
#include <time.h>   // Do inicjalizacji generatora liczb losowych (może być problematyczne na AVR bez RTC)

// --- Definicje pinów i portów (ZAKTUALIZOWANE DLA ATMEGA328PB) ---

// Wyświetlacz 1 (losowane cyfry): PORTD (PD0-PD6)
#define SEVEN_SEG_PORT_DIGIT_1 PORTD
#define SEVEN_SEG_DDR_DIGIT_1 DDRD

// Wyświetlacz 2 (konto gracza/mnożnik): PORTC (PC0-PC5) i PB0 (dla segmentu G)
#define SEVEN_SEG_PORT_DIGIT_2_PC PORTC
#define SEVEN_SEG_DDR_DIGIT_2_PC DDRC
#define SEVEN_SEG_PIN_DIGIT_2_G PB0 // Pin dla segmentu G drugiego wyświetlacza
#define SEVEN_SEG_PORT_DIGIT_2_PB PORTB // Port dla segmentu G drugiego wyświetlacza
#define SEVEN_SEG_DDR_DIGIT_2_PB DDRB

// Piny dla segmentów wyświetlaczy (dla wspólnej katody)
// a, b, c, d, e, f, g (odpowiadają bitom 0-6 w masce)
const uint8_t segment_map_common_cathode[] = {
    // gfedcba
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};

// Klawiatura 4x4
// Rzędy na PORTB (PB1-PB4)
#define KEYPAD_ROWS_DDR DDRB
#define KEYPAD_ROWS_PORT PORTB
#define KEYPAD_ROWS_PIN PINB
const uint8_t keypad_row_pins[] = {PB1, PB2, PB3, PB4}; // Piny dla wierszy (PB1, PB2, PB3, PB4)

// Kolumny na PORTE (PE0-PE3)
#define KEYPAD_COLS_DDR DDRE
#define KEYPAD_COLS_PORT PORTE
#define KEYPAD_COLS_PIN PINE
const uint8_t keypad_col_pins[] = {PE0, PE1, PE2, PE3}; // Piny dla kolumn (PE0, PE1, PE2, PE3)

// Diody LED
#define LED_PORT_STATUS PORTD // PD7 dla LED_WIN (dla sygnalizacji wygranej/przegranej)
#define LED_DDR_STATUS DDRD
#define LED_WIN PD7 // Dioda wygranej (może migać dla przegranej)

#define LED_PORT_AUTO PORTB // PB5 dla LED_AUTO
#define LED_DDR_AUTO DDRB
#define LED_AUTO PB5 // Dioda auto-kręcenia

// --- Zmienne globalne ---
volatile uint8_t game_state = 0; // 0: oczekiwanie, 1: gra_start, 2: quick_game, 3: edytuj_mnoznik, 4: koniec_gry
volatile uint32_t player_account = 1000; // Początkowe konto gracza
volatile uint32_t current_bet = 0;      // Aktualna stawka
volatile uint8_t multiplier = 1;       // Mnożnik
volatile uint8_t rolled_digit = 0;     // Wylosowana cyfra
volatile uint8_t auto_roll_enabled = 0; // 0: OFF, 1: ON
volatile uint8_t num_digits_to_roll = 3; // Domyślna liczba cyfr do losowania (3 lub 4)

// Flaga do obsługi przerwania od timera (dla skanowania klawiatury i auto-kręcenia)
volatile uint8_t timer_flag = 0;

// --- Prototypy funkcji ---
void initialize_ports(void);
void initialize_timer0(void);
void display_digit(uint8_t digit_value, uint8_t display_num);
uint8_t read_keypad(void);
void turn_on_led(uint8_t led_pin);
void turn_off_led(uint8_t led_pin);
void toggle_led(uint8_t led_pin); // Nowa funkcja do migania

void game_start(void);
void quick_game(void);
void edit_multiplier(void);
void roll_digit(void);
void make_deposit(uint16_t amount);
void make_withdrawal(uint16_t amount);
void game_end(void);
void clear_bet(void);
void clear_multiplier(void);
void set_num_digits_to_roll(void);

// --- Implementacja funkcji ---

/**
 * @brief Inicjalizuje porty I/O dla wyświetlaczy, klawiatury i diod LED.
 */
void initialize_ports(void) {
    // Wyświetlacz 1 (PORTD, PD0-PD6 jako wyjścia)
    SEVEN_SEG_DDR_DIGIT_1 |= 0x7F; // Piny PD0-PD6 jako wyjścia

    // Wyświetlacz 2 (PORTC PC0-PC5 jako wyjścia, PB0 jako wyjście)
    SEVEN_SEG_DDR_DIGIT_2_PC |= 0x3F; // Piny PC0-PC5 jako wyjścia
    SEVEN_SEG_DDR_DIGIT_2_PB |= (1 << SEVEN_SEG_PIN_DIGIT_2_G); // PB0 jako wyjście

    // Klawiatura - rzędy (PORTB PB1-PB4 jako wyjścia, domyślnie wysoki stan)
    KEYPAD_ROWS_DDR |= 0x1E; // Piny PB1-PB4 jako wyjścia (0b00011110)
    KEYPAD_ROWS_PORT |= 0x1E; // Ustaw domyślny stan wysoki dla PB1-PB4

    // Klawiatura - kolumny (PORTE PE0-PE3 jako wejścia z Pull-up)
    KEYPAD_COLS_DDR &= ~0x0F; // Piny PE0-PE3 jako wejścia
    KEYPAD_COLS_PORT |= 0x0F; // Włącz wewnętrzne rezystory Pull-up dla PE0-PE3

    // Diody LED
    LED_DDR_STATUS |= (1 << LED_WIN); // PD7 jako wyjście
    LED_DDR_AUTO |= (1 << LED_AUTO);   // PB5 jako wyjście
    turn_off_led(LED_WIN);
    turn_off_led(LED_AUTO);
}

/**
 * @brief Inicjalizuje Timer0 do generowania przerwań co określony czas.
 * Wykorzystywany do skanowania klawiatury i ewentualnie auto-kręcenia.
 * Przerwanie co ok. 10ms (dla preskalera 1024, zegar 16MHz).
 */
void initialize_timer0(void) {
    TCCR0A |= (1 << WGM01); // Tryb CTC
    OCR0A = 155; // Ustawienie wartości do porównania dla przerwania co ~10ms (16MHz, preskaler 1024)
    TCCR0B |= (1 << CS02) | (1 << CS00); // Preskaler 1024
    TIMSK0 |= (1 << OCIE0A); // Włączenie przerwania Compare Match A
    sei(); // Włącz globalne przerwania
}

/**
 * @brief Wyświetla pojedynczą cyfrę na wybranym wyświetlaczu 7-segmentowym.
 * @param digit_value Cyfra do wyświetlenia (0-9).
 * @param display_num Numer wyświetlacza (1 lub 2).
 */
void display_digit(uint8_t digit_value, uint8_t display_num) {
    if (digit_value > 9) digit_value = 0; // Zabezpieczenie przed nieprawidlowa cyfra

    if (display_num == 1) { // Wyświetlacz 1 (PORTD, a-g na PD0-PD6)
        SEVEN_SEG_PORT_DIGIT_1 = segment_map_common_cathode[digit_value]; // Ustaw cały port
    } else if (display_num == 2) { // Wyświetlacz 2 (PORTC a-f na PC0-PC5, G na PB0)
        uint8_t segment_data = segment_map_common_cathode[digit_value];

        // Ustaw piny PC0-PC5 dla segmentów A-F (maska 0x3F = 0b00111111)
        // Zachowaj pozostałe bity portu C
        SEVEN_SEG_PORT_DIGIT_2_PC = (SEVEN_SEG_PORT_DIGIT_2_PC & ~0x3F) | (segment_data & 0x3F);

        // Ustaw pin PB0 dla segmentu G (bit 6 z segment_data)
        if (segment_data & (1 << 6)) { // Jeśli segment G jest aktywny (bit 6)
            SEVEN_SEG_PORT_DIGIT_2_PB |= (1 << SEVEN_SEG_PIN_DIGIT_2_G);
        } else {
            SEVEN_SEG_PORT_DIGIT_2_PB &= ~(1 << SEVEN_SEG_PIN_DIGIT_2_G);
        }
    }
}

/**
 * @brief Odczytuje stan klawiatury 4x4.
 * @return Kod przycisku (1-16) lub 0, jeśli żaden przycisk nie został naciśnięty.
 */
uint8_t read_keypad(void) {
    // Używamy PB1, PB2, PB3, PB4 dla rzędów
    // Używamy PE0, PE1, PE2, PE3 dla kolumn
    uint8_t key_pressed = 0;

    for (uint8_t row_idx = 0; row_idx < 4; row_idx++) {
        // Ustawienie aktywnego wiersza (stan niski)
        // Ustawiamy tylko jeden pin rzędu na LOW, reszta pinów rzędów na HIGH
        KEYPAD_ROWS_PORT = (0x1E & ~(1 << keypad_row_pins[row_idx])) | (KEYPAD_ROWS_PORT & ~0x1E);
        _delay_us(10); // Krótkie opóźnienie na stabilizację

        // Odczytanie kolumn
        uint8_t col_data_raw = KEYPAD_COLS_PIN & 0x0F; // Odczytujemy tylko PE0-PE3

        if (~col_data_raw & 0x0F) { // Jeśli którykolwiek z bitów kolumn jest LOW (naciśnięty)
            for (uint8_t col_idx = 0; col_idx < 4; col_idx++) {
                if (!((col_data_raw >> col_idx) & 1)) { // Jeśli pin kolumny jest LOW (naciśnięty)
                    key_pressed = (row_idx * 4) + col_idx + 1; // Obliczanie kodu przycisku
                    // Debouncing
                    _delay_ms(50);
                    // Poczekaj, aż przycisk zostanie zwolniony
                    while (!((KEYPAD_COLS_PIN >> col_idx) & 1)) { // Czekaj, aż pin kolumny będzie HIGH
                        _delay_ms(10);
                    }
                    // Przywróć porty rzędów do stanu nieaktywnego (wszystkie PB1-PB4 na HIGH)
                    KEYPAD_ROWS_PORT |= 0x1E;
                    return key_pressed;
                }
            }
        }
    }
    // Przywróć porty rzędów do stanu nieaktywnego (wszystkie PB1-PB4 na HIGH)
    KEYPAD_ROWS_PORT |= 0x1E;
    return 0; // Brak naciśniętego przycisku
}

/**
 * @brief Zapala wskazaną diodę LED.
 * @param led_pin Pin diody LED (używaj LED_WIN lub LED_AUTO).
 */
void turn_on_led(uint8_t led_pin) {
    if (led_pin == LED_WIN) {
        LED_PORT_STATUS |= (1 << LED_WIN);
    } else if (led_pin == LED_AUTO) {
        LED_PORT_AUTO |= (1 << LED_AUTO);
    }
}

/**
 * @brief Gasi wskazaną diodę LED.
 * @param led_pin Pin diody LED (używaj LED_WIN lub LED_AUTO).
 */
void turn_off_led(uint8_t led_pin) {
    if (led_pin == LED_WIN) {
        LED_PORT_STATUS &= ~(1 << LED_WIN);
    } else if (led_pin == LED_AUTO) {
        LED_PORT_AUTO &= ~(1 << LED_AUTO);
    }
}

/**
 * @brief Przełącza stan wskazanej diody LED.
 * @param led_pin Pin diody LED (używaj LED_WIN lub LED_AUTO).
 */
void toggle_led(uint8_t led_pin) {
    if (led_pin == LED_WIN) {
        LED_PORT_STATUS ^= (1 << LED_WIN);
    } else if (led_pin == LED_AUTO) {
        LED_PORT_AUTO ^= (1 << LED_AUTO);
    }
}

// --- Funkcje logiki gry (bez zmian, używają ogólnych funkcji sterowania sprzętem) ---
// game_start(), quick_game(), edit_multiplier(), roll_digit(), make_deposit(), make_withdrawal(),
// game_end(), clear_bet(), clear_multiplier(), set_num_digits_to_roll()
// Te funkcje pozostają takie same jak w poprzedniej wersji, ponieważ operują na zmiennych
// globalnych i wywołują funkcje obsługujące sprzęt, które zostały dostosowane powyżej.

void game_start(void) {
    game_state = 1;
    player_account = 1000;
    current_bet = 0;
    multiplier = 1;
    rolled_digit = 0;
    auto_roll_enabled = 0;
    num_digits_to_roll = 3;
    turn_off_led(LED_WIN);
    turn_off_led(LED_AUTO);
    display_digit(multiplier, 2);
    display_digit(0, 1); // Wyzeruj wylosowaną cyfrę
}

void quick_game(void) {
    game_state = 2;
    current_bet = 10;
    multiplier = 1;
    num_digits_to_roll = 3;
    turn_off_led(LED_WIN);
    turn_off_led(LED_AUTO);
    display_digit(multiplier, 2);
    display_digit(0, 1); // Wyzeruj wylosowaną cyfrę
}

void edit_multiplier(void) {
    game_state = 3;
    display_digit(multiplier, 2);
}

void roll_digit(void) {
    rolled_digit = rand() % 10;
    display_digit(rolled_digit, 1);
}

void make_deposit(uint16_t amount) {
    player_account += amount;
    display_digit(player_account % 10, 2);
    _delay_ms(500);
    display_digit(multiplier, 2);
}

void make_withdrawal(uint16_t amount) {
    if (player_account >= amount) {
        player_account -= amount;
    } else {
        player_account = 0;
    }
    display_digit(player_account % 10, 2);
    _delay_ms(500);
    display_digit(multiplier, 2);
}

void game_end(void) {
    game_state = 0;
    current_bet = 0;
    multiplier = 1;
    auto_roll_enabled = 0;
    turn_off_led(LED_WIN);
    turn_off_led(LED_AUTO);
    display_digit(0, 1);
    display_digit(0, 2);
}

void clear_bet(void) {
    current_bet = 0;
}

void clear_multiplier(void) {
    multiplier = 1;
    display_digit(multiplier, 2);
}

void set_num_digits_to_roll(void) {
    num_digits_to_roll = (num_digits_to_roll == 3) ? 4 : 3;
    display_digit(num_digits_to_roll, 1);
    _delay_ms(200);
    display_digit(rolled_digit, 1);
}


// --- Obsługa przerwań ---
ISR(TIMER0_COMPA_vect) {
    timer_flag = 1;
}

// --- Główna pętla programu ---

int main(void) {
    initialize_ports();
    initialize_timer0();
    srand(1234); // Na AVR bez RTC, lepiej użyć stałej wartości dla powtarzalnych testów lub bardziej złożonej inicjalizacji

    display_digit(0, 1);
    display_digit(0, 2);

    while (1) {
        if (timer_flag) {
            timer_flag = 0;

            uint8_t pressed_key = read_keypad();

            if (auto_roll_enabled && (game_state == 1 || game_state == 2)) {
                _delay_ms(500);
                roll_digit();
                if (rolled_digit == 7) {
                    turn_on_led(LED_WIN);
                    if (current_bet > 0) make_deposit(current_bet * multiplier);
                    _delay_ms(1000);
                    turn_off_led(LED_WIN);
                } else if (rolled_digit == 0) {
                    // Dla przegranej, możemy migać diodą wygranej, aby zasygnalizować inny stan
                    for (int i = 0; i < 3; i++) {
                        toggle_led(LED_WIN);
                        _delay_ms(200);
                    }
                    turn_off_led(LED_WIN); // Upewnij się, że jest wyłączona
                    if (current_bet > 0) make_withdrawal(current_bet);
                }
            }

            switch (pressed_key) {
                case 1: game_start(); break;
                case 2: edit_multiplier(); break;
                case 3: game_end(); break;
                case 4:
                    if (game_state == 1 || game_state == 2) {
                        auto_roll_enabled = !auto_roll_enabled;
                        if (auto_roll_enabled) { turn_on_led(LED_AUTO); }
                        else { turn_off_led(LED_AUTO); }
                    }
                    break;
                case 13: if (game_state == 1) { current_bet += 1; } break;
                case 14: if (game_state == 1) { current_bet += 10; } break;
                case 15: if (game_state == 1) { current_bet += 100; } break;
                case 16: if (game_state == 1) { current_bet += 1000; } break;
                case 9:
                    if (game_state == 3) {
                        multiplier = (multiplier % 9) + 1;
                        display_digit(multiplier, 2);
                    } else if (game_state == 1) {
                        if (current_bet > 0) {
                            make_withdrawal(current_bet);
                            roll_digit();
                            if (rolled_digit == 7) {
                                turn_on_led(LED_WIN);
                                make_deposit(current_bet * multiplier);
                                _delay_ms(1000);
                                turn_off_led(LED_WIN);
                            } else if (rolled_digit == 0) {
                                for (int i = 0; i < 3; i++) { toggle_led(LED_WIN); _delay_ms(200); }
                                turn_off_led(LED_WIN);
                            }
                        }
                    }
                    break;
                case 10:
                    if (game_state == 3) {
                        multiplier = (multiplier % 9) + 1;
                        display_digit(multiplier, 2);
                    } else if (game_state == 0) {
                        quick_game();
                    }
                    break;
                case 11: clear_bet(); break;
                case 12: clear_multiplier(); break;
                case 5: if (game_state == 1) { set_num_digits_to_roll(); } break;
                case 6:
                    if (game_state == 1 || game_state == 2) {
                        display_digit(player_account % 10, 2);
                        _delay_ms(1500);
                        display_digit(multiplier, 2);
                    }
                    break;
                case 7: if (game_state == 3) { game_state = 1; display_digit(multiplier, 2); } break;
                case 8:
                    if (game_state == 1 || game_state == 2) {
                        current_bet = 0;
                        auto_roll_enabled = 0;
                        turn_off_led(LED_AUTO);
                        display_digit(rolled_digit, 1);
                        display_digit(multiplier, 2);
                    }
                    break;
            }
        }
    }
}