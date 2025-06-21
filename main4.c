#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdlib.h> // Do funkcji rand() i srand()

// --- Definicje pinów i portów ---

// Piny segmentów (A-G, DP) - wspólne dla wszystkich cyfr na jednym wyświetlaczu
#define SEGMENT_PORT PORTD
#define SEGMENT_DDR DDRD
// PD0-PD6 dla segmentów A-G
// PD7 dla segmentu DP (kropki dziesiętnej) - opcjonalnie, jeśli chcesz jej używać

// Piny wyboru cyfry (digit select) - PC0-PC3
// ZAKŁADAMY: Wyświetlacz jest Common Anode, więc aktywacja pinem LOW
#define DIGIT_SELECT_PORT PORTC
#define DIGIT_SELECT_DDR DDRC
#define DIGIT_1_ENABLE_PIN PC0 // Most significant digit (MSD) - lewa cyfra 1
#define DIGIT_2_ENABLE_PIN PC1 // Cyfra 2
#define DIGIT_3_ENABLE_PIN PC2 // Cyfra 3
#define DIGIT_4_ENABLE_PIN PC3 // Least significant digit (LSD) - prawa cyfra 4
// Maska dla pinów wyboru cyfry
#define DIGIT_ENABLE_MASK ((1 << DIGIT_1_ENABLE_PIN) | (1 << DIGIT_2_ENABLE_PIN) | \
                           (1 << DIGIT_3_ENABLE_PIN) | (1 << DIGIT_4_ENABLE_PIN))

// Piny dla segmentów (dla wspólnej anody - CA)
// gfedcba - pamiętaj o kolejności podpięcia segmentów do pinów PD0-PD6
// 0 oznacza zaświecony segment, 1 oznacza zgaszony segment
const uint8_t segment_map_common_anode[] = {
    //   gfedcba
    0b01000000, // 0
    0b01111001, // 1
    0b00100100, // 2
    0b00001000, // 3
    0b00011001, // 4
    0b00010010, // 5
    0b00000010, // 6
    0b01111000, // 7
    0b00000000, // 8
    0b00001000  // 9
};
// Maska dla segmentu G (bit 6)
#define SEG_G_MASK (1 << 6)


// Klawiatura 4x4
// Rzędy na PORTB (PB0-PB3)
#define KEYPAD_ROWS_DDR DDRB
#define KEYPAD_ROWS_PORT PORTB
#define KEYPAD_ROWS_PIN PINB
const uint8_t keypad_row_pins[] = {PB0, PB1, PB2, PB3}; // Piny dla wierszy (PB0, PB1, PB2, PB3)

// Kolumny na PORTE (PE0-PE3)
#define KEYPAD_COLS_DDR DDRE
#define KEYPAD_COLS_PORT PORTE
#define KEYPAD_COLS_PIN PINE
const uint8_t keypad_col_pins[] = {PE0, PE1, PE2, PE3}; // Piny dla kolumn (PE0, PE1, PE2, PE3)

// Diody LED
#define LED_PORT_PC PORTC // PC4 dla LED_WIN, PC5 dla LED_LOSE
#define LED_DDR_PC DDRC
#define LED_WIN PC4
#define LED_LOSE PC5

#define LED_PORT_PB PORTB // PB4 dla LED_AUTO
#define LED_DDR_PB DDRB
#define LED_AUTO PB4


// --- Zmienne globalne ---
volatile uint8_t game_state = 0; // 0: oczekiwanie, 1: gra_start, 2: quick_game, 3: edytuj_mnoznik, 4: koniec_gry
volatile uint32_t player_account = 1000; // Początkowe konto gracza
volatile uint32_t current_bet = 0;      // Aktualna stawka
volatile uint8_t multiplier = 1;       // Mnożnik
volatile uint8_t rolled_digit = 0;     // Wylosowana cyfra
volatile uint8_t auto_roll_enabled = 0; // 0: OFF, 1: ON
volatile uint8_t num_digits_to_roll = 3; // Domyślna liczba cyfr do losowania (3 lub 4)

