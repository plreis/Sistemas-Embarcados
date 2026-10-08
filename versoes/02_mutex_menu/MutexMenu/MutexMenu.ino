#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <LiquidCrystal.h>
#include <string.h>

// Mega 2560: potenciometro 10k no contraste V0 do LCD; sem volume no software.
#define LCD_RS 22
#define LCD_E 23
#define LCD_D4 24
#define LCD_D5 25
#define LCD_D6 26
#define LCD_D7 27
#define JOY_X 54 // A0
#define JOY_Y 55 // A1
#define JOY_SEL 2
#define BOTAO_SONECA 5
#define BUZZER 8
#define LED_ALARME 9
#define LIMIAR_BAIXO 200
#define LIMIAR_ALTO 800
#define SONECA_MIN 1
#define SONECA_MAX 10 // Cabe no TickType_t de 16 bits desta biblioteca/porta.

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
SemaphoreHandle_t mutexRelogio;

struct Config {
  uint8_t hora, minuto, dia, mes;
  uint16_t ano;
  uint8_t alarmeHora, alarmeMinuto, sonecaMin;
};

struct Estado {
  Config cfg;
  uint8_t segundo;
  bool alarmeLigado, tocando, emSoneca;
  TickType_t inicioSoneca, inicioToque;
  uint32_t ultimoDisparo;
};

Estado relogio = {{12, 0, 29, 9, 2026, 7, 0, 5}, 0, true, false, false, 0, 0, 0};
Config rascunho;
bool editando = false;
uint8_t campo = 0; // Hora, minuto, dia, mes, ano, hora AL, minuto AL, soneca.
bool editouHorario = false, editouData = false;

uint8_t diasNoMes(uint8_t mes, uint16_t ano) {
  if (mes == 2) return ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0) ? 29 : 28;
  if (mes == 4 || mes == 6 || mes == 9 || mes == 11) return 30;
  return 31;
}

void normalizarDia(Config &c) {
  uint8_t limite = diasNoMes(c.mes, c.ano);
  if (c.dia > limite) c.dia = limite;
}

uint8_t circular(uint8_t valor, int8_t direcao, uint8_t minimo, uint8_t maximo) {
  if (direcao > 0) return valor == maximo ? minimo : valor + 1;
  return valor == minimo ? maximo : valor - 1;
}

void mudarCampo(int8_t direcao) {
  if (campo <= 1) editouHorario = true;
  else if (campo <= 4) editouData = true;
  switch (campo) {
    case 0: rascunho.hora = circular(rascunho.hora, direcao, 0, 23); break;
    case 1: rascunho.minuto = circular(rascunho.minuto, direcao, 0, 59); break;
    case 2: rascunho.dia = circular(rascunho.dia, direcao, 1, diasNoMes(rascunho.mes, rascunho.ano)); break;
    case 3: rascunho.mes = circular(rascunho.mes, direcao, 1, 12); normalizarDia(rascunho); break;
    case 4:
      rascunho.ano = direcao > 0 ? (rascunho.ano == 2099 ? 2000 : rascunho.ano + 1)
                                  : (rascunho.ano == 2000 ? 2099 : rascunho.ano - 1);
      normalizarDia(rascunho);
      break;
    case 5: rascunho.alarmeHora = circular(rascunho.alarmeHora, direcao, 0, 23); break;
    case 6: rascunho.alarmeMinuto = circular(rascunho.alarmeMinuto, direcao, 0, 59); break;
    case 7: rascunho.sonecaMin = circular(rascunho.sonecaMin, direcao, SONECA_MIN, SONECA_MAX); break;
  }
}

// Identificador unico para o minuto na faixa de anos editavel (2000 a 2099).
uint32_t chaveMinuto(const Config &c) {
  uint32_t dia = (uint32_t)(c.ano - 2000) * 372UL + (uint32_t)(c.mes - 1) * 31UL + c.dia - 1;
  return dia * 1440UL + (uint16_t)c.hora * 60U + c.minuto + 1UL;
}

TickType_t duracaoSoneca(uint8_t minutos) {
  return (TickType_t)((uint32_t)minutos * 60000UL / portTICK_PERIOD_MS);
}

