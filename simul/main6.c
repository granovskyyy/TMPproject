/*
 * Gra typu Blackjack dla ATmega328PB
 * Autorzy: KO TG
 * Data: 22.06.2025
 * Środowisko: Microchip Studio
 * Język: C
 */

#define F_CPU 16000000UL // Taktowanie procesora 16MHz

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h> // Dla funkcji rand() i srand()
#include <stdbool.h>

// --- DEFINICJE SPRZĘTOWE ---

// Diody LED
#define LED_WIN_PORT    PORTB
#define LED_WIN_PIN     PB5
#define LED_LOSE_PORT   PORTC
#define LED_LOSE_PIN    PC4
#define LED_QS_PORT     PORTC
#define LED_QS_PIN      PC5

// Wyświetlacz 7-segmentowy
#define SEGMENT_PORT    PORTD // PD0-PD7 to A-G, DP
#define DIGIT_PORT      PORTC // PC0-PC3 to D1-D4

// Klawiatura 4x4
#define KEY_ROW_PORT    PORTB
#define KEY_ROW_PIN     PINB
#define KEY_ROW_DDR     DDRB
#define KEY_COL_PORT    PORTE
#define KEY_COL_PIN     PINE
#define KEY_COL_DDR     DDRE

// --- DEFINICJE PRZYCISKÓW (zgodnie z numeracją 1-16) ---
#define KEY_NEW_GAME          1
#define KEY_EDIT_MULTIPLIER   2
#define KEY_END_GAME          3
#define KEY_QUICK_SPIN        4
#define KEY_CHANGE_MODE       5
#define KEY_BACK              6
// Klawisze 7 i 8 - bez funkcji
#define KEY_SPIN_MULT_X1_5    9
#define KEY_ALL_IN_MULT_X2    10
#define KEY_ZERO_BET          11
#define KEY_RESET_MULTIPLIER  12
#define KEY_BET_P1_1          13
#define KEY_BET_P1_2          14
#define KEY_BET_P1_3          15
#define KEY_BET_P1_4          16

// --- ZMIENNE GLOBALNE ---

// Tablica kodów dla cyfr 0-9 i znaku '-' (WSPÓLNA ANODA: 0=ON, 1=OFF)
// Wartości są zanegowane bitowo w stosunku do wersji ze wspólną katodą.
const uint8_t segment_map[11] = {
	~0b00111111, // 0
	~0b00000110, // 1
	~0b01011011, // 2
	~0b01001111, // 3
	~0b01100110, // 4
	~0b01101101, // 5
	~0b01111101, // 6
	~0b00000111, // 7
	~0b01111111, // 8
	~0b01101111, // 9
	~0b01000000  // - (znak minus/myślnik)
};

volatile uint8_t display_digits[4] = {10, 10, 10, 10}; // Cyfry do wyświetlenia (10 oznacza '-')
volatile uint8_t current_digit_idx = 0; // Indeks aktualnie multipleksowanej cyfry

// Stany gry
typedef enum {
	MODE_GAME,
	MODE_FINANCE
} GameMode;

GameMode current_mode = MODE_GAME;
bool game_active = false;
bool quick_spin_active = false;

long player_balance = 10000; // Początkowe saldo gracza
int current_bet = 0;
uint8_t multiplier_x10 = 10; // Mnożnik x10 (10 = x1.0, 15 = x1.5, 20 = x2.0)

// --- DEKLARACJE FUNKCJI ---
void init_hardware();
void init_timer_display();
void update_display_content(long value);
void show_dash();
uint8_t scan_keypad();
void handle_key_press(uint8_t key);
void perform_spin();

// --- Główna funkcja programu ---
int main(void) {
	init_hardware();
	init_timer_display();
	srand(0); // Inicjalizacja generatora liczb losowych
	sei();    // Włączenie globalnych przerwań

	show_dash(); // Początkowy stan wyświetlacza "----"

	uint8_t key_pressed = 0;
	uint8_t last_key = 0;

	while (1) {
		key_pressed = scan_keypad();

		if (key_pressed != 0 && key_pressed != last_key) {
			handle_key_press(key_pressed);
			_delay_ms(50); // Proste opóźnienie dla debouncingu
		}
		last_key = key_pressed;
		
		if(quick_spin_active) {
			// Szybkie miganie diodą Quick Spin
			LED_QS_PORT ^= (1 << LED_QS_PIN);
			_delay_ms(50);
		}
	}
}

// --- IMPLEMENTACJE FUNKCJI ---