// Tryby wyświetlania na jednym wyświetlaczu
volatile uint8_t display_mode = 0; // 0: tryb finansowy (konto/mnożnik), 1: tryb gry (wylosowana cyfra)
volatile uint8_t animation_active = 0; // Flaga dla animacji segmentów G

// Zmienne przechowujące cyfry do wyświetlenia dla multipleksowania
// Dla trybu finansowego (wyświetlamy dwie ostatnie cyfry konta LUB mnożnik)
volatile uint8_t disp_financial_d3 = 0; // Trzecia cyfra od lewej (dziesiątki)
volatile uint8_t disp_financial_d4 = 0; // Czwarta cyfra od lewej (jednostki)
// Dla trybu gry (wyświetlamy wylosowaną cyfrę na D4)
volatile uint8_t disp_game_d4 = 0; // Czwarta cyfra od lewej (jednostki)


// Flagi i liczniki dla przerwań
volatile uint8_t timer_flag = 0;            // Flaga dla głównej logiki gry (rzadsze wywołania)
volatile uint8_t multiplex_counter = 0;     // Licznik dla multipleksowania wyświetlacza (szybkie wywołania)
volatile uint8_t animation_frame_counter = 0; // Licznik klatek animacji


// --- Prototypy funkcji ---
void initialize_ports(void);
void initialize_timer0(void);
void set_segment_data(uint8_t digit_value);
void select_digit_enable(uint8_t digit_pin); // Zmienione dla Common Anode
void display_update_task(void); // Główna funkcja multipleksująca
void run_animation(void);     // Funkcja animacji segmentów G
uint8_t read_keypad(void);
void turn_on_led(uint8_t led_pin);
void turn_off_led(uint8_t led_pin);
void toggle_led(uint8_t led_pin);

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
void update_display_values(void); // Funkcja aktualizująca globalne zmienne do wyświetlania
void switch_display_mode(void); // Nowa funkcja do przełączania trybów wyświetlania


// --- Implementacja funkcji ---

/**
 * @brief Inicjalizuje porty I/O.
 */
void initialize_ports(void) {
    // Segmenty wyświetlacza (PORTD PD0-PD6 jako wyjścia)
    SEGMENT_DDR = 0xFF; // Ustaw WSZYSTKIE piny PORTD jako wyjścia (PD0-PD7)
    SEGMENT_PORT = 0xFF; // Upewnij się, że wszystkie segmenty są wyłączone na start (HIGH dla CA)

    // Piny wyboru cyfry (PORTC PC0-PC3 jako wyjścia)
    DIGIT_SELECT_DDR |= DIGIT_ENABLE_MASK;
    // Domyślnie wyłącz wszystkie cyfry (stan HIGH dla Common Anode)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    // Klawiatura - rzędy (PORTB PB0-PB3 jako wyjścia, domyślnie wysoki stan)
    KEYPAD_ROWS_DDR |= 0x0F; // Piny PB0-PB3 jako wyjścia
    KEYPAD_ROWS_PORT |= 0x0F; // Ustaw domyślny stan wysoki dla PB0-PB3 (nieaktywne)

    // Klawiatura - kolumny (PORTE PE0-PE3 jako wejścia z Pull-up)
    KEYPAD_COLS_DDR &= ~0x0F; // Piny PE0-PE3 jako wejścia
    KEYPAD_COLS_PORT |= 0x0F; // Włącz wewnętrzne rezystory Pull-up dla PE0-PE3 (KLUCZOWE!)

    // Diody LED
    LED_DDR_PC |= (1 << LED_WIN) | (1 << LED_LOSE); // PC4, PC5 jako wyjścia
    LED_DDR_PB |= (1 << LED_AUTO); // PB4 jako wyjście

    turn_off_led(LED_WIN);
    turn_off_led(LED_LOSE);
    turn_off_led(LED_AUTO);
}

/**
 * @brief Inicjalizuje Timer0 do generowania przerwań dla multipleksowania (szybsze) i skanowania klawiatury.
 * Przerwanie co ok. 2ms dla odświeżania wyświetlacza.
 */