void avancarSegundo() {
  Config &c = relogio.cfg;
  if (++relogio.segundo < 60) return;
  relogio.segundo = 0;
  if (++c.minuto < 60) return;
  c.minuto = 0;
  if (++c.hora < 24) return;
  c.hora = 0;
  if (++c.dia <= diasNoMes(c.mes, c.ano)) return;
  c.dia = 1;
  if (++c.mes <= 12) return;
  c.mes = 1;
  c.ano = c.ano == 2099 ? 2000 : c.ano + 1;
}

struct Botao {
  bool ultimaLeitura, estavel;
  TickType_t mudouEm;
};

bool apertou(uint8_t pino, Botao &b, TickType_t agora) {
  bool leitura = digitalRead(pino) == LOW;
  if (leitura != b.ultimaLeitura) {
    b.ultimaLeitura = leitura;
    b.mudouEm = agora;
  }
  if (leitura != b.estavel &&
      (TickType_t)(agora - b.mudouEm) >= pdMS_TO_TICKS(50)) {
    b.estavel = leitura;
    return leitura;
  }
  return false;
}

struct Eixo {
  int8_t ultimo;
  TickType_t proximo;
};

int8_t movimento(int leitura, Eixo &e, TickType_t agora) {
  int8_t direcao = leitura < LIMIAR_BAIXO ? -1 : leitura > LIMIAR_ALTO ? 1 : 0;
  if (!direcao) { e.ultimo = 0; return 0; }
  if (direcao != e.ultimo || (int16_t)(agora - e.proximo) >= 0) {
    e.ultimo = direcao;
    e.proximo = agora + pdMS_TO_TICKS(350);
    return direcao;
  }
  return 0;
}

void TaskRelogio(void *p) {
  (void)p;
  TickType_t ultima = xTaskGetTickCount();
  uint8_t restoMs = 0;
  for (;;) {
    // 62/63 ticks alternados para 1000 ms nominais quando o tick vale 16 ms.
    TickType_t periodo = 1000 / portTICK_PERIOD_MS;
    restoMs += 1000 % portTICK_PERIOD_MS;
    if (restoMs >= portTICK_PERIOD_MS) {
      ++periodo;
      restoMs -= portTICK_PERIOD_MS;
    }
    vTaskDelayUntil(&ultima, periodo);
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    avancarSegundo();
    xSemaphoreGive(mutexRelogio);
  }
}

