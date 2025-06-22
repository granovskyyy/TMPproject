#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <stdlib.h>

// --- KONFIGURACJA SPRZÊTOWA ---
#define LED_WIN_PORT    PORTB
#define LED_WIN_PIN     PB5
#define LED_LOSE_PORT   PORTC
#define LED_LOSE_PIN    PC4
#define LED_AUTO_PORT   PORTC
#define LED_AUTO_PIN    PC5

#define SEGMENT_PORT    PORTD
#define SEGMENT_DDR     DDRD

#define DIGIT_PORT      PORTC
#define DIGIT_DDR       DDRC

#define KEY_ROW_PORT    PORTB
#define KEY_ROW_PIN     PINB
#define KEY_ROW_DDR     DDRB
#define KEY_COL_PORT    PORTE
#define KEY_COL_PIN     PINE
#define KEY_COL_DDR     DDRE

#define KEY_NEW_GAME          1
#define KEY_EDIT_MULTIPLIER   2
#define KEY_END_GAME          3
#define KEY_QUICK_SPIN        4
#define KEY_CHANGE_MODE       5
#define KEY_BACK              6
#define KEY_SPIN_MULT_X1_5    9
#define KEY_ALL_IN_MULT_X2    10
#define KEY_ZERO_BET          11
#define KEY_RESET_MULTIPLIER  12
#define KEY_BET_P1_1          13
#define KEY_BET_P1_2          14
#define KEY_BET_P1_3          15
#define KEY_BET_P1_4          16

const uint8_t digit_to_segment[11] = {
	~0b00111111, ~0b00000110, ~0b01011011, ~0b01001111, ~0b01100110,
	~0b01101101, ~0b01111101, ~0b00000111, ~0b01111111, ~0b01101111, ~0b01000000
};

volatile uint8_t display_digits[4] = {0, 0, 0, 0};
volatile uint8_t current_digit = 0;

volatile uint16_t balance = 1000;
volatile uint8_t bet = 1;
volatile uint8_t multiplier = 1;
volatile bool auto_mode = false;
volatile bool spinning = false;
volatile uint16_t spin_counter = 0;

typedef struct {
	uint8_t current_pos;
	uint8_t target_pos;
	uint8_t speed;
	uint8_t delay;
	bool accelerating;
	bool decelerating;
} WheelState;

volatile WheelState wheels[3];
volatile uint8_t final_results[3];

static uint16_t lfsr = 0xACE1u;
static uint8_t bit;

uint8_t rand_improved() {
	bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
	return lfsr = (lfsr >> 1) | (bit << 15);
}

void init_random() {
	ADMUX = (1 << REFS0);
	ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	while(ADCSRA & (1 << ADSC));
	lfsr = ADC;
	ADCSRA = 0;
	for(uint8_t i=0; i<20; i++) rand_improved();
}

void init_hardware() {
	DDRB |= (1 << LED_WIN_PIN);
	DDRC |= (1 << LED_LOSE_PIN) | (1 << LED_AUTO_PIN);
	LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
	LED_AUTO_PORT &= ~(1 << LED_AUTO_PIN);

	SEGMENT_DDR = 0xFF;
	SEGMENT_PORT = 0x00;
	DIGIT_DDR |= (1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3);
	DIGIT_PORT |= (1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3);

	KEY_ROW_DDR &= ~((1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB3));
	KEY_ROW_PORT |= ((1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB3));
	KEY_COL_DDR |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
	KEY_COL_PORT |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
}

void init_timer_display() {
	TCCR0A |= (1 << WGM01);
	TCCR0B |= (1 << CS01) | (1 << CS00);
	OCR0A = 124;
	TIMSK0 |= (1 << OCIE0A);
}

