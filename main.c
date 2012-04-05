/*
 * main.c
 * CodeLock
 *
 * Created on 05/04/2012 by Árpád Goretity
 * Released into the public domain
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <avr/eeprom.h>
#include <avrutil/avrutil.h>

#define MAX_WRONG_TRIES 3
#define LOCKDOWN_WAIT 300 /* 5 minutes */
#define WAIT_TIMER ((uint16_t *)0x00)
#define WRONG_TRIES ((uint8_t *)0x20)
#define RESET_DEVICE 0

static char *chars[] = { /* The keyboard layout */
	"123A",
	"456B",
	"789C",
	"*0#D"
};

static char code[] = { '1', '3', '3', '7' }; /* Change this */

static uint8_t cursor = 0;

uint8_t scan_kb_matrix(volatile uint8_t *out_row, volatile uint8_t *out_col)
{
	for (uint8_t row = 0; row < 4; row++)
	{
		PORTD |= B00001111;
		avr_bit_clear(PORTD, row);
		
		/* 
		 * Here we would wait at least 10 microseconds
		 * in order the pins/wires/capacitances/etc. to let the current trough themselves
		 * without this wait, the 0th column of the keyboard never gets detected
		 *
		 * Furthermore, we want to debounce the results, so we wait even more: 2 ms
		 */
		_delay_ms(2);
		
		/* 
		 * The low nibble of PORTD serves as output (keyboard rows),
		 * pulling the correspinding bits in the high nibble (keyboard columns)
		 * when the appropriate button in the matrix is pressed.
		 */
		for (uint8_t col = 0; col < 4; col++)
		{
			if (avr_bit_isclear(PIND, col + 4))
			{
				loop_until_bit_is_set(PIND, col + 4);
				*out_row = row;
				*out_col = col;
				return 1; /* means we found a pressed key */
			}
		}
	}
	return 0; /* means that no key was pressed */
}

void wait_for_passcode()
{
	cursor = 0;
	avr_lcd_clear();
	avr_lcd_puts("Enter passcode:");
	avr_lcd_set_cursor_pos(AVR_LCD_LINE1 + 0);
}

/* 
 * If there were MAX_WRONG_TRIES unsuccessful attempts to type the code,
 * block the execution for LOCKDOWN_WAIT seconds.
 */
void lock_device()
{
	if (!eeprom_read_word(WAIT_TIMER))
	{
		return;
	}
	
	avr_lcd_clear();
	avr_lcd_puts("Device locked");
	
	char buf[16];
	for (uint16_t i = eeprom_read_word(WAIT_TIMER); i > 0; i--)
	{
		uint8_t min = i / 60;
		uint8_t sec = i % 60;
		
		snprintf(buf, 16, "Wait %02d m %02d s", min, sec);
		avr_lcd_set_cursor_pos(AVR_LCD_LINE1 + 0);
		avr_lcd_puts(buf);
		_delay_ms(1000);
		
		eeprom_write_word(WAIT_TIMER, i);
	}
	
	eeprom_write_word(WAIT_TIMER, 0);
	eeprom_write_byte(WRONG_TRIES, 0);
}

void deny_access()
{
	/* 
	 * Store the failed attemps in the EEPROM
	 * in order to prevent circumventing the security by unplugging the
	 * AVR's supply wires...
	 * (Same applies to the remaining wait time, see later.)
	 */
	eeprom_write_byte(WRONG_TRIES, eeprom_read_byte(WRONG_TRIES) + 1);
	
	avr_lcd_clear();
	avr_lcd_puts("Access denied!");
	
	char buf[16];
	snprintf(buf, 16, "%d of %d tries", eeprom_read_byte(WRONG_TRIES), MAX_WRONG_TRIES);
	avr_lcd_set_cursor_pos(AVR_LCD_LINE1 + 0);
	avr_lcd_puts(buf);

	_delay_ms(2000);

	if (eeprom_read_byte(WRONG_TRIES) >= MAX_WRONG_TRIES)
	{
		eeprom_write_word(WAIT_TIMER, LOCKDOWN_WAIT);
		lock_device();
	}	
}

void grant_access()
{
	avr_lcd_clear();
	avr_lcd_puts("Access granted!");
	/* Generate a 2s long HIGH pulse on port C, bit 0 */
	PORTC = B00000001;
	_delay_ms(2000);
	PORTC = B00000000;
	
	/* Clear the wrong attempt counter */
	eeprom_write_byte(WRONG_TRIES, 0);
}

#if RESET_DEVICE

/* 
 * If this program is run,
 * it resets the device from the locked state.
 */

int main()
{
	/*
	 * Clear attempt and wait counters
	 */
	
	eeprom_write_word(WAIT_TIMER, 0);
	eeprom_write_byte(WRONG_TRIES, 0);
	avr_lcd_init();

	while (1) /* And hang... */
	{
		avr_lcd_clear();
		_delay_ms(500);
		avr_lcd_puts("Device reset");
		avr_lcd_set_cursor_pos(AVR_LCD_LINE1 + 0);
		avr_lcd_puts("Reprogram AVR");
		_delay_ms(500);
	}
	
	return 0;
}

#else

int main()
{
	/* Initialize the peripheria */
	avr_lcd_init();

	DDRD =  B00001111;
	PORTD = B11110000;
	
	DDRC =  B00000001;
	PORTC = B00000000;
	
	/* 
	 * Check if there was some trickery. lock_device() only locks actually
	 * if there's a number of remaining wait time seconds greater than 0.
	 */
	lock_device();
	
	/* 
	 * Accumulate the user's keystrokes in this buffer
	 */
	char keystrokes[sizeof(code)];
	
	wait_for_passcode();
	
	while (1)
	{
		uint8_t row, col;
		if (scan_kb_matrix(&row, &col))
		{
			char c = chars[row][col];
			
			if (c == 'C') /* Clear/Cancel/Correct */
			{
				wait_for_passcode();
			}
			else if (c == '#') /* Enter/OK */
			{
				/* Memcmp returns zero upon equality */
				if (memcmp(code, keystrokes, cursor) || cursor != sizeof(code))
				{
					deny_access();
				}
				else
				{
					grant_access();
				}

				wait_for_passcode();
			}
			else
			{
				keystrokes[cursor++] = c;
				avr_lcd_putc('*');
			}
		}
	}
	
	return 0;
}

#endif

