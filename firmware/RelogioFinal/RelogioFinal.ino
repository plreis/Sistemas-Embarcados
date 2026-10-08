// Relogio e alarme - exame de suficiencia, Sistemas Embarcados, UTFPR.
// Firmware consolidado da versao ZERO-CTC-1; comportamento preservado.
// Dependencias: FreeRTOS 11.1.0-3 e LiquidCrystal 1.0.7; placa Mega 2560.
#include "RelogioTipos.h"
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <LiquidCrystal.h>
#include <queue.h>
#include <string.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
void iniciarTempo();
uint32_t lerTempo();
bool tempoConfigurado();
void iniciarAplicacao();

// INICIO_NUCLEO

namespace relogio {
Estado estadoInicial();
Estado estadoInicial() {
  Estado e = {};
  e.agora = {2026, 10, 8, 7, 58, 30};
  e.alarme = {7, 0, 5, false};
  return e;
}
uint8_t diasNoMes(uint16_t ano, uint8_t mes);
uint8_t diasNoMes(uint16_t ano, uint8_t mes) {
  if (mes == 2)
    return (ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0)) ? 29 : 28;
  return (mes == 4 || mes == 6 || mes == 9 || mes == 11) ? 30 : 31;
}
static void segundoSeguinte(DataHora &d);
static void segundoSeguinte(DataHora &d) {
  if (++d.segundo < 60)
    return;
  d.segundo = 0;
  if (++d.minuto < 60)
    return;
  d.minuto = 0;
  if (++d.hora < 24)
    return;
  d.hora = 0;
  if (++d.dia <= diasNoMes(d.ano, d.mes))
    return;
  d.dia = 1;
  if (++d.mes <= 12)
    return;
  d.mes = 1;
  d.ano = d.ano == 2099 ? 2000 : d.ano + 1;
}
static uint32_t chave(const DataHora &d);
static uint32_t chave(const DataHora &d) {
  const uint32_t dia = (uint32_t)(d.ano - 2000) * 372UL + (d.mes - 1) * 31UL + d.dia - 1;
  return dia * 1440UL + d.hora * 60UL + d.minuto + 1;
}
static void avaliar(Estado &e);
static void avaliar(Estado &e) {
  if (!e.alarme.habilitado || e.agora.segundo != 0 || e.agora.hora != e.alarme.hora ||
      e.agora.minuto != e.alarme.minuto)
    return;
  uint32_t ocorrencia = chave(e.agora);
  if (ocorrencia == e.ultimaOcorrencia)
    return;
  e.ultimaOcorrencia = ocorrencia;
  if (e.aviso == Aviso::Silencio) {
    e.aviso = Aviso::Tocando;
    e.restante = 60UL * TICKS_SEGUNDO;
  }
}
static void consumirPrazo(Estado &e, uint32_t ticks);
static void consumirPrazo(Estado &e, uint32_t ticks) {
  if (e.aviso == Aviso::Silencio)
    return;
  if (ticks < e.restante) {
    e.restante -= ticks;
    return;
  }
  ticks -= e.restante;
  if (e.aviso == Aviso::Adiado && ticks < 60UL * TICKS_SEGUNDO) {
    e.aviso = Aviso::Tocando;
    e.restante = 60UL * TICKS_SEGUNDO - ticks;
  } else {
    e.aviso = Aviso::Silencio;
    e.restante = 0;
  }
}
void avancar(Estado &e, uint32_t ticks);
void avancar(Estado &e, uint32_t ticks) {
  // Prazos e calendario evoluem na mesma ordem temporal, inclusive na recuperacao.
  // Nao soma ticks+fracao em uint32_t: evita overflow dessa soma intermediaria.
  while (ticks) {
    uint32_t passo = TICKS_SEGUNDO - e.fracao;
    if (ticks < passo)
      passo = ticks;
    consumirPrazo(e, passo);
    e.fracao += passo;
    ticks -= passo;
    if (e.fracao == TICKS_SEGUNDO) {
      e.fracao = 0;
      segundoSeguinte(e.agora);
      avaliar(e);
    }
  }
}
static uint8_t circular(uint8_t x, int8_t delta, uint8_t min, uint8_t max);
static uint8_t circular(uint8_t x, int8_t delta, uint8_t min, uint8_t max) {
  return delta > 0 ? (x == max ? min : x + 1) : (x == min ? max : x - 1);
}
static void alterar(Edicao &r, int8_t delta);
static void alterar(Edicao &r, int8_t delta) {
  if (r.campo <= 2)
    r.mudouHora = true;
  if (r.campo >= 3 && r.campo <= 5)
    r.mudouData = true;
  switch (r.campo) {
  case 0:
    r.data.hora = circular(r.data.hora, delta, 0, 23);
    break;
  case 1:
    r.data.minuto = circular(r.data.minuto, delta, 0, 59);
    break;
  case 2:
    r.data.segundo = circular(r.data.segundo, delta, 0, 59);
    break;
  case 3:
    r.data.dia = circular(r.data.dia, delta, 1, diasNoMes(r.data.ano, r.data.mes));
    break;
  case 4:
    r.data.mes = circular(r.data.mes, delta, 1, 12);
    break;
  case 5:
    r.data.ano = delta > 0 ? (r.data.ano == 2099 ? 2000 : r.data.ano + 1)
                           : (r.data.ano == 2000 ? 2099 : r.data.ano - 1);
    break;
  case 6:
    r.alarme.hora = circular(r.alarme.hora, delta, 0, 23);
    break;
  case 7:
    r.alarme.minuto = circular(r.alarme.minuto, delta, 0, 59);
    break;
  case 8:
    r.alarme.sonecaMinutos = circular(r.alarme.sonecaMinutos, delta, 1, 10);
    break;
  }
  uint8_t limite = diasNoMes(r.data.ano, r.data.mes);
  if (r.data.dia > limite)
    r.data.dia = limite;
}
void comandar(Estado &e, Evento evento);
void comandar(Estado &e, Evento evento) {
  if (evento == Evento::Encerrar) {
    e.aviso = Aviso::Silencio;
    e.restante = 0;
    e.edicao.ativa = false;
    return;
  }
  if (e.aviso == Aviso::Tocando) {
    if (evento == Evento::Soneca || evento == Evento::Selecionar) {
      e.aviso = Aviso::Adiado;
      e.restante = (uint32_t)e.alarme.sonecaMinutos * 60UL * TICKS_SEGUNDO;
    }
    return;
  }
  if (e.aviso == Aviso::Adiado) {
    if (evento == Evento::Soneca) {
      e.aviso = Aviso::Silencio;
      e.restante = 0;
      e.alarme.habilitado = false;
    }
    return;
  }
  if (evento == Evento::Soneca) {
    if (e.edicao.ativa)
      e.edicao.ativa = false;
    else {
      e.alarme.habilitado = !e.alarme.habilitado;
      avaliar(e);
    }
    return;
  }
  if (evento == Evento::Selecionar) {
    if (!e.edicao.ativa) {
      e.edicao = {e.agora, e.alarme, 0, true, false, false};
    } else {
      const Edicao &r = e.edicao;
      if (r.mudouHora) {
        e.agora.hora = r.data.hora;
        e.agora.minuto = r.data.minuto;
        e.agora.segundo = r.data.segundo;
        e.fracao = 0;
        ++e.ajustesHora;
      }
      if (r.mudouData) {
        e.agora.ano = r.data.ano;
        e.agora.mes = r.data.mes;
        e.agora.dia = r.data.dia;
      }
      e.alarme.hora = r.alarme.hora;
      e.alarme.minuto = r.alarme.minuto;
      e.alarme.sonecaMinutos = r.alarme.sonecaMinutos;
      e.edicao.ativa = false;
      avaliar(e);
    }
    return;
  }
  if (!e.edicao.ativa)
    return;
  switch (evento) {
  case Evento::Direita:
    e.edicao.campo = (e.edicao.campo + 1) % CAMPOS;
    break;
  case Evento::Esquerda:
    e.edicao.campo = (e.edicao.campo + CAMPOS - 1) % CAMPOS;
    break;
  case Evento::Mais:
    alterar(e.edicao, 1);
    break;
  case Evento::Menos:
    alterar(e.edicao, -1);
    break;
  default:
    break;
  }
}
static void dois(char *linha, uint8_t col, uint8_t valor);
static void dois(char *linha, uint8_t col, uint8_t valor) {
  linha[col] = '0' + valor / 10;
  linha[col + 1] = '0' + valor % 10;
}
static void horario(char *l, uint8_t col, const DataHora &d);
static void horario(char *l, uint8_t col, const DataHora &d) {
  dois(l, col, d.hora);
  l[col + 2] = ':';
  dois(l, col + 3, d.minuto);
  l[col + 5] = ':';
  dois(l, col + 6, d.segundo);
}
static void data(char *l, const DataHora &d);
static void data(char *l, const DataHora &d) {
  dois(l, 0, d.dia);
  l[2] = '/';
  dois(l, 3, d.mes);
  l[5] = '/';
  dois(l, 6, d.ano / 100);
  dois(l, 8, d.ano % 100);
}
void renderizar(const Estado &e, char (&l0)[17], char (&l1)[17], int8_t &cursor);
void renderizar(const Estado &e, char (&l0)[17], char (&l1)[17], int8_t &cursor) {
  memset(l0, ' ', 16);
  memset(l1, ' ', 16);
  l0[16] = l1[16] = '\0';
  cursor = -1;
  if (e.aviso == Aviso::Tocando) {
    memcpy(l0, "ALARME TOCANDO!", 14);
    memcpy(l1, "D5/SEL: SONECA", 14);
    return;
  }
  if (e.aviso == Aviso::Adiado) {
    memcpy(l0, "SONECA", 6);
    uint16_t segundos = (e.restante + TICKS_SEGUNDO - 1) / TICKS_SEGUNDO;
    dois(l0, 8, segundos / 60);
    l0[10] = ':';
    dois(l0, 11, segundos % 60);
    memcpy(l1, "D5: DESLIGAR", 12);
    return;
  }
  if (!e.edicao.ativa) {
    horario(l0, 0, e.agora);
    memcpy(l0 + 9, "AL", 2);
    dois(l0, 11, e.alarme.hora);
    l0[13] = ':';
    dois(l0, 14, e.alarme.minuto);
    data(l1, e.agora);
    memcpy(l1 + 12, e.alarme.habilitado ? "ON " : "OFF", 3);
    return;
  }
  const Edicao &r = e.edicao;
  static const char *const nomes[] = {"HORA", "MIN",  "SEG",  "DIA",   "MES",
                                      "ANO",  "AL-H", "AL-M", "SONECA"};
  memcpy(l1, nomes[r.campo], strlen(nomes[r.campo]));
  memcpy(l1 + 7, "SEL:SALVA", 9);
  if (r.campo <= 2) {
    horario(l0, 0, r.data);
    memcpy(l0 + 10, "EDITAR", 6);
    cursor = r.campo * 3;
  } else if (r.campo <= 5) {
    data(l0, r.data);
    cursor = r.campo == 3 ? 0 : r.campo == 4 ? 3 : 6;
  } else if (r.campo <= 7) {
    memcpy(l0, "ALARME ", 7);
    dois(l0, 7, r.alarme.hora);
    l0[9] = ':';
    dois(l0, 10, r.alarme.minuto);
    cursor = r.campo == 6 ? 7 : 10;
  } else {
    memcpy(l0, "SONECA ", 7);
    dois(l0, 7, r.alarme.sonecaMinutos);
    memcpy(l0 + 10, "min", 3);
    cursor = 7;
  }
}
} // namespace relogio
// FIM_NUCLEO