void initialize_timer0(void) {
    // Tryb CTC (Clear Timer on Compare Match)
    TCCR0A |= (1 << WGM01);

    // OCR0A dla przerwania co ~2ms (16MHz / 256 preskaler / 125 = 500 Hz -> 2ms)
    OCR0A = 124; // Ustawienie wartości do porównania dla przerwania co ~2ms

    // Włączenie preskalera (256) i start timera
    TCCR0B |= (1 << CS02); // Preskaler 256

    // Włączenie przerwania Compare Match A
    TIMSK0 |= (1 << OCIE0A);

    sei(); // Włącz globalne przerwania - KLUCZOWE!
}

/**
 * @brief Ustawia stany na pinach segmentów (A-G) dla danego kodu cyfry.
 * @param digit_value Cyfra (0-9) do wyświetlenia.
 */
void set_segment_data(uint8_t digit_value) {
    SEGMENT_PORT = segment_map_common_anode[digit_value]; // Użyj mapy dla Common Anode
}

/**
 * @brief Aktywuje pin wyboru cyfry (digit enable pin).
 * Zakładamy, że aktywacja odbywa się stanem LOW (digit enable LOW = cyfra włączona).
 * @param digit_pin Pin cyfry do aktywacji (np. DIGIT_1_ENABLE_PIN).
 */
void select_digit_enable(uint8_t digit_pin) {
    // Wyłącz wszystkie cyfry najpierw (ustaw na HIGH)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    // Aktywuj tylko wybraną cyfrę (ustaw na LOW)
    DIGIT_SELECT_PORT &= ~(1 << digit_pin);
}

/**
 * @brief Funkcja do cyklicznego odświeżania wyświetlaczy (multipleksowanie).
 * Wywoływana z przerwania Timer0.
 */
void display_update_task(void) {
    static uint8_t current_digit_index = 0; // 0: D1, 1: D2, 2: D3, 3: D4
    uint8_t digit_value_to_display = 0;
    uint8_t digit_enable_pin = 0;

    // Krok 1: Wyłącz wszystkie segmenty, aby uniknąć ghostingu (HIGH dla CA)
    SEGMENT_PORT = 0xFF; 

    // Krok 2: Wyłącz wszystkie cyfry (digit enable) (HIGH dla CA)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; 

    // Krok 3: Wybierz, która cyfra będzie następna i jakie wartości ma wyświetlić
    switch (current_digit_index) {
        case 0: // Cyfra 1 (MSD - Most Significant Digit)
            digit_value_to_display = 0; // W naszej grze nie używamy tej cyfry
            digit_enable_pin = DIGIT_1_ENABLE_PIN;
            break;
        case 1: // Cyfra 2
            digit_value_to_display = 0; // W naszej grze nie używamy tej cyfry
            digit_enable_pin = DIGIT_2_ENABLE_PIN;
            break;
        case 2: // Cyfra 3 (D3) - setki/dziesiątki (tryb finansowy)
            if (display_mode == 0) { // Tryb finansowy
                digit_value_to_display = disp_financial_d3;
            } else { // Tryb gry - nieużywana
                digit_value_to_display = 0;
            }
            digit_enable_pin = DIGIT_3_ENABLE_PIN;
            break;
        case 3: // Cyfra 4 (LSD - Least Significant Digit) - jednostki (w obu trybach)
            if (display_mode == 0) { // Tryb finansowy
                digit_value_to_display = disp_financial_d4;
            } else { // Tryb gry
                digit_value_to_display = disp_game_d4;
            }
            digit_enable_pin = DIGIT_4_ENABLE_PIN;
            break;
    }

    // Krok 4: Ustaw segmenty dla aktualnej cyfry
    set_segment_data(digit_value_to_display);

    // Krok 5: Aktywuj wybraną cyfrę (digit enable) (LOW dla CA)
    select_digit_enable(digit_enable_pin); 

    current_digit_index++;
    if (current_digit_index >= 4) { // Po czwartej cyfrze wróć do pierwszej
        current_digit_index = 0;
    }
}

/**
 * @brief Funkcja animacji "kasyno" - miganie segmentów G.
 * Wywoływana z przerwania Timer0, gdy animation_active jest true.
 */