void TaskBotoes(void *p) {
  (void)p;
  Botao sel = {false, false, 0}, soneca = {false, false, 0};
  Eixo x = {0, 0}, y = {0, 0};
  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    bool click = apertou(JOY_SEL, sel, agora);
    bool teclaD5 = apertou(BOTAO_SONECA, soneca, agora);
    int8_t dx = movimento(analogRead(JOY_X), x, agora);
    int8_t dy = movimento(analogRead(JOY_Y), y, agora);
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    if (teclaD5) {
      if (relogio.tocando && relogio.alarmeLigado) {
        relogio.tocando = false;
        relogio.emSoneca = true;
        relogio.inicioSoneca = agora;
      } else {
        relogio.alarmeLigado = !relogio.alarmeLigado;
        relogio.emSoneca = false;
        relogio.tocando = false;
      }
    }
    if (click) {
      if (relogio.tocando) relogio.tocando = false; // Silencia so este disparo.
      else if (!editando) {
        rascunho = relogio.cfg;
        editando = true;
        campo = 0;
        editouHorario = false;
        editouData = false;
      } else {
        // Campos nao alterados preservam o relogio que continuou contando.
        if (editouHorario) {
          relogio.cfg.hora = rascunho.hora;
          relogio.cfg.minuto = rascunho.minuto;
          relogio.segundo = 0;
        }
        if (editouData) {
          relogio.cfg.dia = rascunho.dia;
          relogio.cfg.mes = rascunho.mes;
          relogio.cfg.ano = rascunho.ano;
        }
        relogio.cfg.alarmeHora = rascunho.alarmeHora;
        relogio.cfg.alarmeMinuto = rascunho.alarmeMinuto;
        relogio.cfg.sonecaMin = rascunho.sonecaMin;
        relogio.emSoneca = false;
        relogio.ultimoDisparo = chaveMinuto(relogio.cfg);
        editando = false;
      }
    }
    if (editando && !relogio.tocando) {
      // Montagem fisica: X alto = direita, X baixo = esquerda.
      if (dx > 0) campo = (campo + 1) % 8;
      else if (dx < 0) campo = (campo + 7) % 8;
      if (dy) mudarCampo(dy); // Y alto = cima = aumenta.
    }
    xSemaphoreGive(mutexRelogio);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void TaskAlarme(void *p) {
  (void)p;
  bool fase = false;
  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    bool tocar;
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    if (!relogio.alarmeLigado) {
      relogio.tocando = false;
      relogio.emSoneca = false;
    } else if (relogio.emSoneca) {
      if ((TickType_t)(agora - relogio.inicioSoneca) >= duracaoSoneca(relogio.cfg.sonecaMin)) {
        relogio.emSoneca = false;
        relogio.tocando = true;
        relogio.inicioToque = agora;
      }
    } else if (!relogio.tocando &&
               relogio.cfg.hora == relogio.cfg.alarmeHora &&
               relogio.cfg.minuto == relogio.cfg.alarmeMinuto) {
      uint32_t chave = chaveMinuto(relogio.cfg);
      if (chave != relogio.ultimoDisparo) {
        relogio.ultimoDisparo = chave;
        relogio.tocando = true;
        relogio.inicioToque = agora;
      }
    }
    if (relogio.tocando &&
        (TickType_t)(agora - relogio.inicioToque) >= 60000UL / portTICK_PERIOD_MS)
      relogio.tocando = false;
    tocar = relogio.tocando;
    xSemaphoreGive(mutexRelogio);
    if (tocar) {
      fase = !fase;
      if (fase) tone(BUZZER, 2000); else noTone(BUZZER);
      digitalWrite(LED_ALARME, fase ? HIGH : LOW);
      vTaskDelay(pdMS_TO_TICKS(200));
    } else {
      fase = false;
      noTone(BUZZER);
      digitalWrite(LED_ALARME, LOW);
      vTaskDelay(pdMS_TO_TICKS(80));
    }
  }
}

void escrever2(char *linha, uint8_t pos, uint8_t numero) {
  linha[pos] = '0' + numero / 10;
  linha[pos + 1] = '0' + numero % 10;
}

void preencher(char *linha) {
  memset(linha, ' ', 16);
  linha[16] = '\0';
}

