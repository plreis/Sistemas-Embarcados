#pragma once
#include <Arduino.h>

// Timer1 normal, /1024: 15625 contagens/s e resolucao de 64 us a 16 MHz.
// Sem calibracao empirica: a frequencia fisica ainda deve ser medida.
constexpr uint32_t CONTAGENS_POR_SEGUNDO = F_CPU / 1024UL;
static_assert(F_CPU == 16000000UL, "Configuracao validada para Mega de 16 MHz.");

void iniciarBaseTempo();
uint32_t lerBaseTempo();
bool baseTempoIntegra();
