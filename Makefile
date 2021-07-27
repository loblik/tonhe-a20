DEVICE     = atmega168p
CLOCK      = 8000000
PROGRAMMER = avrisp2
PORT	   = /dev/ttyUSB1
BAUD       = 19200
FILENAME   = main
COMPILE    = avr-gcc -Wall -Os -DF_CPU=$(CLOCK) -mmcu=$(DEVICE)

all: build upload

build:
	$(COMPILE) -c $(FILENAME).c -o $(FILENAME).o
	$(COMPILE) -c i2c.c
	$(COMPILE) -o $(FILENAME).elf $(FILENAME).o i2c.o
	avr-objcopy -j .text -j .data -O ihex $(FILENAME).elf $(FILENAME).hex
	avr-size --format=avr --mcu=$(DEVICE) $(FILENAME).elf

upload:
	avrdude -v -p $(DEVICE) -c $(PROGRAMMER) -P $(PORT) -U flash:w:$(FILENAME).hex:i

clean:
	rm -rf main.o
	rm -rf main.elf
	rm -rf main.hex