void run_animation(void) {
    animation_frame_counter++;
    
    // Wyłącz wszystkie segmenty na początku animacji (HIGH dla CA)
    SEGMENT_PORT = 0xFF; 

    if (animation_frame_counter % 2 == 0) { // Miganie co 2 wywołania (szybkie)
        SEGMENT_PORT &= ~SEG_G_MASK; // Włącz segment G (LOW dla CA)
    } else {
        // Pozostaw segment G wyłączony (HIGH dla CA)
        SEGMENT_PORT |= SEG_G_MASK; 
    }
    // Aktywuj wszystkie cyfry jednocześnie dla animacji (ustaw na LOW dla CA)
    DIGIT_SELECT_PORT &= ~DIGIT_ENABLE_MASK; 
}


/**
 * @brief Odczytuje stan klawiatury 4x4.
 * @return Kod przycisku (1-16) lub 0, jeśli żaden przycisk nie został naciśnięty.
 */
uint8_t read_keypad(void) {
    uint8_t key_pressed = 0;

    // Ustaw wszystkie rzędy na HIGH (nieaktywne)
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
                    key_pressed = (row_idx * 4) + col_idx + 1;
                    _delay_ms(50); // Debouncing - krótka pauza

                    // Czekaj, aż przycisk zostanie zwolniony
                    while (!((KEYPAD_COLS_PIN >> col_idx) & 1)) {
                        _delay_ms(10);
                    }
                    // Po zwolnieniu przycisku, ustaw wszystkie rzędy z powrotem na HIGH
                    KEYPAD_ROWS_PORT |= 0x0F;
                    return key_pressed; 
                }
            }
        }
        // Po sprawdzeniu rzędu, ustaw go z powrotem na HIGH przed przejściem do następnego
        KEYPAD_ROWS_PORT |= (1 << keypad_row_pins[row_idx]);
    }
    // Jeśli żaden przycisk nie został naciśnięty po wszystkich rzędach
    KEYPAD_ROWS_PORT |= 0x0F; // Upewnij się, że wszystkie rzędy są z powrotem na HIGH
    return 0; // Brak naciśniętego przycisku
}

/**
 * @brief Zapala wskazaną diodę LED.
 * @param led_pin Pin diody LED (używaj LED_WIN, LED_LOSE, LED_AUTO).
 */
void turn_on_led(uint8_t led_pin) {
    // Zakładamy, że LEDy są podłączone do VCC, a do pinu mikrokontrolera jest rezystor i GND
    // Więc LOW zapala LED
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC &= ~(1 << led_pin);
    } else { // LED_AUTO na PB
        LED_PORT_PB &= ~(1 << led_pin);
    }
}

/**
 * @brief Gasi wskazaną diodę LED.
 * @param led_pin Pin diody LED (używaj LED_WIN, LED_LOSE, LED_AUTO).
 */
void turn_off_led(uint8_t led_pin) {
    // Zakładamy, że LEDy są podłączone do VCC, a do pinu mikrokontrolera jest rezystor i GND
    // Więc HIGH gasi LED
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC |= (1 << led_pin);
    } else { // LED_AUTO na PB
        LED_PORT_PB |= (1 << led_pin);
    }
}

/**
 * @brief Przełącza stan wskazanej diody LED.
 * @param led_pin Pin diody LED (używaj LED_WIN, LED_LOSE, LED_AUTO).
 */
void toggle_led(uint8_t led_pin) {
    if (led_pin == LED_WIN || led_pin == LED_LOSE) {
        LED_PORT_PC ^= (1 << led_pin);
    } else { // LED_AUTO na PB
        LED_PORT_PB ^= (1 << led_pin);
    }
}

/**
 * @brief Aktualizuje globalne zmienne display_digitX_value w zależności od trybu.
 */
void update_display_values(void) {
    if (display_mode == 0) { // Tryb finansowy (konto/mnożnik)
        if (game_state == 3) { // Tryb edycji mnożnika
            disp_financial_d3 = 0; // Mnożnik jest jednocyfrowy
            disp_financial_d4 = multiplier;
        } else { // Konto gracza (pokazujemy dwie ostatnie cyfry)
            disp_financial_d3 = (player_account / 10) % 10;
            disp_financial_d4 = player_account % 10;
        }
        disp_game_d4 = 0; // Upewnij się, że cyfra gry jest zerem
    } else { // Tryb gry (wylosowana cyfra)
        disp_game_d4 = rolled_digit;
        disp_financial_d3 = 0; // Upewnij się, że cyfry finansowe są zerami
        disp_financial_d4 = 0;
    }
}

