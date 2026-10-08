#include "base_tempo.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#if !defined(__AVR_ATmega2560__)
#error "Esta base de tempo exige ATmega2560."
#endif

namespace {
volatile uint16_t estourosTimer1 = 0;
constexpr uint8_t CLOCK_TIMER1 = _BV(CS12) | _BV(CS10);
}

// 65536 / 15625 = 4,194304 s por overflow. Sem chamadas de RTOS na ISR.
ISR(TIMER1_OVF_vect) {
  ++estourosTimer1;
}

void iniciarBaseTempo() {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1B = 0;
    TIMSK1 = 0;
    TCCR1A = 0; // Modo normal; OC1 desconectado dos pinos.
    TCNT1 = 0;
    estourosTimer1 = 0;
    TIFR1 = _BV(TOV1) | _BV(OCF1A) | _BV(OCF1B) | _BV(OCF1C) | _BV(ICF1);
    TIMSK1 = _BV(TOIE1);
    TCCR1B = CLOCK_TIMER1;
  }
}

uint32_t lerBaseTempo() {
  uint16_t alto, baixo;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    alto = estourosTimer1;
    baixo = TCNT1;
    // O hardware pode ter virado antes da ISR. Corrige apenas esta copia
    // e rele o contador apos o overflow; a ISR atualizara o alto global.
    if (TIFR1 & _BV(TOV1)) {
      ++alto;
      baixo = TCNT1;
    }
  }
  // Wrap de 32 bits em 3,18 dias. Consumidor usa subtracao unsigned.
  // Nao bloquear interrupcoes por uma volta de hardware (4,19 s).
  return ((uint32_t)alto << 16) | baixo;
}

bool baseTempoIntegra() {
  // Detecta reconfiguracao; nao mede frequencia nem prova ausencia de pulsos perdidos.
  bool ok;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ok = TCCR1A == 0 && TCCR1B == CLOCK_TIMER1 && TIMSK1 == _BV(TOIE1);
  }
  return ok;
}
