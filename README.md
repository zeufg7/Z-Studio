# Zeufg Studio

DAW (estação de trabalho de áudio digital) para **Windows e macOS**, inspirada no melhor do **Logic Pro** e do **Cubase**, feita por Eliseu F. Guedes.

Construída em C++ com [JUCE](https://juce.com) e [Tracktion Engine](https://github.com/Tracktion/tracktion_engine).

> Versão atual: **0.1** (primeira versão utilizável: arranjo, gravação, plugins e exportação)

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
1. Descompacte e abra `Zeufg Studio.exe`.
2. Se o Windows mostrar "O Windows protegeu o computador", clique em **Mais informações → Executar assim mesmo** (o app ainda não tem assinatura digital).
3. Em **Opções → Áudio e MIDI**, escolha sua interface de áudio e ative o teclado MIDI.
4. Em **Opções → Gerenciar plugins**, clique em **Options → Scan for new or updated VST3 plug-ins** para carregar seus plugins (Kontakt etc.).

### macOS (11 Big Sur ou mais novo, Intel ou Apple Silicon)
1. Descompacte e arraste `Zeufg Studio.app` para a pasta **Aplicativos**.
2. Na primeira vez, clique com o **botão direito → Abrir** (o app ainda não é notarizado pela Apple).
3. Permita o acesso ao **microfone** se for gravar áudio.
4. Escaneie os plugins em **Opções → Gerenciar plugins** (AU e VST3).

---

## Como compilar

Requisitos: CMake 3.22+, e **Visual Studio 2022** (Windows) ou **Xcode 13+** (macOS).

```bash
git clone --recursive https://github.com/zeufg7/zeufg-studio.git
cd zeufg-studio
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

O app fica em `build/ZeufgStudio_artefacts/Release/`.

A cada envio para o GitHub, o **GitHub Actions** compila sozinho as versões de Windows e Mac. Para publicar uma versão, crie uma tag (ex.: `v0.1.0`).

---

## Próximas versões

- **v0.2** – Piano roll completo, mixer com sends/aux, automação, ASIO no Windows
- **v0.3** – Expression Maps (articulações), Chord Track, Smart Controls, Track Stacks
- **v0.4** – Vídeo sincronizado, marcadores e templates de trilha sonora
- **Depois** – Editor de partitura, Flex Time e modo ao vivo

## Licença

Código sob **GNU AGPLv3** (veja `LICENSE`), porque usa JUCE (AGPLv3) e Tracktion Engine (GPLv3).
Para vender o programa com código fechado, é preciso comprar as licenças comerciais do JUCE e do Tracktion Engine.