uint8_t scan_keypad() {
	for (uint8_t col = 0; col < 4; col++) {
		KEY_COL_DDR &= ~((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
		KEY_COL_PORT |= ((1<<PE0) | (1<<PE1) | (1<<PE2) | (1<<PE3));
		KEY_COL_DDR |= (1 << (PE0 + col));
		KEY_COL_PORT &= ~(1 << (PE0 + col));
		_delay_us(1);
		for (uint8_t row = 0; row < 4; row++) {
			if (!(KEY_ROW_PIN & (1 << (PB0 + row)))) {
				return (row * 4) + col + 1;
			}
		}
	}
	return 0;
}

void update_display() {
	display_digits[0] = wheels[0].current_pos;
	display_digits[1] = wheels[1].current_pos;
	display_digits[2] = wheels[2].current_pos;
	display_digits[3] = 0;
}

void handle_buttons() {
	static uint8_t last_key = 0;
	uint8_t key = scan_keypad();
	if (key != last_key && key != 0) {
		switch (key) {
			case KEY_NEW_GAME:
			balance = 1000; bet = 1; multiplier = 1; auto_mode = false;
			LED_AUTO_PORT &= ~(1 << LED_AUTO_PIN);
			break;
			case KEY_EDIT_MULTIPLIER:
			case KEY_BET_P1_2:
			multiplier = (multiplier % 5) + 1;
			break;
			case KEY_QUICK_SPIN:
			if (!spinning && balance >= bet) start_spin();
			break;
			case KEY_CHANGE_MODE:
			auto_mode = !auto_mode;
			if (auto_mode) LED_AUTO_PORT |= (1 << LED_AUTO_PIN);
			else LED_AUTO_PORT &= ~(1 << LED_AUTO_PIN);
			break;
			case KEY_BACK:
			if (bet > 1) bet--;
			break;
			case KEY_SPIN_MULT_X1_5:
			multiplier = 2;
			break;
			case KEY_ALL_IN_MULT_X2:
			bet = balance;
			multiplier = 2;
			break;
			case KEY_ZERO_BET:
			bet = 0;
			break;
			case KEY_RESET_MULTIPLIER:
			multiplier = 1;
			break;
			case KEY_BET_P1_1:
			bet = (bet % 9) + 1;
			break;
			case KEY_BET_P1_3:
			bet = 5;
			break;
			case KEY_BET_P1_4:
			bet = 10;
			break;
		}
	}
	last_key = key;
}

void update_wheels() {
	for(uint8_t i=0; i<3; i++) {
		if(wheels[i].delay++ >= wheels[i].speed) {
			wheels[i].delay = 0;
			wheels[i].current_pos = (wheels[i].current_pos + 1) % 10;
			if(wheels[i].accelerating) {
				if(wheels[i].speed > 1) wheels[i].speed--;
				if(wheels[i].speed <= 1) {
					wheels[i].accelerating = false;
					wheels[i].decelerating = true;
				}
				} else if(wheels[i].decelerating) {
				static uint8_t decel_counter = 0;
				if(decel_counter++ > 20 + (i*5)) {
					wheels[i].speed++;
					decel_counter = 0;
				}
				if(wheels[i].speed >= 15) {
					wheels[i].decelerating = false;
					wheels[i].current_pos = final_results[i];
					if(i == 2) spinning = false;
				}
			}
		}
	}
}

void init_wheels() {
	for(uint8_t i=0; i<3; i++) {
		wheels[i].current_pos = rand_improved() % 10;
		wheels[i].speed = 3 + i;
		wheels[i].delay = 0;
		wheels[i].accelerating = true;
		wheels[i].decelerating = false;
	}
}

void determine_results() {
	uint8_t pattern = rand_improved() % 100;
	if(pattern < 15) {
		uint8_t num = rand_improved() % 10;
		for(uint8_t i=0; i<3; i++) final_results[i] = num;
	}
	else if(pattern < 45) {
		uint8_t num = rand_improved() % 10;
		uint8_t pos = rand_improved() % 3;
		final_results[pos] = num;
		final_results[(pos+1)%3] = num;
		final_results[(pos+2)%3] = (num + 1 + rand_improved()%8) % 10;
	}
	else {
		for(uint8_t i=0; i<3; i++)
		final_results[i] = rand_improved() % 10;
	}
}

void check_win_condition() {
	bool win = false;

	// Domyœlnie: wszystkie diody OFF
	LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
	LED_AUTO_PORT &= ~(1 << LED_AUTO_PIN);

	// Warunek wygranej: 3 takie same
	if (final_results[0] == final_results[1] == final_results[2]) {
		win = true;
		balance += bet * multiplier * 5;

		// Wszystkie diody ON
		LED_WIN_PORT |= (1 << LED_WIN_PIN);
		LED_LOSE_PORT |= (1 << LED_LOSE_PIN);
		LED_AUTO_PORT |= (1 << LED_AUTO_PIN);
		return;
	}

	// Przegrana: saldo siê skoñczy³o
	if (balance == 0) {
		// Skrajne diody ON
		LED_WIN_PORT |= (1 << LED_WIN_PIN);
		LED_AUTO_PORT |= (1 << LED_AUTO_PIN);
		} else {
		// Normalna przegrana: tylko œrodkowa dioda ON
		LED_LOSE_PORT |= (1 << LED_LOSE_PIN);
	}
}



void start_spin() {
	if (spinning || balance < bet) return;

	balance -= bet;

	LED_WIN_PORT &= ~(1 << LED_WIN_PIN);
	LED_LOSE_PORT &= ~(1 << LED_LOSE_PIN);
	LED_AUTO_PORT &= ~(1 << LED_AUTO_PIN);

	determine_results();
	init_wheels();
	spinning = true;

	spin_counter = 80; 
}


ISR(TIMER0_COMPA_vect) {
	DIGIT_PORT |= (1 << PC0) | (1 << PC1) | (1 << PC2) | (1 << PC3);
	SEGMENT_PORT = digit_to_segment[display_digits[current_digit]];
	DIGIT_PORT &= ~(1 << (PC0 + current_digit));
	current_digit = (current_digit + 1) % 4;
	static uint8_t anim_counter = 0;
	if (++anim_counter >= 4) {
	anim_counter = 0;
	if (spinning) {
		update_wheels();
		update_display(); 
		} 
		else if (spin_counter > 0) {
		if (--spin_counter == 0) {
			check_win_condition();
			update_display(); 
			if (auto_mode && balance >= bet) {
				start_spin();
			}
		}
	}
}

}

int main(void) {
	init_random();
	init_hardware();
	init_timer_display();
	sei();
	update_display();
	while(1) {
		handle_buttons();
		if(!spinning && spin_counter == 0) {
			update_display();
		}
		_delay_ms(10);
	}
}