/**
 * @brief Przełącza tryb wyświetlania i uruchamia animację.
 */
void switch_display_mode(void) {
    animation_active = 1; // Włącz animację
    _delay_ms(2000); // Trzymaj animację przez 2 sekundy (blokująco)
    animation_active = 0; // Wyłącz animację

    // Upewnij się, że segmenty i cyfry są wyłączone po animacji
    SEGMENT_PORT = 0xFF; // Wszystkie segmenty OFF (HIGH)
    DIGIT_SELECT_PORT |= DIGIT_ENABLE_MASK; // Wszystkie cyfry OFF (HIGH)

    display_mode = !display_mode; // Przełącz tryb
    update_display_values(); // Zaktualizuj wartości dla nowego trybu
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
    display_mode = 0; // Ustaw na tryb finansowy na start
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
    display_mode = 0; // Ustaw na tryb finansowy
    update_display_values();
}

void edit_multiplier(void) {
    game_state = 3;
    display_mode = 0; // Przejdź do trybu finansowego, aby pokazać mnożnik
    update_display_values();
}

void roll_digit(void) {
    rolled_digit = rand() % 10;
    update_display_values(); // Zaktualizuj wartość w trybie gry
}

void make_deposit(uint16_t amount) {
    player_account += amount;
    uint8_t prev_display_mode = display_mode; // Zapamiętaj tryb
    display_mode = 0; // Przejdź na chwilę do trybu finansowego
    update_display_values();
    _delay_ms(1500); // Przytrzymaj przez chwilę
    display_mode = prev_display_mode; // Wróć do poprzedniego trybu
    update_display_values();
}

void make_withdrawal(uint16_t amount) {
    if (player_account >= amount) {
        player_account -= amount;
    } else {
        player_account = 0;
    }
    uint8_t prev_display_mode = display_mode; // Zapamiętaj tryb
    display_mode = 0; // Przejdź na chwilę do trybu finansowego
    update_display_values();
    _delay_ms(1500); // Przytrzymaj przez chwilę
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
    display_mode = 0; // Wróć do trybu finansowego
    update_display_values();
}

void clear_bet(void) {
    current_bet = 0;

    // Zachowaj obecny tryb wyświetlania
    uint8_t prev_display_mode = display_mode;
    
    // Przejdź na chwilę do trybu finansowego, aby wyświetlić "00"
    display_mode = 0;
    disp_financial_d3 = 0; // Ustaw dziesiątki na 0
    disp_financial_d4 = 0; // Ustaw jednostki na 0
    update_display_values(); // Upewnij się, że wartości są zaktualizowane dla multipleksera

    // Wyświetl "00" przez krótki czas
    _delay_ms(500); 

    // Przywróć poprzedni tryb wyświetlania i zaktualizuj wartości
    display_mode = prev_display_mode;
    update_display_values();
}

void clear_multiplier(void) {
    multiplier = 1;
    update_display_values();
}

void set_num_digits_to_roll(void) {
    num_digits_to_roll = (num_digits_to_roll == 3) ? 4 : 3;
    // Tymczasowo wyświetl na D4
    uint8_t prev_rolled_digit = rolled_digit;
    rolled_digit = num_digits_to_roll;
    uint8_t prev_display_mode = display_mode;
    display_mode = 1; // Przejdź do trybu gry
    update_display_values();
    _delay_ms(500);
    rolled_digit = prev_rolled_digit; // Przywróć poprzednią wartość
    display_mode = prev_display_mode; // Wróć do poprzedniego trybu
    update_display_values();
}


// --- Obsługa przerwań ---
ISR(TIMER0_COMPA_vect) {
    if (animation_active) {
        run_animation();
    } else {
        display_update_task(); // Multipleksowanie wyświetlaczy (wywoływane co ~2ms)
    }

    // Co ~50ms (co 25 wywołań przerwania) ustaw flagę do logiki gry
    multiplex_counter++;
    if (multiplex_counter >= 25) { // 25 * 2ms = 50ms
        timer_flag = 1; // Ustaw flagę dla głównej pętli
        multiplex_counter = 0;
    }
}

