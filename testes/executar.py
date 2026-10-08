#!/usr/bin/env python3
"""Testa o nucleo extraido do proprio .ino entregue, sem manter outra implementacao."""
from pathlib import Path
import subprocess, tempfile
raiz = Path(__file__).resolve().parents[1]
firmware = raiz / "firmware/RelogioFinal"
texto = (firmware / "RelogioFinal.ino").read_text()
nucleo = texto.split("// INICIO_NUCLEO\n")[1].split("// FIM_NUCLEO")[0]
with tempfile.TemporaryDirectory(prefix="relogio-testes-") as pasta:
    origem = Path(pasta) / "nucleo.cpp"
    origem.write_text('#include "RelogioTipos.h"\n#include <string.h>\n' + nucleo)
    binario = Path(pasta) / "testes"
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(firmware), str(raiz / "testes/nucleo_teste.cpp"), str(origem), "-o", str(binario)], check=True)
    subprocess.run([str(binario)], check=True)
