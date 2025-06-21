#ifndef F_CPU
#define F_CPU 16000000UL // KLUCZOWE: Upewnij się, że Twój ATmega328PB pracuje na 16MHz!
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h> // Do funkcji rand() i srand()
#include <stdbool.h> // <--- DODAJ TĘ LINIĘ!

// --- Definicje pinów i portów ---

// Wyświetlacz 7-segmentowy (Common Anode)
#define SEGMENT_PORT PORTD
#define SEGMENT_DDR DDRD
// Segments a-g, dp on PD0-PD7

#define DIGIT_SELECT_PORT PORTC
#define DIGIT_SELECT_DDR DDRC
#define DIGIT_1_ENABLE_PIN PC0
#define DIGIT_2_ENABLE_PIN PC1
#define DIGIT_3_ENABLE_PIN PC2
#define DIGIT_4_ENABLE_PIN PC3
#define DIGIT_ENABLE_MASK ((1 << DIGIT_1_ENABLE_PIN) | (1 << DIGIT_2_ENABLE_PIN) | \
                           (1 << DIGIT_3_ENABLE_PIN) | (1 << DIGIT_4_ENABLE_PIN))

// Mapowanie segmentów dla Common Anode (LOW aktywuje segment)
const uint8_t segment_map_common_anode[] = {
    0b01000000, // 0 (0x40)
    0b01111001, // 1 (0x79)
    0b00100100, // 2 (0x24)
    0b00001000, // 3 (0x08)
    0b00011001, // 4 (0x19)
    0b00010010, // 5 (0x12)
    0b00000010, // 6 (0x02)
    0b01111000, // 7 (0x78)
    0b00000000, // 8 (0x00)
    0b00001000  // 9 (0x08)
};
#define SEG_G_MASK (1 << 6) // Segment G na PD6


// Klawiatura 4x4
#define KEYPAD_ROWS_DDR DDRB
#define KEYPAD_ROWS_PORT PORTB
#define KEYPAD_ROWS_PIN PINB
const uint8_t keypad_row_pins[] = {PB0, PB1, PB2, PB3}; // Rzędy na PB0-PB3

#define KEYPAD_COLS_DDR DDRE
#define KEYPAD_COLS_PORT PORTE
#define KEYPAD_COLS_PIN PINE
const uint8_t keypad_col_pins[] = {PE0, PE1, PE2, PE3}; // Kolumny na PE0-PE3

// Diody LED
#define LED_PORT_PC PORTC
#define LED_DDR_PC DDRC
#define LED_WIN PC4 // PC4
#define LED_LOSE PC5 // PC5

#define LED_PORT_PB PORTB
#define LED_DDR_PB DDRB
#define LED_AUTO PB4 // PB4


// --- Zmienne globalne ---
volatile uint8_t game_state = 0;          // 0: Initial, 1: Active, 2: Quick, 3: Edit Multiplier
volatile uint32_t player_account = 1000;  // Konto gracza
volatile uint32_t current_bet = 0;        // Bieżąca stawka
volatile uint8_t multiplier = 1;          // Mnożnik wygranej
volatile uint8_t rolled_digit = 0;        // Ostatnio wylosowana cyfra
volatile uint8_t auto_roll_enabled = 0;   // Tryb automatyczny
volatile uint8_t num_digits_to_roll = 3;  // Liczba cyfr do wylosowania (oryginalnie była taka zmienna)

volatile uint8_t display_mode = 0;        // 0: Finansowy (konto/mnożnik), 1: Gra (wylosowana cyfra)
volatile uint8_t animation_active = 0;    // Flaga aktywności animacji (dla S8)

// Wartości do wyświetlenia na 4-cyfrowym wyświetlaczu
volatile uint8_t display_d1 = 0; // Tysiące (zawsze 0 w obecnym kodzie, ale trzymamy dla 4 cyfr)
volatile uint8_t display_d2 = 0; // Setki (dla konta) / zawsze 0 dla gry
volatile uint8_t display_d3 = 0; // Dziesiątki (dla konta) / zawsze 0 dla gry
volatile uint8_t display_d4 = 0; // Jednostki (dla konta lub gry)

// Zmienne do multipleksowania i logiki gry
volatile uint8_t multiplex_digit_index = 0;
volatile uint8_t timer_logic_counter = 0; // Licznik do wywoływania głównej logiki co ok. 50ms
volatile bool timer_logic_flag = false;   // Flaga do sygnalizacji głównej pętli

