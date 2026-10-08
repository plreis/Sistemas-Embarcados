volatile uint32_t ticks100Hz = 0;

ISR(TIMER1_COMPA_vect) {
  ++ticks100Hz;
}

uint32_t lerTempo() {
  uint32_t copia;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    copia = ticks100Hz;
  }
  return copia;
}
