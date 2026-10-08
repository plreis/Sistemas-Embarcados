#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <LiquidCrystal.h>
#include <string.h>
#include "relogio_tipos.h" // BIBLIOTECA COM AS STRUCTS

/*
 * Mega 2560 / LCD 1602 paralelo.
 * TaskRelogio e a unica dona do estado. As demais tarefas usam filas.
 * Potenciometro: somente contraste V0 do LCD, sem controle de volume.
 */




  /// PINAGEM DO MEGA2560
constexpr uint8_t LCD_RS = 22, LCD_E = 23;
constexpr uint8_t LCD_D4 = 24, LCD_D5 = 25, LCD_D6 = 26, LCD_D7 = 27;
constexpr uint8_t JOY_X = 54, JOY_Y = 55; // A0 e A1 no Mega; nao sao strings.
constexpr uint8_t JOY_SEL = 2, BOTAO_SONECA = 5, BUZZER = 8, LED_ALARME = 9;
constexpr uint8_t SONECA_MIN = 1, SONECA_MAX = 10;
constexpr uint8_t NUM_CAMPOS = 8, TAM_FILA_EVENTOS = 16;
constexpr uint32_t AVISO_FILA_CHEIA = 1UL;
// INICIA DISPLAY DE LED

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// HANDLER PARA A FILA
QueueHandle_t filaEventos, filaAlarme, filaTela;
// HANDLER PARA AS TASKS
TaskHandle_t tarefaRelogio, tarefaBotoes, tarefaAlarme, tarefaDisplay;

/* Todas as conversoes ms -> ticks usam a API oficial. Nunca atrasar zero ticks. */
TickType_t ticksMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  return ticks == 0 ? 1 : ticks;
}

/*
 * Multiplicacao em 32 bits APOS converter um segundo.
 * Evita converter 600000 ms diretamente para TickType_t de 16 bits.
 * O tick do watchdog tem tolerancia: periodicidade RTOS nao garante precisao civil.
 */
uint32_t ticksSegundos(uint16_t segundos) {
  return (uint32_t)ticksMs(1000) * segundos;
}

//  FUNÇÃO UINT_8 PARA DIAS DO MES

uint8_t diasNoMes(uint8_t mes, uint16_t ano) {
  if (mes == 2)
    return ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0) ? 29 : 28;
  return (mes == 4 || mes == 6 || mes == 9 || mes == 11) ? 30 : 31;
}
// SE DIA > 30 RESETA PARA 0 DIAS / NOVO MES

void normalizarDia(Configuracao &c) {
  uint8_t limite = diasNoMes(c.mes, c.ano);
  if (c.dia > limite) c.dia = limite;
}

// CIRCULAR ? 
uint8_t circular(uint8_t valor, int8_t delta, uint8_t minimo, uint8_t maximo) {
  if (delta > 0) return valor == maximo ? minimo : valor + 1;
  return valor == minimo ? maximo : valor - 1;
}

// CONTAGEM DOS SEGUNDOS