volatile uint16_t animation_frame_counter = 0; // Licznik klatek dla animacji


// --- Prototypy funkcji ---
void initialize_ports(void);
void initialize_timer0(void);
void set_segment_data(uint8_t digit_value);
void select_digit_enable(uint8_t digit_pin);
void display_multiplex_task(void); // Nowa nazwa, wywoływana przez przerwanie
void run_animation(void);
uint8_t read_keypad(void); // Zmodyfikowana na 4x4
void turn_on_led(uint8_t led_pin);
void turn_off_led(uint8_t led_pin);
void toggle_led(uint8_t led_pin);

void reset_game_state(void); // Pełny reset stanu
void update_display_values(void); // Aktualizuje wartości w display_d1-d4
void game_start(void);
void quick_game(void);
void edit_multiplier(void);
void roll_digit(void);
void make_deposit(uint32_t amount);
void make_withdrawal(uint32_t amount);
void game_end(void);
void clear_bet(void);
void clear_multiplier(void);
void set_num_digits_to_roll(void);
void switch_display_mode(void);


// --- Implementacja funkcji ---

void initialize_ports(void) {
    // Ustaw wszystkie porty na bezpieczne stany początkowe
    // PORTD dla segmentów (wyjście, wszystkie wyłączone)
    SEGMENT_DDR = 0xFF;
    SEGMENT_PORT = 0xFF; // Wszystkie segmenty OFF (HIGH dla Common Anode)

    // PORTC dla wyboru cyfr (wyjścia, wszystkie wyłączone) i diod LED PC4, PC5
    DIGIT_SELECT_DDR |= DIGIT_ENABLE_MASK | (1 << LED_WIN) | (1 << LED_LOSE);
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK | (1 << LED_WIN) | (1 << LED_LOSE); // Wszystkie cyfry i diody OFF (HIGH)

    // PORTB dla rzędów klawiatury (wyjścia) i LED_AUTO (PB4)
    KEYPAD_ROWS_DDR |= 0x0F; // PB0-PB3 jako wyjścia
    KEYPAD_ROWS_PORT |= 0x0F; // Wszystkie rzędy HIGH (nieaktywne)

    LED_DDR_PB |= (1 << LED_AUTO); // PB4 jako wyjście
    LED_PORT_PB |= (1 << LED_AUTO); // LED_AUTO OFF (HIGH)

    // PORTE dla kolumn klawiatury (wejścia z pull-up)
    KEYPAD_COLS_DDR &= ~0x0F; // PE0-PE3 jako wejścia
    KEYPAD_COLS_PORT |= 0x0F; // Aktywuj pull-upy na PE0-PE3
}

void initialize_timer0(void) {
    // Tryb CTC (Clear Timer on Compare Match)
    TCCR0A |= (1 << WGM01);

    // OCR0A dla przerwania co ~2ms (16MHz / 256 preskaler / 125 = 500 Hz -> 2ms)
    OCR0A = 124;

    // Włączenie preskalera (256) i start timera
    TCCR0B |= (1 << CS02);

    // Włączenie przerwania Compare Match A
    TIMSK0 |= (1 << OCIE0A);

    sei(); // Włącz globalne przerwania
}

void set_segment_data(uint8_t digit_value) {
    SEGMENT_PORT = segment_map_common_anode[digit_value];
}

void select_digit_enable(uint8_t digit_pin_mask) {
    // Wyłącz wszystkie cyfry najpierw (ustaw na HIGH)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    // Aktywuj tylko wybraną cyfrę (ustaw na LOW)
    DIGIT_SELECT_PORT &= ~digit_pin_mask;
}