void init_hardware() {
	// Konfiguracja portów dla diod LED jako wyjścia
	DDRB |= (1 << LED_WIN_PIN);
	DDRC |= (1 << LED_LOSE_PIN) | (1 << LED_QS_PIN);
	// Zgaszenie diod
	LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
	LED_QS_PORT &= ~(1 << LED_QS_PIN);

	// Konfiguracja portu D (segmenty) jako wyjścia
	DDRD = 0xFF;
	PORTD = 0xFF; // ZMIANA DLA WSPÓLNEJ ANODY: Stan wysoki wyłącza wszystkie segmenty

	// Konfiguracja portu C (cyfry D1-D4) jako wyjścia
	DDRC |= (1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3);
	// ZMIANA DLA WSPÓLNEJ ANODY: Wyłączenie wszystkich cyfr (stan niski)
	DIGIT_PORT &= ~((1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3));

	// Konfiguracja klawiatury (wiersze PB0-3 jako wejścia z pull-up, kolumny PE0-3 jako wyjścia)
	KEY_ROW_DDR &= ~((1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB3));
	KEY_ROW_PORT |= ((1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB3));
	
	KEY_COL_DDR |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
	KEY_COL_PORT |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
}

void init_timer_display() {
	// Użycie Timera0 do multipleksowania wyświetlacza
	// Tryb CTC (Clear Timer on Compare Match)
	TCCR0A |= (1 << WGM01);
	// Ustawienie preskalera na 64 (16MHz / 64 = 250kHz)
	TCCR0B |= (1 << CS01) | (1 << CS00);
	// Ustawienie wartości porównania (250kHz / 250 = 1kHz -> przerwanie co 1ms)
	// Odświeżanie całego wyświetlacza ~250 Hz, jednej cyfry 1kHz
	OCR0A = 249;
	// Włączenie przerwania od porównania
	TIMSK0 |= (1 << OCIE0A);
}

// Przerwanie od Timera0 - obsługa multipleksowania wyświetlacza
ISR(TIMER0_COMPA_vect) {
	// ZMIANA DLA WSPÓLNEJ ANODY: Wyłącz wszystkie cyfry (stan niski)
	DIGIT_PORT &= ~((1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3));

	// Ustaw segmenty dla bieżącej cyfry (mapa jest już zanegowana)
	SEGMENT_PORT = segment_map[display_digits[current_digit_idx]];

	// ZMIANA DLA WSPÓLNEJ ANODY: Włącz tylko bieżącą cyfrę (stan wysoki)
	DIGIT_PORT |= (1 << (PC0 + current_digit_idx));

	// Przejdź do następnej cyfry
	current_digit_idx = (current_digit_idx + 1) % 4;
}

// Funkcja aktualizująca zawartość bufora wyświetlacza na podstawie liczby
void update_display_content(long value) {
	if (value > 9999) value = 9999;
	display_digits[0] = (value / 1000) % 10; // Tysiące
	display_digits[1] = (value / 100) % 10;  // Setki
	display_digits[2] = (value / 10) % 10;   // Dziesiątki
	display_digits[3] = value % 10;          // Jedności
}

// Funkcja wyświetlająca "----"
void show_dash() {
	display_digits[0] = 10;
	display_digits[1] = 10;
	display_digits[2] = 10;
	display_digits[3] = 10;
}

// Funkcja skanująca klawiaturę matrycową 4x4
uint8_t scan_keypad() {
	for (uint8_t col = 0; col < 4; col++) {
		// Ustaw wszystkie kolumny jako wejścia
		KEY_COL_DDR &= ~((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
		KEY_COL_PORT |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3)); // Pull-up

		// Ustaw bieżącą kolumnę jako wyjście LOW
		KEY_COL_DDR |= (1 << (PE0 + col));
		KEY_COL_PORT &= ~(1 << (PE0 + col));

		_delay_us(1); // Czas na ustabilizowanie się stanów

		for (uint8_t row = 0; row < 4; row++) {
			if (!(KEY_ROW_PIN & (1 << (PB0 + row)))) {
				// Klawisz wciśnięty
				return (row * 4) + col + 1;
			}
		}
	}
	return 0; // Żaden klawisz nie jest wciśnięty
}

void display_mode_change_effect() {
	uint8_t temp_digits[4];
	for(int i=0; i<4; i++) temp_digits[i] = display_digits[i];
	
	show_dash();
	_delay_ms(500);
	
	for(int i=0; i<4; i++) display_digits[i] = temp_digits[i];
}