void TaskDisplay(void *p) {
  (void)p;
  char l0[17], l1[17], anterior0[17] = "", anterior1[17] = "";
  bool cursor = false;
  for (;;) {
    Estado c;
    Config e;
    bool ed;
    uint8_t selecionado;
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    c = relogio;
    e = rascunho;
    ed = editando;
    selecionado = campo;
    xSemaphoreGive(mutexRelogio);
    preencher(l0); preencher(l1);
    uint8_t posCursor = 0;
    if (c.tocando) {
      memcpy(l0, "! ALARME TOCANDO", 16);
      memcpy(l1, "D5:Zz SEL:parar ", 16);
    } else if (ed) {
      const char *rotulos[] = {"HORA", "MIN", "DIA", "MES", "ANO", "AL-H", "AL-M", "SONECA"};
      memcpy(l1, rotulos[selecionado], strlen(rotulos[selecionado]));
      memcpy(l1 + 7, "X<>Y+-OK", 8);
      if (selecionado <= 1) {
        memcpy(l0, "HORA ", 5);
        escrever2(l0, 5, e.hora); l0[7] = ':';
        escrever2(l0, 8, e.minuto); l0[10] = ':';
        escrever2(l0, 11, 0);
        posCursor = selecionado == 0 ? 5 : 8;
      } else if (selecionado <= 4) {
        memcpy(l0, "DATA ", 5);
        escrever2(l0, 5, e.dia); l0[7] = '/';
        escrever2(l0, 8, e.mes); l0[10] = '/';
        escrever2(l0, 11, e.ano / 100);
        escrever2(l0, 13, e.ano % 100);
        posCursor = selecionado == 2 ? 5 : selecionado == 3 ? 8 : 11;
      } else if (selecionado <= 6) {
        memcpy(l0, "ALARME ", 7);
        escrever2(l0, 7, e.alarmeHora); l0[9] = ':';
        escrever2(l0, 10, e.alarmeMinuto);
        posCursor = selecionado == 5 ? 7 : 10;
      } else {
        memcpy(l0, "SONECA ", 7);
        escrever2(l0, 7, e.sonecaMin);
        memcpy(l0 + 10, "min", 3);
        posCursor = 7;
      }
    } else if (c.emSoneca) {
      memcpy(l0, "SONECA ATIVA", 12);
      TickType_t duracao = duracaoSoneca(c.cfg.sonecaMin);
      TickType_t passou = (TickType_t)(xTaskGetTickCount() - c.inicioSoneca);
      uint32_t falta = passou >= duracao ? 0 :
        ((uint32_t)(duracao - passou) * portTICK_PERIOD_MS + 999UL) / 1000UL;
      memcpy(l1, "RESTA ", 6);
      escrever2(l1, 6, falta / 60); l1[8] = ':';
      escrever2(l1, 9, falta % 60);
      memcpy(l1 + 12, "D5:-", 4);
    } else {
      escrever2(l0, 0, c.cfg.hora); l0[2] = ':';
      escrever2(l0, 3, c.cfg.minuto); l0[5] = ':';
      escrever2(l0, 6, c.segundo);
      memcpy(l0 + 9, c.alarmeLigado ? "AL" : "--", 2);
      escrever2(l0, 11, c.cfg.alarmeHora); l0[13] = ':';
      escrever2(l0, 14, c.cfg.alarmeMinuto);
      escrever2(l1, 0, c.cfg.dia); l1[2] = '/';
      escrever2(l1, 3, c.cfg.mes); l1[5] = '/';
      escrever2(l1, 6, c.cfg.ano / 100);
      escrever2(l1, 8, c.cfg.ano % 100);
      memcpy(l1 + 12, c.alarmeLigado ? "ON " : "OFF", 3);
    }
    if (strcmp(l0, anterior0)) {
      lcd.setCursor(0, 0); lcd.print(l0); strcpy(anterior0, l0);
    }
    if (strcmp(l1, anterior1)) {
      lcd.setCursor(0, 1); lcd.print(l1); strcpy(anterior1, l1);
    }
    bool mostrar = ed && !c.tocando;
    if (mostrar != cursor) {
      if (mostrar) lcd.cursor(); else lcd.noCursor();
      cursor = mostrar;
    }
    if (mostrar) lcd.setCursor(posCursor, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

void erroInicial(const char *recurso) {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print("ERRO INICIAL");
  lcd.setCursor(0, 1); lcd.print(recurso);
  for (;;) {}
}

void setup() {
  pinMode(JOY_SEL, INPUT_PULLUP);
  pinMode(BOTAO_SONECA, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_ALARME, OUTPUT);
  lcd.begin(16, 2);
  lcd.print("Alarme RTOS MEGA");
  lcd.setCursor(0, 1); lcd.print("iniciando...");
  mutexRelogio = xSemaphoreCreateMutex();
  if (mutexRelogio == NULL) erroInicial("MUTEX");
  if (xTaskCreate(TaskRelogio, "Relogio", 192, NULL, 2, NULL) != pdPASS) erroInicial("TASK RELOGIO");
  if (xTaskCreate(TaskBotoes, "Botoes", 224, NULL, 2, NULL) != pdPASS) erroInicial("TASK BOTOES");
  if (xTaskCreate(TaskAlarme, "Alarme", 192, NULL, 3, NULL) != pdPASS) erroInicial("TASK ALARME");
  if (xTaskCreate(TaskDisplay, "Display", 256, NULL, 1, NULL) != pdPASS) erroInicial("TASK DISPLAY");
}

void loop() {}