void avancarSegundo(EstadoRelogio &r) {
  Configuracao &c = r.cfg;
  if (++r.segundo < 60) return;
  r.segundo = 0;
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

uint32_t chaveMinuto(const Configuracao &c) {
  /* Chave distinta por data/minuto na faixa 2000..2099; zero fica reservado. */
  uint32_t dia = (uint32_t)(c.ano - 2000) * 372UL +
                 (uint32_t)(c.mes - 1) * 31UL + c.dia - 1;
  return dia * 1440UL + (uint16_t)c.hora * 60U + c.minuto + 1UL;
}


//GARANTE FIM DO RELAOGIO ? 
void avaliarAlarme(EstadoRelogio &r) {
  if (!r.alarmeLigado || r.cfg.hora != r.cfg.alarmeHora ||
      r.cfg.minuto != r.cfg.alarmeMinuto) return;
  uint32_t chave = chaveMinuto(r.cfg);
  if (chave == r.ultimoDisparo) return;
  r.ultimoDisparo = chave;
  /* Se ja ha uma soneca/toque, a ocorrencia coincidente nao cria outro aviso. */
  if (r.sonecaTicks == 0 && r.toqueTicks == 0)
    r.toqueTicks = ticksSegundos(60);
}

/* Unica implementacao de soneca para D5 e clique curto no joystick. */
void iniciarSoneca(EstadoRelogio &r) {
  if (!r.alarmeLigado || r.toqueTicks == 0) return;
  r.toqueTicks = 0;
  r.sonecaTicks = ticksSegundos((uint16_t)r.cfg.sonecaMinutos * 60U);
}

void descontarPrazo(uint32_t &prazo, uint32_t decorrido) {
  prazo = decorrido >= prazo ? 0 : prazo - decorrido;
}

void atualizarPrazos(EstadoRelogio &r, TickType_t decorrido) {
  descontarPrazo(r.avisoTicks, decorrido);
  if (r.sonecaTicks != 0) {
    if ((uint32_t)decorrido >= r.sonecaTicks) {
      uint32_t excedente = (uint32_t)decorrido - r.sonecaTicks;
      r.sonecaTicks = 0;
      r.toqueTicks = ticksSegundos(60);
      descontarPrazo(r.toqueTicks, excedente);
    } else r.sonecaTicks -= decorrido;
  } else descontarPrazo(r.toqueTicks, decorrido);
}

void alterarCampo(Edicao &e, int8_t delta) {
  Configuracao &c = e.rascunho;
  if (e.campo <= 1) e.mudouHora = true;
  else if (e.campo <= 4) e.mudouData = true;
  switch (e.campo) {
    case 0: c.hora = circular(c.hora, delta, 0, 23); break;
    case 1: c.minuto = circular(c.minuto, delta, 0, 59); break;
    case 2: c.dia = circular(c.dia, delta, 1, diasNoMes(c.mes, c.ano)); break;
    case 3: c.mes = circular(c.mes, delta, 1, 12); normalizarDia(c); break;
    case 4:
      c.ano = delta > 0 ? (c.ano == 2099 ? 2000 : c.ano + 1)
                       : (c.ano == 2000 ? 2099 : c.ano - 1);
      normalizarDia(c);
      break;
    case 5: c.alarmeHora = circular(c.alarmeHora, delta, 0, 23); break;
    case 6: c.alarmeMinuto = circular(c.alarmeMinuto, delta, 0, 59); break;
    case 7: c.sonecaMinutos = circular(c.sonecaMinutos, delta, SONECA_MIN, SONECA_MAX); break;
  }
}

void salvarEdicao(EstadoRelogio &r, Edicao &e, uint32_t &fracaoSegundo) {
  /* Editar so alarme/soneca nao retrocede a hora que continuou contando. */
  if (e.mudouHora) {
    r.cfg.hora = e.rascunho.hora;
    r.cfg.minuto = e.rascunho.minuto;
    r.segundo = 0;
    fracaoSegundo = 0; // Primeiro segundo completo apos confirmar a nova hora.
  }
  if (e.mudouData) {
    r.cfg.dia = e.rascunho.dia;
    r.cfg.mes = e.rascunho.mes;
    r.cfg.ano = e.rascunho.ano;
  }
  r.cfg.alarmeHora = e.rascunho.alarmeHora;
  r.cfg.alarmeMinuto = e.rascunho.alarmeMinuto;
  r.cfg.sonecaMinutos = e.rascunho.sonecaMinutos;
  e.ativa = false;
}

void tratarEvento(Evento evento, EstadoRelogio &r, Edicao &e, uint32_t &fracao) {
  if (evento == Evento::SEL_LONGO) {
    r.toqueTicks = 0;
    r.sonecaTicks = 0;
    e.ativa = false; // Cancela o rascunho; mantem o alarme para a proxima ocorrencia.
    return;
  }
  if (r.toqueTicks != 0) {
    if (evento == Evento::SONECA || evento == Evento::SEL_CURTO) iniciarSoneca(r);
    return;
  }
  if (r.sonecaTicks != 0) {
    if (evento == Evento::SONECA) {
      r.sonecaTicks = 0;
      r.alarmeLigado = false;
    }
    return;
  }
  if (evento == Evento::SONECA) {
    if (e.ativa) e.ativa = false; // D5 cancela a edicao.
    else r.alarmeLigado = !r.alarmeLigado;
    return;
  }
  if (evento == Evento::SEL_CURTO) {
    if (e.ativa) salvarEdicao(r, e, fracao);
    else {
      e.rascunho = r.cfg;
      e.ativa = true;
      e.mudouHora = e.mudouData = false;
      e.campo = 0;
    }
    return;
  }
  if (!e.ativa) return;
  switch (evento) {
    case Evento::DIREITA: e.campo = (e.campo + 1) % NUM_CAMPOS; break;
    case Evento::ESQUERDA: e.campo = (e.campo + NUM_CAMPOS - 1) % NUM_CAMPOS; break;
    case Evento::CIMA: alterarCampo(e, 1); break;
    case Evento::BAIXO: alterarCampo(e, -1); break;
    default: break;
  }
}

/*
 * 0 = nenhum evento; 1 = curto; 2 = longo.
 * Debounce por estado e borda, sem esperar o usuario soltar o botao.
 * SEL curto e emitido na soltura; D5 e emitido no aperto confirmado.
 */
uint8_t lerBotao(uint8_t pino, BotaoDebounce &b, bool permiteLongo, TickType_t agora) {
  bool leitura = digitalRead(pino) == LOW;
  if (leitura != b.leituraAnterior) {
    b.leituraAnterior = leitura;
    b.mudouEm = agora;
  }
  if (leitura != b.estavel &&
      (TickType_t)(agora - b.mudouEm) >= ticksMs(50)) {
    b.estavel = leitura;
    if (leitura) {
      b.pressionouEm = agora;
      b.longoEnviado = false;
      if (!permiteLongo) return 1;
    } else if (permiteLongo && !b.longoEnviado) return 1;
  }
  if (permiteLongo && b.estavel && leitura && !b.longoEnviado &&
      (TickType_t)(agora - b.pressionouEm) >= ticksMs(1000)) {
    b.longoEnviado = true;
    return 2;
  }
  return 0;
}

int8_t direcaoEixo(int leitura) {
  return leitura < 200 ? -1 : leitura > 800 ? 1 : 0;
}

int8_t lerEixo(int8_t direcao, EixoJoystick &e, TickType_t agora) {
  if (direcao == 0) { e.anterior = 0; return 0; }
  if (direcao != e.anterior ||
      (TickType_t)(agora - e.repetiuEm) >= ticksMs(350)) {
    e.anterior = direcao;
    e.repetiuEm = agora;
    return direcao;
  }
  return 0;
}

void enviarEvento(Evento evento) {
  if (xQueueSend(filaEventos, &evento, 0) != pdPASS) {
    /* Sobrecarga nao trava a leitura nem passa despercebida ao usuario. */
    xTaskNotify(tarefaRelogio, AVISO_FILA_CHEIA, eSetBits);
  }
}

void TaskBotoes(void *parametros) {
  (void)parametros;
  BotaoDebounce sel = {}, d5 = {};
  EixoJoystick eixoX = {}, eixoY = {};
  TickType_t ultima = xTaskGetTickCount();
  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    uint8_t eventoSel = lerBotao(JOY_SEL, sel, true, agora);
    if (eventoSel) enviarEvento(eventoSel == 2 ? Evento::SEL_LONGO : Evento::SEL_CURTO);
    if (lerBotao(BOTAO_SONECA, d5, false, agora)) enviarEvento(Evento::SONECA);

    int8_t x = direcaoEixo(analogRead(JOY_X));
    int8_t y = direcaoEixo(analogRead(JOY_Y));
    int8_t dx = lerEixo(x, eixoX, agora);
    /* Prioriza navegacao nas diagonais: evita alterar outro campo por acidente. */
    int8_t dy = lerEixo(x == 0 ? y : 0, eixoY, agora);
    if (dx) enviarEvento(dx > 0 ? Evento::DIREITA : Evento::ESQUERDA);
    else if (dy) enviarEvento(dy > 0 ? Evento::CIMA : Evento::BAIXO);
    vTaskDelayUntil(&ultima, ticksMs(20));
    
  }
}

