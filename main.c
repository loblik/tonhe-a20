#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/sfr_defs.h>
#include <util/delay.h>

#define UART_BAUD 38400
#define MYUBRR F_CPU/16/BAUD-1

//#define UART_DEBUG 1

int uart_putchar(char c, FILE *stream);

FILE uart_str = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
int pin_change_int;
int timer0_int;

enum seg {
    BOTTOM_LEFT = 1,
    BOTTOM_RIGHT = 2,
    BOTTOM = 4,
    MIDDLE = 8,
    UPPER_LEFT = 16,
    UPPER = 32,
    UPPER_RIGHT = 64,
    DOT = 128,
};

int disp_numbers[] = {
/* 0 */ BOTTOM_LEFT | BOTTOM | BOTTOM_RIGHT | UPPER_LEFT | UPPER_RIGHT | UPPER,
/* 1 */ BOTTOM_RIGHT | UPPER_RIGHT,
/* 2 */ BOTTOM_LEFT | BOTTOM | UPPER_RIGHT | UPPER | MIDDLE,
/* 3 */ BOTTOM | BOTTOM_RIGHT | UPPER_RIGHT | UPPER | MIDDLE,
/* 4 */ BOTTOM_RIGHT | UPPER_RIGHT | MIDDLE | UPPER_LEFT,
/* 5 */ BOTTOM | BOTTOM_RIGHT | MIDDLE | UPPER_LEFT | UPPER,
/* 6 */ BOTTOM_LEFT | BOTTOM | BOTTOM_RIGHT | UPPER_LEFT | MIDDLE | UPPER,
/* 7 */ BOTTOM_RIGHT | UPPER_RIGHT | UPPER,
/* 8 */ BOTTOM_LEFT | BOTTOM | BOTTOM_RIGHT | MIDDLE | UPPER_LEFT | UPPER | UPPER_RIGHT,
/* 9 */ BOTTOM | BOTTOM_RIGHT | MIDDLE | UPPER_LEFT | UPPER | UPPER_RIGHT,
};

int disp_led_pads[] = { PD2, PD6, PD1, PD7, PD4, PD3, PD5, PD0 };
int disp_drive_pads[] = { PC1, PC0, PC3, PC2 };

struct display {
    int current_digit;
    char buffer[4];
};

enum keyname { KEY_SET = 0, KEY_RIGHT, KEY_UP };
enum keystate { KEY_STATE_UP_HANDLED = 0, KEY_STATE_DOWN, KEY_STATE_UP };

struct key {
    int state;
    int count;
    int limit;
};

struct display disp;
struct key button[3];


int a, b, c;

void display_init() {
    disp.current_digit = 0;
    memset(disp.buffer, 0, 4);
}

void draw_digit(char symbol) {

    char segments;

    PORTD = 0;

    if (symbol >= '0' && symbol <= '9')
        segments = disp_numbers[symbol - '0'];
    else
        return;

    for (int i = 0; i < 8; i ++)
    {
        if (segments & _BV(i))
        {
#ifdef UART_DEBUG
            if (disp_led_pads[i] == PD1)
                continue;
#endif

            PORTD |= _BV(disp_led_pads[i]);
            DDRD |= _BV(disp_led_pads[i]);
        }
    }
}

void draw_display() {

    PORTC = 0;

    draw_digit(disp.buffer[disp.current_digit]);

    PORTC |= _BV(disp_drive_pads[disp.current_digit]);

    disp.current_digit++;
    disp.current_digit %= 4;
}

void key_init() {
    for (int i = 0; i < 3; i++)
    {
        button[i].count = 0;
        button[i].limit = 100;
        button[i].state = KEY_STATE_UP_HANDLED;
    }
}

void key_handle(char key, int event) {
#ifdef UART_DEBUG
    printf("pressed %d tint %d\n", key, timer0_int);
#endif
    //key_dump();
    if (event == KEY_STATE_DOWN)
    {
        switch (key) {
            case KEY_SET:
                a++;
                a %= 10;
                break;
            case KEY_UP:
                c++;
                c %= 10;
                break;
            case KEY_RIGHT:
                b++;
                b %= 10;
                break;

        }
    }
};

