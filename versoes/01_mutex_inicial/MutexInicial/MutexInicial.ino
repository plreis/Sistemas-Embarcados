#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <LiquidCrystal.h>

// Alarme / Relogio com FreeRTOS - Arduino MEGA (ATmega2560)
// Hardware real: LCD 16x2 paralelo, joystick, pot 10k no contraste, buzzer, LED.
// Tasks: TaskRelogio (2), TaskBotoes (2), TaskAlarme (3), TaskDisplay (1)

#define PINO_LCD_RS 22
#define PINO_LCD_E  23
#define PINO_LCD_D4 24
#define PINO_LCD_D5 25
#define PINO_LCD_D6 26
#define PINO_LCD_D7 27

#define PINO_JOYSTICK_HORZ 54 // Entrada analogica A0 do Mega 2560
#define PINO_JOYSTICK_VERT 55 // Entrada analogica A1 do Mega 2560
#define PINO_JOYSTICK_SEL  2
#define BOTAO_SONECA       5

#define PINO_BUZZER 8
#define PINO_LED_ALARME 9

#define SONECA_SEGUNDOS 60
#define TEMPO_DEBOUNCE_MS 50
#define LIMIAR_JOYSTICK_ALTO 800
#define LIMIAR_JOYSTICK_BAIXO 200

LiquidCrystal lcd(PINO_LCD_RS, PINO_LCD_E, PINO_LCD_D4, PINO_LCD_D5, PINO_LCD_D6, PINO_LCD_D7);

typedef enum {
  MODO_NORMAL = 0,
  MODO_AJUSTE_HORA,
  MODO_AJUSTE_MINUTOS,
  MODO_AJUSTE_DIA,
  MODO_AJUSTE_MES,
  MODO_AJUSTE_ALARME_HORA,
  MODO_AJUSTE_ALARME_MINUTOS
} ModoEdicao_t;

typedef struct {
  uint8_t hora, minutos, segundos;
  uint8_t dia, mes;
  uint16_t ano;
  uint8_t alarmeHora, alarmeMinutos;
  bool alarmeLigado;
  bool tocando;
  bool emSoneca;
  uint32_t fimSonecaEpoch;
  ModoEdicao_t modoEdicao;
} Relogio_t;

Relogio_t relogio = {
  12, 0, 0,
  29, 9, 2026,
  7, 0,
  true, false, false, 0,
  MODO_NORMAL
};

SemaphoreHandle_t mutexRelogio;

const char* nomeModo(ModoEdicao_t modo) {
  switch (modo) {
    case MODO_NORMAL: return "Retornar";
    case MODO_AJUSTE_HORA: return "Set hora";
    case MODO_AJUSTE_MINUTOS: return "Set min";
    case MODO_AJUSTE_DIA: return "Set dia";
    case MODO_AJUSTE_MES: return "Set mes";
    case MODO_AJUSTE_ALARME_HORA: return "Set Alarm";
    case MODO_AJUSTE_ALARME_MINUTOS: return "AJ AL M";
  }
  return "";
}

uint8_t diasNoMes(uint8_t mes, uint16_t ano) {
  if (mes == 2) {
    bool bissexto = ((ano % 4 == 0 && ano % 100 != 0) || (ano % 400 == 0));
    return bissexto ? 29 : 28;
  }
  if (mes == 4 || mes == 6 || mes == 9 || mes == 11) return 30;
  return 31;
}

void aplicarMais() {
  switch (relogio.modoEdicao) {
    case MODO_AJUSTE_HORA: relogio.hora = (relogio.hora + 1) % 24; break;
    case MODO_AJUSTE_MINUTOS: relogio.minutos = (relogio.minutos + 1) % 60; relogio.segundos = 0; break;
    case MODO_AJUSTE_DIA: relogio.dia++; if (relogio.dia > diasNoMes(relogio.mes, relogio.ano)) relogio.dia = 1; break;
    case MODO_AJUSTE_MES: relogio.mes++; if (relogio.mes > 12) relogio.mes = 1; break;
    case MODO_AJUSTE_ALARME_HORA: relogio.alarmeHora = (relogio.alarmeHora + 1) % 24; break;
    case MODO_AJUSTE_ALARME_MINUTOS: relogio.alarmeMinutos = (relogio.alarmeMinutos + 1) % 60; break;
    default: break;
  }
}

