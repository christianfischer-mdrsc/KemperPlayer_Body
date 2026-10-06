# Host-Test: kemper_link.c + Datenmodell gegen einen nachgebildeten Kemper Player.
# Aufruf:  make        (braucht nur gcc, laeuft auf dem PC)
CM7   := ../../CM7
BUILD := build

all: $(BUILD)/sim
	./$(BUILD)/sim

# kemper_link.c wird neben die Stubs kopiert, damit seine #include "..." die
# Stubs finden statt der echten USB-/HAL-Header
$(BUILD)/kemper_link.c: $(CM7)/USB/kemper_link.c
	mkdir -p $(BUILD) && cp $< $@ && cp stub/*.h $(CM7)/USB/kemper_link.h $(BUILD)/

$(BUILD)/sim: sim.c $(BUILD)/kemper_link.c $(CM7)/UI/kemper_player.c $(CM7)/UI/kemper_player.h
	gcc -std=gnu11 -g -Wall -Wno-unused-function -Wno-unused-variable \
	    -I$(BUILD) -I$(CM7)/UI -o $@ sim.c $(CM7)/UI/kemper_player.c

clean:
	rm -rf $(BUILD)

.PHONY: all clean