// Ta funkcja jest wywoływana przez Timer0 ISR
void display_multiplex_task(void) {
    uint8_t digit_value_to_display = 0;
    uint8_t digit_enable_mask = 0;

    // Krok 1: Wyłącz wszystkie segmenty, aby uniknąć ghostingu (HIGH dla CA)
    SEGMENT_PORT = 0xFF; 
    // Krok 2: Wyłącz wszystkie cyfry (HIGH dla CA)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    // Krok 3: Wybierz, która cyfra będzie następna i jakie wartości ma wyświetlić
    switch (multiplex_digit_index) {
        case 0: // Cyfra 1 (MSD)
            digit_value_to_display = display_d1; 
            digit_enable_mask = (1 << DIGIT_1_ENABLE_PIN);
            break;
        case 1: // Cyfra 2
            digit_value_to_display = display_d2;
            digit_enable_mask = (1 << DIGIT_2_ENABLE_PIN);
            break;
        case 2: // Cyfra 3
            digit_value_to_display = display_d3;
            digit_enable_mask = (1 << DIGIT_3_ENABLE_PIN);
            break;
        case 3: // Cyfra 4 (LSD)
            digit_value_to_display = display_d4;
            digit_enable_mask = (1 << DIGIT_4_ENABLE_PIN);
            break;
    }

    // Krok 4: Ustaw segmenty dla aktualnej cyfry
    set_segment_data(digit_value_to_display);

    // Krok 5: Aktywuj wybraną cyfrę (LOW dla CA)
    select_digit_enable(digit_enable_mask); 

    multiplex_digit_index++;
    if (multiplex_digit_index >= 4) { // Przełącz na następną cyfrę (mamy 4 cyfry)
        multiplex_digit_index = 0;
    }
}

void run_animation(void) {
    animation_frame_counter++;
    
    // Zawsze wyłączaj segmenty i cyfry na początku animacji,
    // aby uniknąć wpływu na display_update_task.
    SEGMENT_PORT = 0xFF; 
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    if (animation_frame_counter % 2 == 0) { // Miganie co 2 wywołania przerwania (co 4ms)
        SEGMENT_PORT &= ~SEG_G_MASK; // Włącz segment G (LOW dla CA)
    } else {
        SEGMENT_PORT |= SEG_G_MASK; // Wyłącz segment G (HIGH dla CA)
    }
    // Aktywuj WSZYSTKIE cyfry jednocześnie dla animacji (ustaw na LOW dla CA)
    DIGIT_SELECT_PORT &= ~DIGIT_ENABLE_MASK; 
}


// Klawiatura 4x4 - Zwraca numer klawisza (1-16) lub 0
uint8_t read_keypad(void) {
    static uint8_t last_pressed_key = 0;
    static uint16_t debounce_timer = 0;
    const uint16_t DEBOUNCE_DELAY_MS = 50; // Opóźnienie na debouncing

    // Upewnij się, że wszystkie rzędy są na początku HIGH (nieaktywne)
    KEYPAD_ROWS_PORT |= 0x0F;

    for (uint8_t row_idx = 0; row_idx < 4; row_idx++) {
        // Ustaw wybrany rząd na LOW, aby go aktywować
        KEYPAD_ROWS_PORT &= ~(1 << keypad_row_pins[row_idx]);
        _delay_us(10); // Krótkie opóźnienie dla stabilizacji

        // Odczytaj stan kolumn
        uint8_t col_data_raw = KEYPAD_COLS_PIN & 0x0F; // Odczytujemy tylko PE0-PE3

        // Jeśli którykolwiek z pinów kolumn jest LOW (przycisk naciśnięty)
        if ((col_data_raw & 0x0F) != 0x0F) { 
            for (uint8_t col_idx = 0; col_idx < 4; col_idx++) {
                if (!((col_data_raw >> col_idx) & 1)) { // Jeśli pin kolumny jest LOW
                    uint8_t pressed_key = (row_idx * 4) + col_idx + 1;

                    if (pressed_key != last_pressed_key) {
                        // Nowe naciśnięcie, czekaj na debouncing
                        _delay_ms(DEBOUNCE_DELAY_MS);
                        if (!((KEYPAD_COLS_PIN >> col_idx) & 1)) { // Sprawdź ponownie po debouncingu
                             last_pressed_key = pressed_key;
                             // Czekaj, aż przycisk zostanie zwolniony (pin powróci do HIGH dzięki pull-up)
                             while (!((KEYPAD_COLS_PIN >> col_idx) & 1)) {
                                _delay_ms(10);
                             }
                             // Po zwolnieniu przycisku, ustaw wszystkie rzędy z powrotem na HIGH
                             KEYPAD_ROWS_PORT |= 0x0F;
                             return pressed_key;
                        }
                    }
                }
            }
        }
        // Po sprawdzeniu rzędu, ustaw go z powrotem na HIGH przed przejściem do następnego
        KEYPAD_ROWS_PORT |= (1 << keypad_row_pins[row_idx]);
    }
    // Jeśli żaden przycisk nie został naciśnięty po wszystkich rzędach
    last_pressed_key = 0; // Resetuj ostatnio naciśnięty klawisz
    KEYPAD_ROWS_PORT |= 0x0F; // Upewnij się, że wszystkie rzędy są z powrotem na HIGH
    return 0; // Brak naciśniętego przycisku
}

