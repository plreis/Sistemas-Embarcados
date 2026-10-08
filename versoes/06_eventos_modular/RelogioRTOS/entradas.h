#pragma once
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
