# Převzaté části

## JOndra

- Zdroj: https://github.com/omikron88/jondra
- Revize: `69924d6caea4e852db4ff8eb2cf2ee109c77adc6`
- Licence projektu: GNU GPL v3, text v kořenovém `LICENSE`.
- Autor repozitáře: omikron88; zdrojové soubory uvádějí také tmilata a další
  původní přispěvatele.
- Převzatá data beze změn: `src/roms/*.rom` → `assets/roms/`,
  `src/sound/*.sample` → `assets/sound/`.
- Původní PNG ikony a obrázek klávesnice z `src/icons/` jsou v `assets/ui/`
  bezeztrátově převedené do RGBA (šířka a výška LE32, poté pixely RGBA).
  Jsou vložené do EXE, takže nevyžadují další knihovnu pro dekódování PNG.
- Ondra+ ROM má navíc licenci MIT, copyright © 2023 72ka; původní licenční
  text je zachován v `assets/roms/LICENSE-PLUS`.
- Z Java implementace jsou odvozeny mapování a časování počítače, adresování
  obrazu, matice klávesnice, převod TAP, formát OSN a protokol OndraSD.
- Centronics a Tesla BT100 jsou převedené z `machine/Printer.java`,
  `machine/BT100.java` a jejich zapojení v revizi
  `7aad67efd76badfa6b82835bd1285aa7c9bedeb1` (GPLv3). Zachovávají výchozí
  časování, polaritu snímačů, dráhu vozíku a korekci zpětného chodu.
- Oprava 64µs pulsu INT a zrcadlení I/O po 16 bajtech odpovídá revizi
  `94b8675486fd00179c965cbddbc61dc1ed683755`.
- Registry DS1307, bateriový stav a navazující změny protokolu OndraSD jsou
  odvozené z `machine/OndraSD.java` v revizi
  `5b3c850e63a8573ea1a24ae3a8e14b6f91f1ce38` (GPLv3). Testy porovnávají
  chování hodin s touto Java implementací při řízeném zdroji času.
- Java CPU, Swing UI ani `machine/SN76489.java` nejsou součástí tohoto portu.
  Poslední z těchto souborů má vlastní nekomerční licenci; zdejší `Psg` je nová
  implementace hardwarových registrů a čítačů, nepřebírá jeho implementaci.

## Z80

- Zdroj: https://github.com/redcode/Z80
- Revize: `1d5e1a248594493bb380d868d59846a3b299c22c`
- Copyright © 1999–2026 Manuel Sainz de Baranda y Goñi.
- Licence: LGPL-3.0-or-later.
- Soubory: `third_party/z80/Z80.c`, `Z80.h`, `COPYING`, `COPYING.LESSER`,
  zkopírované beze změn. CMake je sestavuje jako statickou C99 knihovnu se
  zapnutou emulací Q a NMOS LD A,I/R chybou.

## Zeta

- Zdroj: https://github.com/redcode/Zeta
- Revize: `93ba5ab967eef00f074d21bb760fe9dc48afd2d3`
- Autor: Manuel Sainz de Baranda y Goñi.
- Licence: LGPL v3, viz hlavičky a `third_party/zeta/COPYING.LESSER`.
- Soubory: kompletní nezměněné `API/` a oba licenční texty. Z80 používá tuto
  hlavičkovou knihovnu pro typy a přenositelnost.

## SDL2 (externí závislost)

- Zdroj: https://github.com/libsdl-org/SDL
- Licence: zlib.
- Běžné sestavení používá systémovou instalaci; není vendored.
- Volitelný FetchContent používá SDL 2.32.10, revizi
  `5d249570393f7a37e037abf22cd6012a4cc56a71` a SHA-256 archivu
  `10f1194f8d2e4a73ca1c7c553b3189d0b68ab9ef3544e8d2a268e4429456b373`.
- Při redistribuci binárních knihoven zachovejte jejich licenční texty
  a požadavky jednotlivých knihoven. GPL/LGPL zdrojové části jsou přiložené.

## Dear ImGui

- Zdroj: https://github.com/ocornut/imgui
- Verze: `v1.91.9b`, revize `f5befd2d29e66809cd1110a152e375a7f1981f06`.
- Copyright © 2014–2025 Omar Cornut a přispěvatelé.
- Licence: MIT, viz `third_party/imgui/LICENSE.txt`.
- Nezměněné jádro a backendy SDL2 / SDLRenderer2 jsou v `third_party/imgui/`.
  CMake je sestavuje staticky; nevyžadují OpenGL ani další DLL.
- Zdrojový kód obsahuje také výchozí ProggyClean font, copyright © 2004–2005
  Tristan Grimmer, se stejnou licencí MIT. Rozhraní používá Roboto níže.

## Roboto Medium

- Zdroj: `misc/fonts/Roboto-Medium.ttf` v uvedené revizi Dear ImGui.
- Copyright 2011 Google Inc. All Rights Reserved.
- Licence: Apache-2.0, viz `assets/ui/LICENSE-Roboto` a `NOTICE-Roboto`.
- Font je nezměněný a vložený do EXE; používá se i pro české názvy souborů.
