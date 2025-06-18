#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdbool.h>

#define SEGMENT_PORT PORTD
#define SEGMENT_DDR  DDRD

#define DIGIT_PORT   PORTB
#define DIGIT_DDR    DDRB

#define BUTTON_PIN   PINC
#define BUTTON_PORT  PORTC
#define BUTTON_DDR   DDRC

#define BTN_SPIN     PC0
#define BTN_BET_UP   PC1
#define BTN_MUL_UP   PC2
#define BTN_AUTO     PC3
#define BTN_END      PC4

const uint8_t digit_to_segment[10] = {
	~(0b11111100),
	~(0b01100000),
	~(0b11011010),
	~(0b11110010),
	~(0b01100110),
	~(0b10110110),
	~(0b10111110),
	~(0b11100000),
	~(0b11111110),
	~(0b11110110)
};


volatile uint16_t balance = 0;
volatile uint8_t bet = 1;
volatile uint8_t multiplier = 1;
volatile bool auto_mode = false;
bool spin_in_progress = false;
uint16_t spin_counter = 0;


uint8_t digits[3] = {0, 0, 0};

bool is_button_pressed(uint8_t pin) {
	return !(BUTTON_PIN & (1 << pin));
}

void display_number(uint8_t a, uint8_t b, uint8_t c) {
	const uint8_t num[3] = {a, b, c};

	for (uint8_t i = 0; i < 3; ++i) {
		SEGMENT_PORT = digit_to_segment[num[i]];
		DIGIT_PORT = ~(1 << i);
		_delay_ms(2);
		DIGIT_PORT = 0xFF;
	}
}

void leds_on() {
	PORTB &= ~((1 << PB4) | (1 << PB5));
}

void leds_off() {
	PORTB |= (1 << PB4) | (1 << PB5);
}





void spin_step() {
	if (!spin_in_progress) {
		digits[0] = rand() % 10;
		digits[1] = rand() % 10;
		digits[2] = rand() % 10;
		spin_in_progress = true;
		spin_counter = 0;
	}

	display_number(digits[0], digits[1], digits[2]);
	spin_counter++;

	if (spin_counter > 200) {
		spin_in_progress = false;

		if (digits[0] == digits[1] && digits[1] == digits[2]) {
			balance += bet * multiplier;
		}
	}
	if (digits[0] == digits[1] && digits[1] == digits[2]) {
		uint8_t win = bet * multiplier;
		balance += win;
		leds_on();
		} else {
		leds_off();
	}

}


void init() {
	DDRD = 0xFF;
	PORTD = 0xFF;
	
	DDRB = 0xFF;
	PORTB = 0xFF;
	DDRB |= (1 << PB4) | (1 << PB5);
	PORTB &= ~((1 << PB4) | (1 << PB5));
	DDRC = 0b00001111;
	PORTC = 0xFF;
}



int scan_keypad() {
	static int last_key = -1;
	int current_key = -1;

	for (uint8_t col = 0; col < 4; col++) {
		PORTC |= 0xFF;             // ustaw wszystkie kolumny na 1
		PORTC &= ~(1 << col);      // ustaw aktywn¹ kolumnê na 0
		_delay_us(5);              // czas stabilizacji

		for (uint8_t row = 0; row < 2; row++) {
			if (!(PINC & (1 << (row + 4)))) {
				current_key = row * 4 + col;

				// debounce: sprawdŸ czy nadal naciœniêty po opóŸnieniu
				_delay_ms(20);
				if (!(PINC & (1 << (row + 4)))) {
					if (last_key != current_key) {
						last_key = current_key;

						// Poczekaj a¿ przycisk zostanie puszczony
						while (!(PINC & (1 << (row + 4)))) {
							_delay_ms(10);
						}

						return current_key;
					}
				}
			}
		}
	}
	last_key = -1;
	return -1;
}



int main(void) {
	init();
	int last_key = -1;
	bool just_stopped_auto = false;

	while (1) {
		int key = scan_keypad();

		if (key >= 0 && key != last_key && !spin_in_progress) {
			switch (key) {
				case 0: // Bet up
				bet = (bet % 9) + 1;
				break;
				case 1: // Multiplier up
				multiplier = (multiplier % 5) + 1;
				break;
				case 2: // Spin
				spin_in_progress = false; // restart spin
				spin_step();
				break;
				case 3: // Auto spin on/off
				auto_mode = !auto_mode;
				if (!auto_mode) {
					just_stopped_auto = true;
				}
				break;
				case 4: // Reset
				balance = 0;
				break;
			}
			last_key = key;
		}

		if (key == -1) {
			last_key = -1;
		}

		// G³ówna logika dzia³ania
		if (auto_mode || spin_in_progress) {
			spin_step();
			_delay_ms(5);
		}
		else if (just_stopped_auto) {
			for (uint16_t i = 0; i < 250; i++) {
				display_number(digits[0], digits[1], digits[2]);
				_delay_ms(8);
			}
			just_stopped_auto = false;
		}
		else {
			display_number(digits[0], digits[1], digits[2]);
		}
	}
}