void publicarTela(const EstadoRelogio &r, const Edicao &e) {
  Tela tela = {};
  tela.cfg = e.ativa ? e.rascunho : r.cfg;
  tela.segundo = r.segundo;
  tela.campo = e.campo;
  tela.alarmeLigado = r.alarmeLigado;
  tela.editando = e.ativa;
  tela.tocando = r.toqueTicks != 0;
  tela.emSoneca = r.sonecaTicks != 0;
  tela.avisoEntrada = r.avisoTicks != 0;
  uint32_t segundo = ticksMs(1000);
  tela.sonecaSegundos = (uint16_t)((r.sonecaTicks + segundo - 1) / segundo);
  /* Mailbox de UMA posicao: o LCD sempre recebe o estado mais recente. */
  xQueueOverwrite(filaTela, &tela);
}

void TaskRelogio(void *parametros) {
  (void)parametros;
  EstadoRelogio r = {};
  r.cfg = {16, 21, 6, 10, 2026, 17, 50, 5};
  r.alarmeLigado = true;
  Edicao edicao = {};
  TickType_t ultima = xTaskGetTickCount(), anterior = ultima, ultimaTela = ultima;
  uint32_t fracaoSegundo = 0;
  bool alarmeAnterior = false;
  publicarTela(r, edicao);

  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    /* Subtracao unsigned atravessa o wrap do contador RTOS. A tarefa roda a
       cada 20 ms, muito antes de uma volta completa do TickType_t de 16 bits. */
    TickType_t decorrido = (TickType_t)(agora - anterior);
    anterior = agora;
    atualizarPrazos(r, decorrido);
    fracaoSegundo += decorrido;
    while (fracaoSegundo >= ticksMs(1000)) {
      fracaoSegundo -= ticksMs(1000);
      avancarSegundo(r);
      avaliarAlarme(r);
    }

    uint32_t avisos = 0;
    if (xTaskNotifyWait(0, AVISO_FILA_CHEIA, &avisos, 0) == pdTRUE &&
        (avisos & AVISO_FILA_CHEIA)) r.avisoTicks = ticksSegundos(2);

    Evento evento;
    /* Limite por ciclo: uma rajada de entradas nao monopoliza o controlador. */
    for (uint8_t i = 0; i < TAM_FILA_EVENTOS; ++i) {
      if (xQueueReceive(filaEventos, &evento, 0) != pdPASS) break;
      tratarEvento(evento, r, edicao, fracaoSegundo);
    }
    avaliarAlarme(r);
    bool tocar = r.toqueTicks != 0;
    if (tocar != alarmeAnterior) {
      xQueueOverwrite(filaAlarme, &tocar);
      alarmeAnterior = tocar;
    }
    if ((TickType_t)(agora - ultimaTela) >= ticksMs(100)) {
      publicarTela(r, edicao);
      ultimaTela = agora;
    }
    /* Agenda absoluta: tempo de processamento nao e somado ao periodo. */
    vTaskDelayUntil(&ultima, ticksMs(20));
  }
}