// INICIO_TEMPO
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
void iniciarTempo();
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
uint32_t lerTempo();
uint32_t lerTempo() {
  uint32_t copia;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    copia = ticks100Hz;
  }
  return copia;
}
bool tempoConfigurado();
bool tempoConfigurado() {
  bool ok;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ok = TCCR1A == 0 && TCCR1B == (_BV(WGM12) | _BV(CS11) | _BV(CS10)) && OCR1A == 2499 &&
         TIMSK1 == _BV(OCIE1A);
  }
  return ok;
}
// FIM_TEMPO

// INICIO_APLICACAO

#ifndef portUSE_WDTO
#error "Este projeto reserva Timer1 para o relogio e exige tick RTOS no watchdog."
#endif
#ifndef DIAGNOSTICO_SERIAL
#define DIAGNOSTICO_SERIAL 1
#endif

namespace {
using namespace relogio;
constexpr uint8_t PIN_X = 54, PIN_Y = 55, PIN_SEL = 2, PIN_SONECA = 5;
constexpr uint8_t PIN_BUZZER = 8, PIN_LED = 9;
constexpr uint8_t FILA_ENTRADAS = 16;
LiquidCrystal lcd(22, 23, 24, 25, 26, 27);
QueueHandle_t entradas, saidaAlarme, saidaTela;
TaskHandle_t hRelogio, hBotoes, hAlarme, hDisplay;

struct Snapshot {
  Estado estado;
  uint32_t timer, milissegundos, maiorCiclo, descartes;
  bool timerOk;
};
TickType_t espera(uint16_t ms);
TickType_t espera(uint16_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  return ticks ? ticks : 1;
}
void enviar(Evento evento);
void enviar(Evento evento) {
  if (xQueueSend(entradas, &evento, 0) != pdPASS) {
    // Notificacao contadora: entrada descartada e visivel sem bloquear produtor.
    xTaskNotifyGive(hRelogio);
  }
}
int8_t direcao(int leitura);
int8_t direcao(int leitura) {
  return leitura < 200 ? -1 : leitura > 800 ? 1 : 0;
}
void tarefaBotoes(void *);
void tarefaBotoes(void *) {
  Botao sel, d5;
  Eixo x, y;
  TickType_t ultima = xTaskGetTickCount();
  for (;;) {
    uint32_t agora = lerTempo();
    uint8_t clique = sel.ler(digitalRead(PIN_SEL) == LOW, agora, true);
    if (clique)
      enviar(clique == 2 ? Evento::Encerrar : Evento::Selecionar);
    if (d5.ler(digitalRead(PIN_SONECA) == LOW, agora, false))
      enviar(Evento::Soneca);
    int8_t horizontal = direcao(analogRead(PIN_X));
    int8_t dx = x.ler(horizontal, agora);
    int8_t dy = y.ler(horizontal == 0 ? direcao(analogRead(PIN_Y)) : 0, agora);
    if (dx)
      enviar(dx > 0 ? Evento::Direita : Evento::Esquerda);
    else if (dy)
      enviar(dy > 0 ? Evento::Mais : Evento::Menos);
    vTaskDelayUntil(&ultima, espera(20));
  }
}
void publicar(const Estado &e, uint32_t timer, uint32_t maior, uint32_t descartes);
void publicar(const Estado &e, uint32_t timer, uint32_t maior, uint32_t descartes) {
  Snapshot s = {e, timer, millis(), maior, descartes, tempoConfigurado()};
  xQueueOverwrite(saidaTela, &s); // Fila de UMA posicao; substitui imagem antiga.
}
void tarefaRelogio(void *);
void tarefaRelogio(void *) {
  Estado e = estadoInicial(); // Unico estado autoritativo, privado desta tarefa.
  uint32_t anterior = lerTempo(), cicloAnterior = anterior;
  uint32_t maiorCiclo = 0, descartes = 0;
  bool ultimoToque = false;
  TickType_t ultima = xTaskGetTickCount(), ultimaTela = ultima;
  publicar(e, anterior, maiorCiclo, descartes);
  for (;;) {
    uint32_t agora = lerTempo();
    uint32_t intervalo = agora - cicloAnterior;
    cicloAnterior = agora;
    if (intervalo > maiorCiclo)
      maiorCiclo = intervalo;
    avancar(e, agora - anterior);
    anterior = agora;
    uint32_t perdidos = ulTaskNotifyTake(pdTRUE, 0);
    descartes += perdidos;
    Evento evento;
    for (uint8_t n = 0; n < FILA_ENTRADAS; ++n) {
      if (xQueueReceive(entradas, &evento, 0) != pdPASS)
        break;
      agora = lerTempo();
      avancar(e, agora - anterior);
      anterior = agora;
      comandar(e, evento);
    }
    bool toque = e.aviso == Aviso::Tocando;
    if (toque != ultimoToque) {
      // Estado desejado mais recente; um OFF nao se perde atras de comandos velhos.
      xQueueOverwrite(saidaAlarme, &toque);
      ultimoToque = toque;
    }
    TickType_t agoraKernel = xTaskGetTickCount();
    // Publicacao independe de Timer1: permite mostrar falha se ele for parado.
    if ((TickType_t)(agoraKernel - ultimaTela) >= espera(100)) {
      publicar(e, anterior, maiorCiclo, descartes);
      ultimaTela = agoraKernel;
    }
    vTaskDelayUntil(&ultima, espera(20));
  }
}
void tarefaAlarme(void *);
void tarefaAlarme(void *) {
  bool ativo = false, fase = false;
  for (;;) {
    bool comando;
    if (xQueueReceive(saidaAlarme, &comando, ativo ? espera(200) : portMAX_DELAY) ==
        pdPASS) {
      ativo = comando;
      fase = ativo;
    } else
      fase = !fase;
    if (ativo && fase)
      tone(PIN_BUZZER, 2000);
    else
      noTone(PIN_BUZZER);
    digitalWrite(PIN_LED, ativo && fase ? HIGH : LOW);
  }
}
#if DIAGNOSTICO_SERIAL
void diagnosticar(const Snapshot &s);
void diagnosticar(const Snapshot &s) {
  Serial.print(F("ZERO-CTC-1 t1="));
  Serial.print(s.timer);
  Serial.print(F(" ms="));
  Serial.print(s.milissegundos);
  Serial.print(F(" hora="));
  Serial.print(s.estado.agora.hora);
  Serial.print(':');
  Serial.print(s.estado.agora.minuto);
  Serial.print(':');
  Serial.print(s.estado.agora.segundo);
  Serial.print(F(" ajustes="));
  Serial.print(s.estado.ajustesHora);
  Serial.print(F(" perdasFila="));
  Serial.print(s.descartes);
  Serial.print(F(" cicloMax="));
  Serial.print(s.maiorCiclo);
  Serial.print(F(" timerOK="));
  Serial.print(s.timerOk);
  Serial.print(F(" pilhas="));
  Serial.print(uxTaskGetStackHighWaterMark(hRelogio));
  Serial.print('/');
  Serial.print(uxTaskGetStackHighWaterMark(hBotoes));
  Serial.print('/');
  Serial.print(uxTaskGetStackHighWaterMark(hAlarme));
  Serial.print('/');
  Serial.println(uxTaskGetStackHighWaterMark(hDisplay));
}
#endif
void tarefaDisplay(void *);
void tarefaDisplay(void *) {
  Snapshot s;
  char l0[17], l1[17], anterior0[17] = "", anterior1[17] = "";
  uint32_t descartesVistos = 0;
  uint32_t avisoAte = 0;
  bool aviso = false;
#if DIAGNOSTICO_SERIAL
  uint32_t ultimoLog = 0;
  bool primeiro = true;
#endif
  for (;;) {
    if (xQueueReceive(saidaTela, &s, portMAX_DELAY) != pdPASS)
      continue;
    int8_t cursor;
    renderizar(s.estado, l0, l1, cursor);
    if (s.descartes != descartesVistos) {
      descartesVistos = s.descartes;
      avisoAte = s.timer;
      aviso = true;
    }
    if (aviso && (uint32_t)(s.timer - avisoAte) >= 200)
      aviso = false;
    if (aviso && s.estado.aviso == Aviso::Silencio) {
      memcpy(l0, "REPITA O COMANDO", 16);
      l0[16] = 0;
      memcpy(l1, "FILA DE ENTRADAS", 16);
      l1[16] = 0;
      cursor = -1;
    }
    if (!s.timerOk) {
      memcpy(l0, "FALHA NO TIMER1 ", 16);
      l0[16] = 0;
      cursor = -1;
    }
    if (strcmp(l0, anterior0)) {
      lcd.setCursor(0, 0);
      lcd.print(l0);
      strcpy(anterior0, l0);
    }
    if (strcmp(l1, anterior1)) {
      lcd.setCursor(0, 1);
      lcd.print(l1);
      strcpy(anterior1, l1);
    }
    if (cursor >= 0) {
      lcd.setCursor(cursor, 0);
      lcd.cursor();
    } else
      lcd.noCursor();
#if DIAGNOSTICO_SERIAL
    if (primeiro || (uint32_t)(s.milissegundos - ultimoLog) >= 10000UL) {
      diagnosticar(s);
      ultimoLog = s.milissegundos;
      primeiro = false;
    }
#endif
  }
}
void falhar(const char *motivo);
void falhar(const char *motivo) {
  noTone(PIN_BUZZER);
  digitalWrite(PIN_LED, LOW);
  lcd.clear();
  lcd.print("ERRO AO INICIAR");
  lcd.setCursor(0, 1);
  lcd.print(motivo);
  for (;;) {
  } // Apenas antes do kernel: nao opera com recursos invalidos.
}
} // namespace
void iniciarAplicacao();
void iniciarAplicacao() {
  pinMode(PIN_SEL, INPUT_PULLUP);
  pinMode(PIN_SONECA, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);
  lcd.begin(16, 2);
  lcd.print("Relogio RTOS");
#if DIAGNOSTICO_SERIAL
  Serial.begin(115200);
  Serial.println(F("ZERO-CTC-1 | Mega 16 MHz | Timer1 CTC 100 Hz"));
#endif
  entradas = xQueueCreate(FILA_ENTRADAS, sizeof(Evento));
  saidaAlarme = xQueueCreate(1, sizeof(bool));
  saidaTela = xQueueCreate(1, sizeof(Snapshot));
  if (!entradas || !saidaAlarme || !saidaTela)
    falhar("FILA");
  if (xTaskCreate(tarefaAlarme, "Alarme", 256, NULL, 3, &hAlarme) != pdPASS)
    falhar("ALARME");
  if (xTaskCreate(tarefaRelogio, "Relogio", 512, NULL, 2, &hRelogio) != pdPASS)
    falhar("RELOGIO");
  if (xTaskCreate(tarefaBotoes, "Botoes", 288, NULL, 2, &hBotoes) != pdPASS)
    falhar("BOTOES");
  if (xTaskCreate(tarefaDisplay, "Display", 512, NULL, 1, &hDisplay) != pdPASS)
    falhar("DISPLAY");
  iniciarTempo(); // Antes de qualquer tarefa ler o contador. Kernel inicia ao retornar.
}
// FIM_APLICACAO

void setup() { iniciarAplicacao(); }
void loop() {}
