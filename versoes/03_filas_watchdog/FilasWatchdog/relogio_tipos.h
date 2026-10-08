#pragma once
#include <Arduino_FreeRTOS.h>

/* Tipos em header para o gerador de prototipos da Arduino IDE. */
enum class Evento : uint8_t {
  SEL_CURTO, SEL_LONGO, SONECA, ESQUERDA, DIREITA, CIMA, BAIXO
};

struct Configuracao {
  uint8_t hora, minuto, dia, mes;
  uint16_t ano;
  uint8_t alarmeHora, alarmeMinuto, sonecaMinutos;
};

/* Somente TaskRelogio modifica estas estruturas. */
struct EstadoRelogio {
  Configuracao cfg;
  uint8_t segundo;
  bool alarmeLigado;
  uint32_t ultimoDisparo;
  uint32_t sonecaTicks, toqueTicks, avisoTicks;
};

struct Edicao {
  Configuracao rascunho;
  bool ativa, mudouHora, mudouData;
  uint8_t campo;
};

/* Copia por valor enviada ao LCD; nenhum ponteiro para estado compartilhado. */
struct Tela {
  Configuracao cfg;
  uint8_t segundo, campo;
  bool alarmeLigado, editando, tocando, emSoneca, avisoEntrada;
  uint16_t sonecaSegundos;
};

struct BotaoDebounce {
  bool leituraAnterior, estavel, longoEnviado;
  TickType_t mudouEm, pressionouEm;
};

struct EixoJoystick {
  int8_t anterior;
  TickType_t repetiuEm;
};