void TaskAlarme(void *parametros) {
  (void)parametros;
  bool ativo = false, fase = false;
  for (;;) {
    bool comando;
    /* Desligado dorme na fila; ligado acorda por comando ou fim do pulso. */
    if (xQueueReceive(filaAlarme, &comando, ativo ? ticksMs(200) : portMAX_DELAY) == pdPASS) {
      ativo = comando;
      fase = ativo;
    } else fase = !fase;
    if (ativo && fase) tone(BUZZER, 2000);
    else noTone(BUZZER);
    digitalWrite(LED_ALARME, ativo && fase ? HIGH : LOW);
  }
}

void escrever2(char *linha, uint8_t pos, uint8_t numero) {
  linha[pos] = '0' + numero / 10;
  linha[pos + 1] = '0' + numero % 10;
}

void formatarTela(const Tela &t, char *l0, char *l1, uint8_t &coluna) {
  memset(l0, ' ', 16); memset(l1, ' ', 16);
  l0[16] = l1[16] = '\0';
  coluna = 0;
  if (t.tocando) {
    memcpy(l0, "! ALARME TOCANDO", 16);
    memcpy(l1, "D5/SEL Zz 1s:FIM", 16);
  } else if (t.emSoneca) {
    memcpy(l0, "Zz SONECA ATIVA", 14);
    memcpy(l1, "RESTA ", 6);
    escrever2(l1, 6, t.sonecaSegundos / 60); l1[8] = ':';
    escrever2(l1, 9, t.sonecaSegundos % 60);
    memcpy(l1 + 12, "D5:X", 4);
  } else if (t.avisoEntrada) {
    memcpy(l0, "REPITA O COMANDO", 16);
    memcpy(l1, "Entrada ocupada", 15);
  } else if (t.editando) {
    static const char * const rotulos[] = {"HORA", "MIN", "DIA", "MES", "ANO", "AL-H", "AL-M", "SONECA"};
    memcpy(l1, rotulos[t.campo], strlen(rotulos[t.campo]));
    memcpy(l1 + 7, "X<>Y+-OK", 8);
    if (t.campo <= 1) {
      memcpy(l0, "HORA ", 5);
      escrever2(l0, 5, t.cfg.hora); l0[7] = ':';
      escrever2(l0, 8, t.cfg.minuto);
      coluna = t.campo == 0 ? 5 : 8;
    } else if (t.campo <= 4) {
      memcpy(l0, "DATA ", 5);
      escrever2(l0, 5, t.cfg.dia); l0[7] = '/';
      escrever2(l0, 8, t.cfg.mes); l0[10] = '/';
      escrever2(l0, 11, t.cfg.ano / 100);
      escrever2(l0, 13, t.cfg.ano % 100);
      coluna = t.campo == 2 ? 5 : t.campo == 3 ? 8 : 11;
    } else if (t.campo <= 6) {
      memcpy(l0, "ALARME ", 7);
      escrever2(l0, 7, t.cfg.alarmeHora); l0[9] = ':';
      escrever2(l0, 10, t.cfg.alarmeMinuto);
      coluna = t.campo == 5 ? 7 : 10;
    } else {
      memcpy(l0, "SONECA ", 7);
      escrever2(l0, 7, t.cfg.sonecaMinutos);
      memcpy(l0 + 10, "min", 3);
      coluna = 7;
    }
  } else {
    escrever2(l0, 0, t.cfg.hora); l0[2] = ':';
    escrever2(l0, 3, t.cfg.minuto); l0[5] = ':';
    escrever2(l0, 6, t.segundo);
    memcpy(l0 + 9, t.alarmeLigado ? "AL" : "--", 2);
    escrever2(l0, 11, t.cfg.alarmeHora); l0[13] = ':';
    escrever2(l0, 14, t.cfg.alarmeMinuto);
    escrever2(l1, 0, t.cfg.dia); l1[2] = '/';
    escrever2(l1, 3, t.cfg.mes); l1[5] = '/';
    escrever2(l1, 6, t.cfg.ano / 100);
    escrever2(l1, 8, t.cfg.ano % 100);
    memcpy(l1 + 12, t.alarmeLigado ? "ON " : "OFF", 3);
  }
}