void turn_on_led(uint8_t led_pin) {
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC &= ~(1 << led_pin); // LOW zapala
    } else { // LED_AUTO na PB
        LED_PORT_PB &= ~(1 << led_pin); // LOW zapala
    }
}

void turn_off_led(uint8_t led_pin) {
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC |= (1 << led_pin); // HIGH gasi
    } else { // LED_AUTO na PB
        LED_PORT_PB |= (1 << led_pin); // HIGH gasi
    }
}

void toggle_led(uint8_t led_pin) {
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC ^= (1 << led_pin);
    } else { // LED_AUTO na PB
        LED_PORT_PB ^= (1 << led_pin);
    }
}

// Funkcja resetuje wszystkie zmienne gry do stanu początkowego
void reset_game_state(void) {
    game_state = 0;
    player_account = 1000;
    current_bet = 0;
    multiplier = 1;
    rolled_digit = 0;
    auto_roll_enabled = 0;
    num_digits_to_roll = 3;
    turn_off_led(LED_WIN);
    turn_off_led(LED_LOSE);
    turn_off_led(LED_AUTO);
    display_mode = 0; // Tryb finansowy

    // Ustawia wartości wyświetlania na podstawie player_account
    update_display_values();
    animation_active = 0;
    animation_frame_counter = 0;
}

// Funkcja aktualizuje wartości do wyświetlenia (display_d1-d4)
void update_display_values(void) {
    if (display_mode == 0) { // Tryb finansowy (konto/mnożnik)
        if (game_state == 3) { // Tryb edycji mnożnika
            display_d1 = 0;
            display_d2 = 0;
            display_d3 = 0;
            display_d4 = multiplier; // Mnożnik jest jednocyfrowy
        } else { // Konto gracza (pokazujemy całe 4 cyfry konta)
            display_d1 = (player_account / 1000) % 10;
            display_d2 = (player_account / 100) % 10;
            display_d3 = (player_account / 10) % 10;
            display_d4 = player_account % 10;
        }
    } else { // Tryb gry (wylosowana cyfra)
        display_d1 = 0;
        display_d2 = 0;
        display_d3 = 0;
        display_d4 = rolled_digit;
    }
}

// --- Funkcje logiki gry ---
void game_start(void) {
    game_state = 1;
    player_account = 1000;
    current_bet = 0;
    multiplier = 1;
    rolled_digit = 0;
    auto_roll_enabled = 0;
    num_digits_to_roll = 3;
    turn_off_led(LED_WIN);
    turn_off_led(LED_LOSE);
    turn_off_led(LED_AUTO);
    display_mode = 0;
    update_display_values();
}

void quick_game(void) {
    game_state = 2;
    current_bet = 10;
    multiplier = 1;
    num_digits_to_roll = 3;
    turn_off_led(LED_WIN);
    turn_off_led(LED_LOSE);
    turn_off_led(LED_AUTO);
    display_mode = 0;
    update_display_values();
}

void edit_multiplier(void) {
    game_state = 3;
    display_mode = 0;
    update_display_values();
}

void roll_digit(void) {
    rolled_digit = rand() % 10;
    update_display_values();
}

void make_deposit(uint32_t amount) {
    player_account += amount;
    uint8_t prev_display_mode = display_mode;
    display_mode = 0; // Przełącz na tryb finansowy by pokazać zmianę
    update_display_values();
    _delay_ms(1500); // Widoczne opóźnienie
    display_mode = prev_display_mode; // Wróć do poprzedniego trybu
    update_display_values();
}

void make_withdrawal(uint32_t amount) {
    if (player_account >= amount) {
        player_account -= amount;
    } else {
        player_account = 0;
    }
    uint8_t prev_display_mode = display_mode;
    display_mode = 0; // Przełącz na tryb finansowy by pokazać zmianę
    update_display_values();
    _delay_ms(1500); // Widoczne opóźnienie
    display_mode = prev_display_mode; // Wróć do poprzedniego trybu
    update_display_values();
}

