#pragma once
#include <stdint.h>

namespace relogio {
constexpr uint32_t TICKS_SEGUNDO = 100;
constexpr uint8_t CAMPOS = 9;
enum class Evento : uint8_t {
  Selecionar,
  Encerrar,
  Soneca,
  Esquerda,
  Direita,
  Mais,
  Menos
};
enum class Aviso : uint8_t { Silencio, Tocando, Adiado };
struct DataHora {
  uint16_t ano;
  uint8_t mes, dia, hora, minuto, segundo;
};
struct Alarme {
  uint8_t hora, minuto, sonecaMinutos;
  bool habilitado;
};
struct Edicao {
  DataHora data;
  Alarme alarme;
  uint8_t campo;
  bool ativa, mudouHora, mudouData;
};
struct Estado {
  DataHora agora;
  Alarme alarme;
  Edicao edicao;
  Aviso aviso;
  uint32_t restante, ultimaOcorrencia;
  uint16_t fracao, ajustesHora;
};

Estado estadoInicial();
uint8_t diasNoMes(uint16_t ano, uint8_t mes);
void avancar(Estado &e, uint32_t ticks);
void comandar(Estado &e, Evento evento);
// Preenche exatamente 16 caracteres + '\0'. cursor=-1 desativa sublinhado.
void renderizar(const Estado &e, char (&linha0)[17], char (&linha1)[17], int8_t &cursor);
} // namespace relogio


#include <stdint.h>
// Unidade: contagens de 10 ms da base de tempo. Sem espera pela soltura.
struct Botao {
  bool anterior = false, estavel = false, longoEmitido = false;
  uint32_t mudou = 0, pressionou = 0;
  // 0: nada; 1: clique; 2: longo. SEL emite curto na soltura; D5 no aperto.
  uint8_t ler(bool baixo, uint32_t agora, bool permiteLongo) {
    if (baixo != anterior) {
      anterior = baixo;
      mudou = agora;
    }
    if (baixo != estavel && (uint32_t)(agora - mudou) >= 5) {
      estavel = baixo;
      if (baixo) {
        pressionou = agora;
        longoEmitido = false;
        if (!permiteLongo)
          return 1;
      } else if (permiteLongo && !longoEmitido)
        return 1;
    }
    if (permiteLongo && estavel && baixo && !longoEmitido &&
        (uint32_t)(agora - pressionou) >= 100) {
      longoEmitido = true;
      return 2;
    }
    return 0;
  }
};
struct Eixo {
  int8_t anterior = 0;
  uint32_t repetiu = 0;
  int8_t ler(int8_t sentido, uint32_t agora) {
    if (!sentido) {
      anterior = 0;
      return 0;
    }
    if (sentido != anterior || (uint32_t)(agora - repetiu) >= 35) {
      anterior = sentido;
      repetiu = agora;
      return sentido;
    }
    return 0;
  }
};