void TaskDisplay(void *parametros) {
  (void)parametros;
  Tela tela;
  char l0[17], l1[17], anterior0[17] = "", anterior1[17] = "";
  bool cursorVisivel = false;
  for (;;) {
    if (xQueueReceive(filaTela, &tela, portMAX_DELAY) != pdPASS) continue;
    uint8_t coluna;
    formatarTela(tela, l0, l1, coluna);
    /* So esta tarefa acessa o LCD apos setup. Nenhum mutex ou secao critica. */
    if (strcmp(l0, anterior0)) {
      lcd.setCursor(0, 0); lcd.print(l0); strcpy(anterior0, l0);
    }
    if (strcmp(l1, anterior1)) {
      lcd.setCursor(0, 1); lcd.print(l1); strcpy(anterior1, l1);
    }
    bool mostrarCursor = tela.editando && !tela.tocando && !tela.emSoneca && !tela.avisoEntrada;
    if (mostrarCursor != cursorVisivel) {
      if (mostrarCursor) lcd.cursor(); else lcd.noCursor();
      cursorVisivel = mostrarCursor;
    }
    if (mostrarCursor) lcd.setCursor(coluna, 0);
  }
}

void erroInicial(const char *recurso) {
  /* Apenas setup, antes do escalonador. Nao tenta iniciar sem recursos. */
  noTone(BUZZER);
  digitalWrite(LED_ALARME, LOW);
  lcd.clear(); lcd.print("ERRO INICIAL");
  lcd.setCursor(0, 1); lcd.print(recurso);
  for (;;) {}
}

void setup() {
  pinMode(JOY_SEL, INPUT_PULLUP);
  pinMode(BOTAO_SONECA, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_ALARME, OUTPUT);
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_ALARME, LOW);
  lcd.begin(16, 2);
  lcd.print("Alarme RTOS MEGA");

  /* Alocacao apenas na inicializacao. Filas e tarefas vivem ate o reset. */
  filaEventos = xQueueCreate(TAM_FILA_EVENTOS, sizeof(Evento));
  filaAlarme = xQueueCreate(1, sizeof(bool));
  filaTela = xQueueCreate(1, sizeof(Tela));
  if (!filaEventos || !filaAlarme || !filaTela) erroInicial("FILA");

  /* AVR: StackType_t vale 1 byte. Total solicitado: 1344 bytes + kernel/heap. */
  if (xTaskCreate(TaskRelogio, "Relogio", 448, NULL, 2, &tarefaRelogio) != pdPASS)
    erroInicial("TASK RELOGIO");
  if (xTaskCreate(TaskBotoes, "Botoes", 256, NULL, 2, &tarefaBotoes) != pdPASS)
    erroInicial("TASK BOTOES");
  if (xTaskCreate(TaskAlarme, "Alarme", 256, NULL, 3, &tarefaAlarme) != pdPASS)
    erroInicial("TASK ALARME");
  if (xTaskCreate(TaskDisplay, "Display", 384, NULL, 1, &tarefaDisplay) != pdPASS)
    erroInicial("TASK DISPLAY");
  /* A integracao Arduino_FreeRTOS inicia o escalonador depois de setup(). */
}

void loop() {}