void aplicarMenos() {
  switch (relogio.modoEdicao) {
    case MODO_AJUSTE_HORA: relogio.hora = (relogio.hora + 23) % 24; break;
    case MODO_AJUSTE_MINUTOS: relogio.minutos = (relogio.minutos + 59) % 60; relogio.segundos = 0; break;
    case MODO_AJUSTE_DIA: relogio.dia--; if (relogio.dia < 1) relogio.dia = diasNoMes(relogio.mes, relogio.ano); break;
    case MODO_AJUSTE_MES: relogio.mes--; if (relogio.mes < 1) relogio.mes = 12; break;
    case MODO_AJUSTE_ALARME_HORA: relogio.alarmeHora = (relogio.alarmeHora + 23) % 24; break;
    case MODO_AJUSTE_ALARME_MINUTOS: relogio.alarmeMinutos = (relogio.alarmeMinutos + 59) % 60; break;
    default: break;
  }
}

bool botaoSonecaApertado() {
  if (digitalRead(BOTAO_SONECA) == LOW) {
    vTaskDelay(TEMPO_DEBOUNCE_MS / portTICK_PERIOD_MS);
    if (digitalRead(BOTAO_SONECA) == LOW) {
      while (digitalRead(BOTAO_SONECA) == LOW) vTaskDelay(20 / portTICK_PERIOD_MS);
      return true;
    }
  }
  return false;
}

bool joystickSelApertado() {
  if (digitalRead(PINO_JOYSTICK_SEL) == LOW) {
    vTaskDelay(TEMPO_DEBOUNCE_MS / portTICK_PERIOD_MS);
    if (digitalRead(PINO_JOYSTICK_SEL) == LOW) {
      while (digitalRead(PINO_JOYSTICK_SEL) == LOW) vTaskDelay(20 / portTICK_PERIOD_MS);
      return true;
    }
  }
  return false;
}

void TaskRelogio(void *parametros) {
  (void) parametros;
  TickType_t ultimaAtivacao = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&ultimaAtivacao, 1000 / portTICK_PERIOD_MS);
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    relogio.segundos++;
    if (relogio.segundos >= 60) {
      relogio.segundos = 0;
      relogio.minutos++;
      if (relogio.minutos >= 60) {
        relogio.minutos = 0;
        relogio.hora++;
        if (relogio.hora >= 24) {
          relogio.hora = 0;
          relogio.dia++;
          if (relogio.dia > diasNoMes(relogio.mes, relogio.ano)) {
            relogio.dia = 1;
            relogio.mes++;
            if (relogio.mes > 12) { relogio.mes = 1; relogio.ano++; }
          }
        }
      }
    }
    xSemaphoreGive(mutexRelogio);
  }
}

void TaskBotoes(void *parametros) {
  (void) parametros;
  for (;;) {
    if (joystickSelApertado()) {
      xSemaphoreTake(mutexRelogio, portMAX_DELAY);
      if (relogio.tocando) {
        relogio.tocando = false;
        relogio.emSoneca = true;
        relogio.fimSonecaEpoch = (uint32_t)relogio.hora * 3600UL + (uint32_t)relogio.minutos * 60UL + 60UL;
      } else {
        relogio.modoEdicao = (ModoEdicao_t)(((int)relogio.modoEdicao + 1) % 7);
      }
      xSemaphoreGive(mutexRelogio);
    }

    int eixoX = analogRead(PINO_JOYSTICK_HORZ);
    if (eixoX > LIMIAR_JOYSTICK_ALTO || eixoX < LIMIAR_JOYSTICK_BAIXO) {
      xSemaphoreTake(mutexRelogio, portMAX_DELAY);
      if (!relogio.tocando) {
        if (eixoX > LIMIAR_JOYSTICK_ALTO) aplicarMais();
        else aplicarMenos();
      }
      xSemaphoreGive(mutexRelogio);
      vTaskDelay(220 / portTICK_PERIOD_MS);
    }

    if (botaoSonecaApertado()) {
      xSemaphoreTake(mutexRelogio, portMAX_DELAY);
      if (relogio.tocando) {
        uint32_t agoraSegundos = (uint32_t)relogio.hora * 3600UL + (uint32_t)relogio.minutos * 60UL + relogio.segundos;
        relogio.tocando = false;
        relogio.emSoneca = true;
        relogio.fimSonecaEpoch = agoraSegundos + SONECA_SEGUNDOS;
      } else {
        relogio.alarmeLigado = !relogio.alarmeLigado;
        relogio.emSoneca = false;
      }
      xSemaphoreGive(mutexRelogio);
    }

    vTaskDelay(30 / portTICK_PERIOD_MS);
  }
}