// Główna logika obsługi wciśniętych klawiszy
void handle_key_press(uint8_t key) {
	// Jeśli gra nie jest aktywna, tylko przycisk 1 działa
	if (!game_active && key != KEY_NEW_GAME) {
		return;
	}

	switch (key) {
		case KEY_NEW_GAME:
			game_active = true;
			player_balance = 10000;
			current_bet = 0;
			multiplier_x10 = 10;
			current_mode = MODE_GAME;
			update_display_content(0);
			LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
			LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
			break;

		case KEY_EDIT_MULTIPLIER: // Służy też jako powrót
		case KEY_BACK:
			if (current_mode == MODE_GAME) {
				current_mode = MODE_FINANCE;
				update_display_content(current_bet);
			} else {
				current_mode = MODE_GAME;
				update_display_content(0); // W trybie gry po edycji pokazujemy 0, czekając na spin
			}
			display_mode_change_effect();
			break;

		case KEY_END_GAME:
			if (current_mode == MODE_GAME) {
				show_dash();
				game_active = false;
				LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
				LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
			}
			break;

		case KEY_QUICK_SPIN:
			if (current_mode == MODE_GAME && player_balance >= 1000) {
				current_bet = 1000;
				multiplier_x10 = 10; // Mnożnik x1
				quick_spin_active = true;
				perform_spin();
				quick_spin_active = false;
				LED_QS_PORT &= ~(1 << LED_QS_PIN);
			}
			break;

		case KEY_CHANGE_MODE:
			current_mode = (current_mode == MODE_GAME) ? MODE_FINANCE : MODE_GAME;
			if (current_mode == MODE_FINANCE) {
				update_display_content(current_bet);
				} else {
				update_display_content(0);
			}
			display_mode_change_effect();
			break;

		case KEY_SPIN_MULT_X1_5:
			if (current_mode == MODE_GAME) {
				if(current_bet > 0 && current_bet <= player_balance) {
					perform_spin();
				}
			} else { // MODE_FINANCE
				multiplier_x10 = 15; // x1.5
			}
			break;

		case KEY_ALL_IN_MULT_X2:
			if (current_mode == MODE_GAME) { // Quick Game (All-in)
				if(player_balance > 0) {
					current_bet = player_balance;
					multiplier_x10 = 10; // x1.0
					perform_spin();
				}
			} else { // MODE_FINANCE
				multiplier_x10 = 20; // x2.0
			}
			break;

		case KEY_ZERO_BET:
			if (current_mode == MODE_FINANCE) {
				current_bet = 0;
				update_display_content(current_bet);
			}
			break;

		case KEY_RESET_MULTIPLIER:
			if (current_mode == MODE_FINANCE) {
				multiplier_x10 = 10; // x1.0
			}
			break;

		// Obsługa przycisków +1 do stawki
		case KEY_BET_P1_1:
		case KEY_BET_P1_2:
		case KEY_BET_P1_3:
		case KEY_BET_P1_4:
			if (current_mode == MODE_FINANCE) {
				if (current_bet < 9999) {
					current_bet++;
					update_display_content(current_bet);
				}
			}
			break;
	}
}

void perform_spin() {
	player_balance -= current_bet;
	
	LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);

	show_dash();
	_delay_ms(500);

	uint8_t result[4];
	
	// Losowanie pierwszej cyfry
	result[0] = rand() % 10;
	display_digits[0] = result[0];
	_delay_ms(500);

	// Losowanie drugiej cyfry (50% szansy na taką samą)
	result[1] = (rand() % 100 < 50) ? result[0] : rand() % 10;
	display_digits[1] = result[1];
	_delay_ms(500);

	// Losowanie trzeciej cyfry (30% szansy, jeśli dwie pierwsze są takie same)
	if (result[0] == result[1]) {
		result[2] = (rand() % 100 < 30) ? result[0] : rand() % 10;
	} else {
		result[2] = rand() % 10;
	}
	display_digits[2] = result[2];
	_delay_ms(500);

	// Losowanie czwartej cyfry (10% szansy, jeśli trzy pierwsze są takie same)
	if (result[0] == result[1] && result[1] == result[2]) {
		result[3] = (rand() % 100 < 10) ? result[0] : rand() % 10;
	} else {
		result[3] = rand() % 10;
	}
	display_digits[3] = result[3];
	_delay_ms(500);

	// Sprawdzenie wyniku
	if (result[0] == result[1] && result[1] == result[2] && result[2] == result[3]) {
		// WYGRANA
		long winnings = ((long)current_bet * multiplier_x10) / 10;
		player_balance += winnings;
		LED_WIN_PORT |= (1 << LED_WIN_PIN);
		LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
	} else {
		// PRZEGRANA
		LED_LOSE_PORT |= (1 << LED_LOSE_PIN);
		LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	}

	// Reset stawki i mnożnika po grze
	current_bet = 0;
	multiplier_x10 = 10;
}