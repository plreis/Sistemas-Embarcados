#include "entradas.h"
#include "nucleo.h"
#include "tempo_avr.h"
#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <LiquidCrystal.h>
#include <queue.h>
#include <string.h>

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
TickType_t espera(uint16_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  return ticks ? ticks : 1;
}
void enviar(Evento evento) {
  if (xQueueSend(entradas, &evento, 0) != pdPASS) {
    // Notificacao contadora: entrada descartada e visivel sem bloquear produtor.
    xTaskNotifyGive(hRelogio);
  }
}
int8_t direcao(int leitura) {
  return leitura < 200 ? -1 : leitura > 800 ? 1 : 0;
}
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
void publicar(const Estado &e, uint32_t timer, uint32_t maior, uint32_t descartes) {
  Snapshot s = {e, timer, millis(), maior, descartes, tempoConfigurado()};
  xQueueOverwrite(saidaTela, &s); // Fila de UMA posicao; substitui imagem antiga.
}
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
