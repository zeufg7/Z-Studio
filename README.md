# Z-Studio

DAW (estação de trabalho de áudio digital) para **Windows e macOS**, inspirada no melhor do **Logic Pro** e do **Cubase**, feita por Eliseu F. Guedes.

Construída em C++ com [JUCE](https://juce.com), [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) e [Verovio](https://www.verovio.org).

> Versão atual: **0.2 (em desenvolvimento)** – nova aba **Partitura** com limpeza de MIDI e exportação MusicXML

---

## O que já funciona (v0.1)

- **Faixas de instrumento e de áudio**, com cor, nome, Mute/Solo/Rec, volume e medidor de nível
- **Teclado MIDI estilo Logic**: o seu controlador (ex.: Oxygen 61) toca automaticamente a faixa de instrumento selecionada, mesmo com o transporte parado
- **Gravação** de MIDI e de áudio (escolha a entrada no botão *In* das faixas de áudio)
- **Plugins VST3 e AU** (Mac) como instrumentos e como efeitos (inserts), com janela do próprio plugin
- Instrumentos e efeitos internos: sintetizador 4OSC, Sampler, EQ, Compressor, Reverb, Delay, Chorus, Phaser, Filtro e Pitch Shift
- **Janela de arranjo** com régua de compassos, grade com encaixe, zoom e rolagem
- **Clipes**: mover (inclusive para outra faixa), aparar as bordas, renomear e apagar
- **Duplo clique** numa faixa de instrumento cria um clipe MIDI de 4 compassos (como no Cubase)
- **Arrastar arquivos** de áudio (WAV, AIFF, FLAC, OGG, MP3) e **MIDI** direto para as faixas
- **Inspetor** lateral (estilo Logic): instrumento, inserts, pan, volume e master
- **Transporte** com visor de compassos e tempo, BPM, fórmula de compasso, **Cycle/loop** e **metrônomo**
- **Desfazer/Refazer**, salvar e abrir projetos (`.zstudio`)
- **Exportar mixagem** em WAV

## Piano roll (novo na v0.2)

Abre na parte de baixo do Arranjo, como no Logic: **duplo clique** num clipe MIDI ou tecla **E** (ou **P**).
- **Faixas selecionadas** (clipes selecionados ou todos os clipes da faixa) ou **Global** (todas as faixas, cada uma com sua cor)
- Ferramentas **Ponteiro**, **Lápis** e **Borracha**; duplo clique cria/apaga nota
- Mover, redimensionar (pelas duas bordas), seleção por retângulo, Shift para somar à seleção
- Linha de **velocity** (arraste para mudar; vale para todas as notas selecionadas)
- Grade 1/4 a 1/32 e tercinas, **Quantizar** (Q), teclado lateral que toca a nota
- Atalhos: Delete apaga · Ctrl/Cmd+A seleciona tudo · ↑/↓ transpõe (Shift = oitava) · ←/→ move pela grade · Esc limpa a seleção
- Notas escritas fora de um clipe criam um clipe novo de 1 compasso na faixa ativa

## Aba Partitura (novo na v0.2)

A partitura é uma **interpretação** do MIDI: o MIDI continua exatamente como você tocou (humano, sem quantizar) e a limpeza vale só para a leitura. Abra com **N** (ou Ctrl/Cmd + R, ou Ctrl/Cmd + 2).

**Painel "Limpeza do MIDI"** (prévia ao vivo a cada ajuste):
- **Ritmo:** grade dos ataques e das durações (1/4 a 1/64), tercinas automáticas com sensibilidade, escrita de síncopes (moderna ou rígida), pontos de aumento
- **Execução humana:** tolerância de acorde (em ms), arredondamento dos finais de legato, fechar pausas pequenas, ignorar notas-fantasma e velocity baixa
- **Vozes e pautas:** vozes automáticas ou acordes, uma pauta ou piano (duas pautas), divisão das mãos inteligente ou por ponto fixo
- **Tonalidade e cifras:** detecção automática ou escolhida, grafia enarmônica pela tonalidade, **cifras automáticas** (por compasso, meio compasso ou tempo)
- **Articulações:** staccato deduzido da duração tocada, acentos pela velocity
- **Pontos de partida:** Equilibrado, Piano, Melodia, Rubato e Rígido
- **Escopo:** ajustes do projeto inteiro ou próprios de cada faixa (salvos no projeto)

**Exportar MusicXML** (Arquivo → Exportar partitura, ou Ctrl/Cmd + Shift + E): MusicXML 4.0 com vozes, duas pautas, ligaduras, tercinas, barras, cifras e articulações, pronto para Dorico, Sibelius, MuseScore e Finale.

A partitura é desenhada pelo [Verovio](https://www.verovio.org) com a fonte Bravura (SMuFL).

## Atalhos

| Ação | Atalho |
|---|---|
| Tocar / Parar | Espaço |
| Gravar | R  ou  * (teclado numérico) |
| Voltar ao início | Enter  ou  Home |
| Cycle (loop) | C |
| Metrônomo | K |
| Encaixar na grade | J |
| Zoom + / − | H / G  ou  Ctrl(Cmd) + = / − |
| Mostrar/ocultar inspetor | I |
| Piano roll | E  ou  P  (ou duplo clique no clipe) |
| Partitura / Arranjo | N  ou  Ctrl(Cmd) + R |
| Exportar MusicXML | Ctrl(Cmd) + Shift + E |
| Nova faixa de instrumento | Ctrl(Cmd) + T |
| Nova faixa de áudio | Ctrl(Cmd) + Shift + T |
| Importar áudio/MIDI | Ctrl(Cmd) + I |
| Exportar mixagem | Ctrl(Cmd) + E |
| Desfazer / Refazer | Ctrl(Cmd) + Z / Ctrl(Cmd) + Shift + Z |
| Apagar seleção | Delete / Backspace |

**Mouse na janela de arranjo:** roda = rolar faixas · Shift + roda = rolar no tempo · Ctrl(Cmd) + roda = zoom · Alt + roda = altura das faixas.
**Régua:** clique/arraste embaixo = posicionar o cursor · arraste na faixa de cima = definir o Cycle · clique na faixa de cima = liga/desliga o Cycle.

---

## Como instalar

Baixe o `.zip` mais recente na página **Actions** (ou em **Releases**) deste repositório.

### Windows
1. Descompacte e abra `Z-Studio.exe`.
2. Se o Windows mostrar "O Windows protegeu o computador", clique em **Mais informações → Executar assim mesmo** (o app ainda não tem assinatura digital).
3. Em **Opções → Áudio e MIDI**, escolha sua interface de áudio e ative o teclado MIDI.
4. Em **Opções → Gerenciar plugins**, clique em **Options → Scan for new or updated VST3 plug-ins** para carregar seus plugins (Kontakt etc.).

### macOS (11 Big Sur ou mais novo, Intel ou Apple Silicon)
1. Descompacte e arraste `Z-Studio.app` para a pasta **Aplicativos**.
2. Na primeira vez, clique com o **botão direito → Abrir** (o app ainda não é notarizado pela Apple).
3. Permita o acesso ao **microfone** se for gravar áudio.
4. Escaneie os plugins em **Opções → Gerenciar plugins** (AU e VST3).

---

## Como compilar

Requisitos: CMake 3.22+, e **Visual Studio 2022** (Windows) ou **Xcode 13+** (macOS).

```bash
git clone --recursive https://github.com/zeufg7/Z-Studio.git
cd Z-Studio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

O app fica em `build/ZStudio_artefacts/Release/`.

A cada envio para o GitHub, o **GitHub Actions** compila sozinho as versões de Windows e Mac. Para publicar uma versão, crie uma tag (ex.: `v0.1.0`).

---

## Próximas versões

- **v0.2** – Aba Partitura ✅ · próximos: ajustes por trecho, correção nota a nota (grafia, voz, ligadura) sem mexer no MIDI, piano roll completo
- **v0.3** – Expression Maps (keyswitches viram spiccato, marcato, legato… na partitura), Chord Track, Smart Controls, Track Stacks, mixer com sends, ASIO
- **v0.4** – Vídeo sincronizado, marcadores e templates de trilha sonora
- **Depois** – Editor de partitura, Flex Time e modo ao vivo

## Licença

Código sob **GNU AGPLv3** (veja `LICENSE`): software livre e de código aberto. Usa JUCE (AGPLv3), Tracktion Engine (GPLv3) e Verovio (LGPLv3).
