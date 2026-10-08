#include "tempo_avr.h"
#include <Arduino.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#if !defined(__AVR_ATmega2560__)
#error "Selecione Arduino Mega 2560."
#endif
static_assert(F_CPU == 16000000UL, "Timer calculado para 16 MHz.");
namespace {
volatile uint32_t ticks100Hz = 0;
}
ISR(TIMER1_COMPA_vect) {
  ++ticks100Hz;
}
void iniciarTempo() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1B = 0;
    TIMSK1 = 0;
    TCCR1A = 0;
    TCNT1 = 0;
    ticks100Hz = 0;
    OCR1A = 2499;
    TIFR1 = _BV(OCF1A);
    TIMSK1 = _BV(OCIE1A);
    TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);
  }
}
uint32_t lerTempo() {
  uint32_t copia;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    copia = ticks100Hz;
  }
  return copia;
}
bool tempoConfigurado() {
  bool ok;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ok = TCCR1A == 0 && TCCR1B == (_BV(WGM12) | _BV(CS11) | _BV(CS10)) && OCR1A == 2499 &&
         TIMSK1 == _BV(OCIE1A);
  }
  return ok;
}