void TaskAlarme(void *parametros) {
  (void) parametros;
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_LED_ALARME, OUTPUT);
  noTone(PINO_BUZZER);
  for (;;) {
    bool deveTocar;
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    uint32_t agoraSegundos = (uint32_t)relogio.hora * 3600UL + (uint32_t)relogio.minutos * 60UL + relogio.segundos;
    if (relogio.alarmeLigado) {
      if (relogio.emSoneca) {
        if (agoraSegundos >= relogio.fimSonecaEpoch) {
          relogio.emSoneca = false;
          relogio.tocando = true;
        }
      } else if (relogio.hora == relogio.alarmeHora && relogio.minutos == relogio.alarmeMinutos) {
        relogio.tocando = true;
      }
    } else {
      relogio.tocando = false;
      relogio.emSoneca = false;
    }
    deveTocar = relogio.tocando;
    xSemaphoreGive(mutexRelogio);

    if (deveTocar) {
      tone(PINO_BUZZER, 2000);
      digitalWrite(PINO_LED_ALARME, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      noTone(PINO_BUZZER);
      digitalWrite(PINO_LED_ALARME, LOW);
      vTaskDelay(200 / portTICK_PERIOD_MS);
    } else {
      noTone(PINO_BUZZER);
      digitalWrite(PINO_LED_ALARME, LOW);
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

void TaskDisplay(void *parametros) {
  (void) parametros;
  char linha0[17], linha1[17];
  for (;;) {
    Relogio_t copia;
    xSemaphoreTake(mutexRelogio, portMAX_DELAY);
    copia = relogio;
    xSemaphoreGive(mutexRelogio);

    if (copia.tocando) {
      snprintf(linha0, sizeof(linha0), "!! ALARME !!    ");
      snprintf(linha1, sizeof(linha1), "APERTE SONECA   ");
    } else if (copia.emSoneca && copia.alarmeLigado) {
      uint32_t agoraSegundos = (uint32_t)copia.hora * 3600UL + (uint32_t)copia.minutos * 60UL + copia.segundos;
      uint32_t falta = (copia.fimSonecaEpoch > agoraSegundos) ? (copia.fimSonecaEpoch - agoraSegundos) : 0;
      if (falta > 65) {
        snprintf(linha0, sizeof(linha0), "%02d:%02d:%02d Zz    ", copia.hora, copia.minutos, copia.segundos);
        snprintf(linha1, sizeof(linha1), "SONECA %02lu:%02lu  ", (unsigned long)(falta / 60), (unsigned long)(falta % 60));
      } else {
        snprintf(linha0, sizeof(linha0), "%02d:%02d:%02d %c      ", copia.hora, copia.minutos, copia.segundos, copia.alarmeLigado ? '*' : ' ');
        snprintf(linha1, sizeof(linha1), "%02d/%02d AL%02d:%02d ", copia.dia, copia.mes, copia.alarmeHora, copia.alarmeMinutos);
      }
    } else if (copia.modoEdicao != MODO_NORMAL) {
      snprintf(linha0, sizeof(linha0), "%02d:%02d:%02d        ", copia.hora, copia.minutos, copia.segundos);
      snprintf(linha1, sizeof(linha1), "%-8s        ", nomeModo(copia.modoEdicao));
    } else {
      snprintf(linha0, sizeof(linha0), "%02d:%02d:%02d %c      ", copia.hora, copia.minutos, copia.segundos, copia.alarmeLigado ? '*' : ' ');
      snprintf(linha1, sizeof(linha1), "%02d/%02d AL%02d:%02d   ", copia.dia, copia.mes, copia.alarmeHora, copia.alarmeMinutos);
    }

    lcd.setCursor(0, 0); lcd.print(linha0);
    lcd.setCursor(0, 1); lcd.print(linha1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void mostrarErroInicializacao(const char *recurso) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("ERRO INICIAL");
  lcd.setCursor(0, 1); lcd.print(recurso);
  for (;;) {} // Sem recursos essenciais, nao inicia o escalonador.
}

void setup() {
  pinMode(PINO_JOYSTICK_SEL, INPUT_PULLUP);
  pinMode(BOTAO_SONECA, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_LED_ALARME, OUTPUT);

  lcd.begin(16, 2);
  lcd.setCursor(0, 0); lcd.print("Alarme RTOS MEGA ");
  lcd.setCursor(0, 1); lcd.print("iniciando...     ");

  mutexRelogio = xSemaphoreCreateMutex();
  if (mutexRelogio == NULL) mostrarErroInicializacao("MUTEX");

  if (xTaskCreate(TaskRelogio, "Relogio", 192, NULL, 2, NULL) != pdPASS)
    mostrarErroInicializacao("TASK RELOGIO");
  if (xTaskCreate(TaskBotoes, "Botoes", 192, NULL, 2, NULL) != pdPASS)
    mostrarErroInicializacao("TASK BOTOES");
  if (xTaskCreate(TaskAlarme, "Alarme", 192, NULL, 3, NULL) != pdPASS)
    mostrarErroInicializacao("TASK ALARME");
  if (xTaskCreate(TaskDisplay, "Display", 192, NULL, 1, NULL) != pdPASS)
    mostrarErroInicializacao("TASK DISPLAY");
}

void loop() {
}