// --- Główna pętla programu ---

int main(void) {
    initialize_ports();
    initialize_timer0();
    srand(1234); // Na AVR bez RTC, stała wartość jest OK dla testów

    // Ustaw początkowe wartości dla wyświetlacza
    display_mode = 0; // Domyślnie tryb finansowy
    update_display_values();
    animation_active = 0; // Upewnij się, że animacja jest wyłączona na start

    while (1) {
        if (timer_flag) {
            timer_flag = 0;

            uint8_t pressed_key = read_keypad();

            // DODAJ TEN BLOK KODU DO TESTOWANIA:
            if (pressed_key != 0) {
                // Jeśli jakiś przycisk został naciśnięty, zaświeć LED_WIN
                turn_on_led(LED_WIN);
                // Możesz też dodać krótkie opóźnienie, żeby było widać
                _delay_ms(100); 
                turn_off_led(LED_WIN);
            }
            // KONIEC BLOKU KODU DO TESTOWANIA

            if (auto_roll_enabled && (game_state == 1 || game_state == 2)) {
                _delay_ms(500); // Opóźnienie między losowaniami w trybie auto
                roll_digit(); // Aktualizuje rolled_digit i wywołuje update_display_values()
                if (rolled_digit == 7) {
                    turn_on_led(LED_WIN);
                    if (current_bet > 0) make_deposit(current_bet * multiplier);
                    _delay_ms(1000);
                    turn_off_led(LED_WIN);
                } else if (rolled_digit == 0) {
                    turn_on_led(LED_LOSE);
                    if (current_bet > 0) make_withdrawal(current_bet);
                    _delay_ms(1000);
                    turn_off_led(LED_LOSE);
                }
            }

            switch (pressed_key) {
                case 1: game_start(); break;
                case 2: edit_multiplier(); break;
                case 3: game_end(); break;
                case 4: // Auto-kręcenie ON/OFF
                    if (game_state == 1 || game_state == 2) {
                        auto_roll_enabled = !auto_roll_enabled;
                        if (auto_roll_enabled) { turn_on_led(LED_AUTO); }
                        else { turn_off_led(LED_AUTO); }
                    }
                    break;
                case 13: if (game_state == 1) { current_bet += 1; update_display_values(); } break;
                case 14: if (game_state == 1) { current_bet += 10; update_display_values(); } break;
                case 15: if (game_state == 1) { current_bet += 100; update_display_values(); } break;
                case 16: if (game_state == 1) { current_bet += 1000; update_display_values(); } break;
                case 9: // Mnożnik / Zakręć
                    if (game_state == 3) { // Tryb edycji mnożnika
                        multiplier = (multiplier % 9) + 1;
                        update_display_values(); // Odśwież wyświetlanie mnożnika
                    } else if (game_state == 1) { // Tryb "gra_start" - przycisk "Zakręć"
                        if (current_bet > 0) {
                            make_withdrawal(current_bet);
                            roll_digit();
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
                        }
                    }
                    break;
                case 10: // Szybka gra
                    if (game_state == 0) {
                        quick_game();
                    }
                    break;
                case 11: clear_bet(); break;
                case 12: clear_multiplier(); break;

                case 5: // Przycisk 2_1: Zmiana wyświetlacza (tryb finansowy <-> tryb gry)
                    switch_display_mode();
                    break;
                case 6: // Przycisk 2_2: Wolny
                    // Tutaj możesz dodać funkcjonalność
                    break;
                case 7: // Przycisk 2_3: Wolny (np. wyjście z trybu edycji mnożnika)
                    if (game_state == 3) {
                        game_state = 1; // Powrót do stanu gry po edycji mnożnika
                        display_mode = 0; // Wróć do trybu finansowego
                        update_display_values();
                    }
                    break;
                case 8: // Przycisk 2_4: Wolny
                    // Tutaj możesz dodać funkcjonalność
                    break;
            }
        }
    }
}