void key_poll() {

    char pind_bak = PIND;
    /* button pins as input */
    DDRD &= ~(_BV(PD2) | _BV(PD6) | _BV(PD7));

    _delay_us(200);

    char pind_val = PIND;

    /* button pins as output */
    DDRD |= _BV(PD2) | _BV(PD6) | _BV(PD7);

    PIND= pind_bak;


    char pads[] = { PD2, PD6, PD7 };

    for (int i = 0; i < 3; i++)
    {
        if (!(pind_val & _BV(pads[i])))
        {
            if (button[i].state == KEY_STATE_DOWN)
            {
                button[i].count++;
            }
            else
            {
                button[i].state = KEY_STATE_DOWN;
                button[i].count = 0;
            }
        } else {
            /* not pressed */
            if (button[i].state == KEY_STATE_UP)
            {
                button[i].count++;
            }
            else if (button[i].state == KEY_STATE_UP_HANDLED)
            {
                /* nothing */
            }
            else
            {
                button[i].state = KEY_STATE_UP;
                button[i].count = 0;
            }
        }

        if (button[i].count == button[i].limit)
        {
            key_handle(i, button[i].state);
            if (button[i].state == KEY_STATE_UP)
                button[i].state = KEY_STATE_UP_HANDLED;

            button[i].count = 0;
        }
    }
}

void key_dump() {
    for (int i = 0; i < 3; i++)
    {
        printf("count %d\n", button[i].count);
        printf("limit %d\n", button[i].limit);
        printf("state %d\n", button[i].state);
        printf("\n");
    }
}

void pinchange_interrupt_enable() {

}

void pinchange_interrupt_disable() {

}

void enable_display() {
}

ISR(PCINT2_vect)
{
    pin_change_int++;
}

ISR(TIMER2_OVF_vect)
{
    timer0_int++;

    if (timer0_int == 1000)
    {
        //printf("timer\n");

        timer0_int = 0;
    }

    //draw_display();
	//PORTD ^= (1 << LED);
	//TCNT0 = 63974;   // for 1 sec at 16 MHz
}

void
uart_init(void)
{
#if F_CPU < 2000000UL && defined(U2X)
  UCSR0A = _BV(U2X);             /* improve baud rate error by using 2x clk */
  UBRR0L = (F_CPU / (8UL * UART_BAUD)) - 1;
#else
  UBRR0L = (F_CPU / (16UL * UART_BAUD)) - 1;
#endif
}

int
uart_putchar(char c, FILE *stream)
{
  if (c == '\n')
    uart_putchar('\r', stream);

  /* enable transmitter */
  UCSR0B = _BV(TXEN0);

  loop_until_bit_is_set(UCSR0A, UDRE0);
  UDR0 = c;

  while (!(UCSR0A & (1 << TXC0)));

  UCSR0A |= _BV(TXC0);

  UCSR0B &= ~_BV(TXEN0);

  return 0;
}

int init() {
    uart_init();
    key_init();
    return 0;
}

int main() {

    init();


    TCCR2A = 0x00;
	TCCR2B = (1<<CS20) | (1<<CS22);;  // Timer mode with 1024 prescler
    TIFR2  = _BV(TOV2);
	TIMSK2 = (1 << TOIE2) ;   // Enable timer1 overflow interrupt(TOIE1)

    stdout = &uart_str;

    DDRC |= _BV(PC1) | _BV(PC0) | _BV(PC3) | _BV(PC2);
    //PORTC |= _BV(PC1);

//    for (int i = 0; i < 10; i++)
//    {
//        _delay_ms(1000);
//        draw_digit(i);
//    }
//    _delay_ms(1000);


    /* enable interrupts for 16-23 */
    //PCICR |= _BV(PCIE2);
    //PCMSK2 |= _BV(PCINT18) | _BV(PCINT22) | _BV(PCINT23);


//    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sei();

    printf("Initialized\n");

    while (1) {
    //printf("%d %d\n", pin_change_int, timer0_int);
//    sleep_mode();

    char buffer[5];
    snprintf(buffer, 5, "0%d%d%d", a, b, c);
    strncpy(disp.buffer, buffer, 4);
    _delay_us(50);
    key_poll();
    //_delay_ms();
    draw_display();
    //    sleep_mode();
   }
}