void game_end(void) {
    game_state = 0;
    current_bet = 0;
    multiplier = 1;
    auto_roll_enabled = 0;
    turn_off_led(LED_WIN);
    turn_off_led(LED_LOSE);
    turn_off_led(LED_AUTO);
    display_mode = 0;
    update_display_values();
}

void clear_bet(void) {
    current_bet = 0;
    uint8_t prev_display_mode = display_mode;
    display_mode = 0;
    update_display_values(); // Pokazujemy "0000"
    _delay_ms(500);
    display_mode = prev_display_mode;
    update_display_values();
}

void clear_multiplier(void) {
    multiplier = 1;
    update_display_values();
}

void set_num_digits_to_roll(void) {
    num_digits_to_roll = (num_digits_to_roll == 3) ? 4 : 3;
    // Tymczasowo pokaż liczbę cyfr na wyświetlaczu
    uint8_t prev_rolled_digit = rolled_digit;
    rolled_digit = num_digits_to_roll;
    uint8_t prev_display_mode = display_mode;
    display_mode = 1; // Przełącz na tryb gry
    update_display_values();
    _delay_ms(500);
    rolled_digit = prev_rolled_digit;
    display_mode = prev_display_mode;
    update_display_values();
}

void switch_display_mode(void) {
    cli(); // Wyłącz globalne przerwania podczas przygotowywania animacji
    animation_active = 1; 
    animation_frame_counter = 0; // Reset licznika animacji

    // Aktywuj WSZYSTKIE cyfry podczas animacji (LOW dla CA)
    DIGIT_SELECT_PORT &= ~DIGIT_ENABLE_MASK;

    uint32_t target_animation_frames = 1000; // 2 sekundy / 2ms na tick = 1000 ticków

    sei(); // Włącz globalne przerwania ponownie, aby Timer0 działał i animował

    // Aktywne czekanie w pętli. Funkcja ISR(TIMER0_COMPA_vect) nadal działa.
    while (animation_frame_counter < target_animation_frames) {
        // Pusta pętla. Czekamy, aż przerwanie zwiększy animation_frame_counter.
    }
    
    // Po zakończeniu animacji, upewnij się, że jest wyłączona
    animation_active = 0;

    // Zresetuj porty wyświetlacza, aby usunąć kreski i przygotować na multipleksowanie
    SEGMENT_PORT = 0xFF; // Wyłącz wszystkie segmenty
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; // Wyłącz wszystkie cyfry

    // Przełącz tryb wyświetlania
    display_mode = !display_mode; 
    update_display_values(); // Zaktualizuj wartości dla nowego trybu
}


// --- Obsługa Przerwań Timera0 ---
ISR(TIMER0_COMPA_vect) {
    if (animation_active) {
        run_animation(); // Jeśli animacja aktywna, uruchom ją
    } else {
        display_multiplex_task(); // Inaczej, multipleksuj wyświetlacz
    }

    timer_logic_counter++;
    if (timer_logic_counter >= 25) { // Co 25 * 2ms = 50ms
        timer_logic_flag = true; // Ustaw flagę dla głównej pętli
        timer_logic_counter = 0;
    }
}

