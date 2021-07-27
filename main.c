#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/sfr_defs.h>
#include <util/delay.h>
#include <limits.h>

#define UART_BAUD 38400
#define MYUBRR F_CPU/16/BAUD-1

#define KEY_POLL_LIMIT          4
#define KEY_POLL_LIMIT_HOLD    40
#define KEY_POLL_LIMIT_REPEAT   4

#define ISL1208_ADDRESS   0x6F

#include "i2c.h"

#define UART_DEBUG 1

int uart_putchar(char c, FILE *stream);

FILE uart_str = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
int pin_change_int;


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
    char visible[4];
    char dots;
};

enum keyname { KEY_SET = 0, KEY_RIGHT, KEY_UP };
enum keystate { KEY_STATE_DOWN, KEY_STATE_UP, KEY_STATE_UP_HANDLED, KEY_STATE_DOWN_REPEAT, KEY_STATE_DOWN_HOLD };

struct key {
    int state;
    int count;
    int limit;
};

struct display disp;
struct key button[3];

struct timer *timers[3];
unsigned int ticks;

struct timer pwrdown_timer;
struct timer keypoll_timer;
struct timer blink_timer;

struct time {
    char sec;
    char min;
    char hour;
} __attribute__((packed));

struct timer {
    int timeout;
    void (*handler)(void);
};

void display_init() {
    disp.current_digit = 0;
    disp.dots = 0;
    memset(disp.buffer, 0, 4);
    memset(disp.visible, 1, 4);
}

char bcd_to_dec(char bcd)
{
    return (bcd >> 4)*10 + (bcd & 0x0f);
}

void time_bcd_to_dec(struct time *t) {
    t->sec = bcd_to_dec(t->sec);
    t->min = bcd_to_dec(t->min);
    t->hour = bcd_to_dec(t->hour & 0x7f);
}

void timer_init()
{
    timers[0] = &pwrdown_timer;
    timers[1] = &keypoll_timer;
    timers[2] = &blink_timer;

    for (int i = 0; i < sizeof(timers)/sizeof(timers[0]); i++)
    {
        timers[i]->handler = NULL;
        timers[i]->timeout = INT_MAX;
    }
}

void timer_poll()
{
    static unsigned int last_ticks;

    if (ticks == last_ticks)
        return;

    last_ticks = ticks;

    for (int i = 0; i < sizeof(timers)/sizeof(timers[0]); i++)
    {
        if (timers[i]->timeout == INT_MAX) {
            continue;
        }

        timers[i]->timeout -= 25;

        if (timers[i]->timeout <= 0)
        {
            timers[i]->timeout = INT_MAX;
            timers[i]->handler();
        }
    }
}

void timer_set(struct timer *t, int timeout, void (*handler)())
{
    t->handler = handler;
    t->timeout = timeout;
}

void draw_digit(char symbol, char dot) {

    char segments;

    if (symbol >= '0' && symbol <= '9')
        segments = disp_numbers[symbol - '0'];
    else
        return;

    if (dot)
        segments |= DOT;

    for (int i = 0; i < 8; i ++)
    {
        if (segments & _BV(i))
        {
#ifdef UART_DEBUG
            if (disp_led_pads[i] == PD1)
                continue;
#endif

            DDRD |= _BV(disp_led_pads[i]);
            PORTD |= _BV(disp_led_pads[i]);
        }
    }
}

void draw_display() {

    PORTC &= ~(_BV(PC1) | _BV(PC0) | _BV(PC3) | _BV(PC2));
    PORTD = 0;

    if (disp.visible[disp.current_digit]) {
        draw_digit(disp.buffer[disp.current_digit], disp.dots && disp.current_digit == 1);
        PORTC |= _BV(disp_drive_pads[disp.current_digit]);
    }

    disp.current_digit++;
    disp.current_digit %= 4;
}

void blink_handler() {
    disp.visible[0] = !disp.visible[0];
    disp.visible[1] = !disp.visible[1];
    timer_set(&blink_timer, 500, blink_handler);
}

