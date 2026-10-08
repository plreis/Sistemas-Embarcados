#include "nucleo.h"
#include <string.h>

namespace relogio {
Estado estadoInicial() {
  Estado e = {};
  e.agora = {2026, 10, 8, 7, 58, 30};
  e.alarme = {7, 0, 5, false};
  return e;
}
uint8_t diasNoMes(uint16_t ano, uint8_t mes) {
  if (mes == 2)
    return (ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0)) ? 29 : 28;
  return (mes == 4 || mes == 6 || mes == 9 || mes == 11) ? 30 : 31;
}
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
static uint32_t chave(const DataHora &d) {
  const uint32_t dia = (uint32_t)(d.ano - 2000) * 372UL + (d.mes - 1) * 31UL + d.dia - 1;
  return dia * 1440UL + d.hora * 60UL + d.minuto + 1;
}
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
static uint8_t circular(uint8_t x, int8_t delta, uint8_t min, uint8_t max) {
  return delta > 0 ? (x == max ? min : x + 1) : (x == min ? max : x - 1);
}
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
static void dois(char *linha, uint8_t col, uint8_t valor) {
  linha[col] = '0' + valor / 10;
  linha[col + 1] = '0' + valor % 10;
}
static void horario(char *l, uint8_t col, const DataHora &d) {
  dois(l, col, d.hora);
  l[col + 2] = ':';
  dois(l, col + 3, d.minuto);
  l[col + 5] = ':';
  dois(l, col + 6, d.segundo);
}
static void data(char *l, const DataHora &d) {
  dois(l, 0, d.dia);
  l[2] = '/';
  dois(l, 3, d.mes);
  l[5] = '/';
  dois(l, 6, d.ano / 100);
  dois(l, 8, d.ano % 100);
}
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