// --- Główna Pętla Programu ---
int main(void) {
    initialize_ports();
    initialize_timer0();
    srand(1234); // Inicjalizacja generatora liczb losowych

    // Upewnij się, że stan gry jest poprawnie zainicjalizowany na start
    reset_game_state(); 

    while (1) {
        if (timer_logic_flag) { // Wykonuj logikę gry co 50ms
            timer_logic_flag = false; // Zresetuj flagę

            uint8_t pressed_key = read_keypad();

            // Tymczasowy blok do debugowania: zaświeć LED_WIN na krótko po naciśnięciu dowolnego klawisza
            if (pressed_key != 0) {
                turn_on_led(LED_WIN);
                _delay_ms(50); 
                turn_off_led(LED_WIN);
            }

            // Logika auto-kręcenia
            if (auto_roll_enabled && (game_state == 1 || game_state == 2)) {
                // Dodatkowe opóźnienie, żeby nie kręciło się zbyt szybko
                _delay_ms(500); // Uwaga: To _delay_ms jest blokujące!
                
                // Jeśli auto_roll_enabled jest włączone, a gracz ma środki, automatycznie wykonaj losowanie
                if (player_account >= current_bet) {
                    make_withdrawal(current_bet); // Odejmij zakład
                    roll_digit(); // Wylosuj cyfrę

                    // Sprawdź wynik losowania
                    if (rolled_digit == 7) {
                        turn_on_led(LED_WIN);
                        make_deposit(current_bet * multiplier);
                        _delay_ms(1000); // Czas na zobaczenie wygranej
                        turn_off_led(LED_WIN);
                    } else if (rolled_digit == 0) {
                        turn_on_led(LED_LOSE);
                        _delay_ms(1000); // Czas na zobaczenie przegranej
                        turn_off_led(LED_LOSE);
                    }
                } else {
                    auto_roll_enabled = 0; // Wyłącz auto_mode, jeśli brak środków
                    turn_off_led(LED_AUTO); // Zgaś diodę AUTO
                }
            }


            // Główna logika obsługi przycisków
            switch (pressed_key) {
                // Rząd 1
                case 1: game_start(); break;         // S1: Nowa Gra (Reset)
                case 2: edit_multiplier(); break;    // S2: Edytuj Mnożnik
                case 3: game_end(); break;           // S3: Zakończ Grę (Zresetuj stan)
                case 4:                              // S4: Auto-kręcenie ON/OFF
                    if (game_state == 1 || game_state == 2) {
                        auto_roll_enabled = !auto_roll_enabled;
                        toggle_led(LED_AUTO); 
                    }
                    break;
                
                // Rząd 2
                case 5: switch_display_mode(); break; // S5: Zmiana trybu wyświetlania (Finanse/Gra)
                case 6: // S6: Wolny (może być użyty do czegoś innego)
                    if (game_state == 3) { // Wyjście z trybu edycji mnożnika
                        game_state = 1;
                        display_mode = 0;
                        update_display_values();
                    }
                    break;
                case 7: set_num_digits_to_roll(); break; // S7: Zmień liczbę cyfr (3/4)
                case 8: // S8: Wolny (np. Dodaj 1 do konta dla testów)
                    make_deposit(100); // Dodaj 100 do konta
                    break;

                // Rząd 3
                case 9: // S9: Zakręć / Potwierdź mnożnik
                    if (game_state == 3) { // Tryb edycji mnożnika
                        // Mnożnik jest zmieniany przez ten przycisk
                        multiplier = (multiplier % 9) + 1; // Mnożnik od 1 do 9
                        update_display_values();
                    } else if (game_state == 1) { // Tryb gry - Zakręć
                        if (current_bet > 0 && player_account >= current_bet) {
                            make_withdrawal(current_bet); // Odejmij zakład
                            roll_digit(); // Wylosuj cyfrę
                            
                            // Sprawdź wynik losowania
                            if (rolled_digit == 7) {
                                turn_on_led(LED_WIN);
                                make_deposit(current_bet * multiplier);
                                _delay_ms(1000);
                                turn_off_led(LED_WIN);
                            } else if (rolled_digit == 0) {
                                turn_on_led(LED_LOSE);
                                _delay_ms(1000);
                                turn_off_led(LED_LOSE);
                            }
                        } else if (current_bet == 0) {
                            // Mignij LED_LOSE, jeśli brak zakładu
                            toggle_led(LED_LOSE); _delay_ms(100); toggle_led(LED_LOSE);
                        } else { // Brak wystarczających środków
                            toggle_led(LED_LOSE); _delay_ms(100); toggle_led(LED_LOSE);
                        }
                    }
                    break;
                case 10: quick_game(); break; // S10: Szybka gra (stawka 10)
                case 11: clear_bet(); break;     // S11: Wyczyść stawkę
                case 12: clear_multiplier(); break; // S12: Wyczyść mnożnik (ustaw na 1)

                // Rząd 4
                case 13: if (game_state == 1) { current_bet += 1; update_display_values(); } break;   // S13: Stawka +1
                case 14: if (game_state == 1) { current_bet += 10; update_display_values(); } break;  // S14: Stawka +10
                case 15: if (game_state == 1) { current_bet += 100; update_display_values(); } break; // S15: Stawka +100
                case 16: if (game_state == 1) { current_bet += 1000; update_display_values(); } break;// S16: Stawka +1000
            }
        }
    }
}