void key_init() {
    for (int i = 0; i < 3; i++)
    {
        button[i].count = 0;
        button[i].limit = KEY_POLL_LIMIT;
        button[i].state = KEY_STATE_UP_HANDLED;
    }
}

void key_handle(char key, int event) {
#ifdef UART_DEBUG
    printf("pressed %d tick %u\n", key, ticks);
    //key_dump();
#endif
    if (event == KEY_STATE_DOWN)
    {
    }
};

void key_poll() {

    char pind_bak = PIND;
    /* button pins as input */
    DDRD &= ~(_BV(PD2) | _BV(PD6) | _BV(PD7));

    _delay_us(10);

    char pind_val = PIND;

    /* button pins as output */
    DDRD |= _BV(PD2) | _BV(PD6) | _BV(PD7);

    PIND= pind_bak;


    char pads[] = { PD2, PD6, PD7 };

    for (int i = 0; i < 3; i++)
    {
        unsigned char pressed = !(pind_val & _BV(pads[i]));
        struct key *btn = &button[i];

        switch (btn->state)
        {
            case KEY_STATE_DOWN_REPEAT:
            case KEY_STATE_DOWN_HOLD:
            case KEY_STATE_DOWN:
                if (pressed)
                    btn->count++;
                else {
                    btn->limit = KEY_POLL_LIMIT;
                    btn->state = KEY_STATE_UP;
                    btn->count = 0;
                }
                break;
            case KEY_STATE_UP:
                if (!pressed)
                    btn->count++;
                else
                    btn->limit = KEY_POLL_LIMIT;
                    btn->state = KEY_STATE_DOWN;
                    btn->count = 0;
                break;
            case KEY_STATE_UP_HANDLED:
                if (pressed) {
                    btn->state = KEY_STATE_DOWN;
                    btn->count++;
                }
                break;
        }

        if (button[i].count == button[i].limit)
        {
            key_handle(i, button[i].state);

            switch (btn->state)
            {
                case KEY_STATE_DOWN:
                    btn->state = KEY_STATE_DOWN_HOLD;
                    btn->limit = KEY_POLL_LIMIT_HOLD;
                    break;
                case KEY_STATE_DOWN_HOLD:
                    btn->state = KEY_STATE_DOWN_REPEAT;
                    btn->limit = KEY_POLL_LIMIT_REPEAT;
                    break;
                case KEY_STATE_DOWN_REPEAT:
                    break;
                case KEY_STATE_UP:
                    btn->state = KEY_STATE_UP_HANDLED;
                    break;
            }

            button[i].count = 0;
        }
    }

    /* schedule us again */
    timer_set(&keypoll_timer, 25, key_poll);
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

ISR(PCINT2_vect)
{
    pin_change_int++;
}

ISR(TIMER2_OVF_vect)
{
    static int timer2_count;
    timer2_count++;

    /* roughly 25ms */
    if (timer2_count == 6)
    {
        timer2_count = 0;
        ticks++;
    }
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
    display_init();
    i2c_init(400000);
    return 0;
}

int main() {

    init();


    TCCR2A = 0x00;
	TCCR2B = _BV(CS22) | _BV(CS20);  // timer mode with 128 prescaler
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

    disp.dots = 1;

    timer_init();

    timer_set(&keypoll_timer, 25, key_poll);
    timer_set(&blink_timer, 200, blink_handler);

    while (1) {

    //printf("%d %d\n", pin_change_int, timer0_int);
//    sleep_mode();

    char buffer[5];

    struct time rtc_now;

    i2c_read(ISL1208_ADDRESS, sizeof(rtc_now), 0, (uint8_t*)&rtc_now);

    time_bcd_to_dec(&rtc_now);

    snprintf(buffer, 5, "%02d%02d", rtc_now.hour, rtc_now.min);
    strncpy(disp.buffer, buffer, 4);

    timer_poll();

    //_delay_ms();
    draw_display();
    //    sleep_mode();

    _delay_us(50);
   }
}
