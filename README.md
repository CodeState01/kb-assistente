# KB — assistente de voz local (C++)

Assistente de IA que abre com o Windows, **entende sua voz em português** e **responde falando**, rodando
100% no seu PC. Interface em Dear ImGui com animações fluidas, painéis acopláveis (docking) e janelas
flutuantes nativas.

**⬇ Download (detecta o seu aparelho):** https://codestate01.github.io/kb-assistente/

## Recursos

| Área | O que faz |
|---|---|
| **Orb animado** | 7 estados com transição suave: ocioso (respira), ouvindo (ondas + equalizador circular reagindo ao microfone), entendendo, pensando (órbitas com rastro), falando (ondas de voz sincronizadas aos visemas da fala), mostrando (anel tracejado + faíscas), erro |
| **Voz → texto** | whisper.cpp local, idioma `pt`, detecção de fala (VAD) com piso de ruído adaptativo |
| **Texto → voz** | SAPI do Windows (vozes OneCore, ex.: Microsoft Maria / Francisca), fala frase a frase enquanto a IA ainda está escrevendo |
| **IA local** | Usa o que já está instalado: **Ollama**, **LM Studio**, **llama.cpp server**, **Jan**, **KoboldCpp**, text-generation-webui, vLLM… ou um arquivo **.gguf** direto (via `llama-server`) |
| **Modelo de ~13 GB** | Recomendado: `gemma3:12b-it-q8_0` (~13 GB) — baixa pelo próprio app com barra de progresso. Sem escolha manual, a KB usa o maior modelo instalado que cabe na sua RAM |
| **Embeds** | A IA pode mostrar cartões (`card`), passo a passo (`passos`), avisos, dicas, tabelas e código. Cada um tem *copiar* e *destacar em janela flutuante* (com "sempre no topo") |
| **Janelas** | Conversa na janela principal; **Painel do KB** (Geral, IA e modelos, Voz, Celular, Skills, Itens) e **Timer** abrem como janelas flutuantes próprias do Windows |
| **Pesquisa na web** | A IA pesquisa na internet e lê páginas quando precisa de informação atual (ferramentas `web_search` / `open_url`), mostrando as fontes clicáveis na resposta |
| **Skills** | Instruções em arquivos `.md` (em `%APPDATA%\KB\skills`) que a IA carrega quando o pedido combina; dá para ativar, editar e criar pelo Painel |
| **Visão** | Anexe imagens ou transmita ao vivo **uma tela ou uma janela específica**, escolhida num seletor com miniaturas |
| **Timer de Muay Thai** | Placar grande por fase (preparar, luta, descanso), rounds, aviso dos últimos segundos, vários sinais (sino, gongo, apito…), treinos salvos e "KB, criar treino" por voz |
| **Sistema** | Inicia com o Windows (minimizada na bandeja), orb flutuante quando minimizada, atalho global (padrão **Ctrl+Shift+Espaço**), palavra de ativação opcional ("KB, que horas são?"), instância única |

## Compilar

Requisitos: Windows 10/11, **Visual Studio 2022 Build Tools** (C++), **CMake 3.20+**, internet na primeira
compilação (baixa GLFW, ImGui, whisper.cpp, miniaudio e nlohmann/json automaticamente).

```bat
build.bat
```

Para acelerar o reconhecimento de voz numa GPU NVIDIA (requer CUDA Toolkit): `build.bat cuda`.
O executável fica em `bin\KB.exe`.

## Primeiro uso

1. **IA**: instale o [Ollama](https://ollama.com) (ou abra seu LM Studio/llama.cpp). Na aba **Modelos**,
   clique em *Procurar servidores* e escolha um modelo — ou *Baixar recomendado* (~13 GB).
   Para modelos de 13 GB, o ideal é 16 GB+ de RAM ou uma GPU com 16 GB de VRAM.
2. **Voz**: na aba **Modelos > Reconhecimento de voz**, baixe o Whisper *small* (466 MB).
3. **Fala em português**: se não tiver voz pt-BR, instale em *Configurações do Windows > Hora e idioma >
   Idioma e região > Português (Brasil) > Opções > Fala*.
4. Em **Configurações**, ligue *Iniciar com o Windows*.

Depois é só clicar no orb, apertar **Ctrl+Shift+Espaço**, ou (com a palavra de ativação ligada) dizer
"**KB**, …".

## Usar um .gguf que você já tem

Aba **Modelos > Arquivos GGUF**: *Procurar arquivos* (varre LM Studio, Downloads, etc.) ou
*Escolher .gguf…*. A KB inicia o `llama-server.exe` (do [llama.cpp](https://github.com/ggml-org/llama.cpp/releases))
na porta 8089 e encerra junto com o app.

## Estrutura

```
src/
  main.cpp              janela sem moldura, bandeja, atalho global, loop de render, multi-viewport
  app.*                 máquina de estados (ouvindo → entendendo → pensando → falando), threads
  audio/                microfone (miniaudio) + VAD, whisper.cpp, SAPI com visemas
  llm/                  cliente Ollama / OpenAI-compatível (streaming), busca de servidores e .gguf
  net/http.*            WinHTTP com streaming e downloads
  net/web.*             pesquisa (DuckDuckGo) e leitura de páginas para a IA
  core/skills.*         skills em .md com frontmatter (name, description)
  app_tools.cpp         ferramentas que a IA pode chamar (pesquisa, skills, treinos, timer)
  media/screen.*        transmissão de tela/janela (DXGI, GDI, PrintWindow)
  ui/orb.*              o orb animado (gradientes radiais, blobs deformados por ruído, partículas)
  ui/embeds.*, blocks.* parser de markdown + cartões visuais + janelas flutuantes
  ui/views.*            telas: título, barra lateral, conversa, modelos, configurações, mini-orb
  ui/anim.*, widgets.*  sistema de animação (molas, easing) e componentes (toggle, botões, chips)
```

Dados do usuário (config, layout, modelos de voz) ficam em `%APPDATA%\KB`.

## Diagnóstico

Variáveis de ambiente úteis para testes automatizados:
`KB_SNAPSHOT=arquivo.bmp` (salva um print do framebuffer e de cada janela flutuante, sem fala),
`KB_SNAPSHOT_FRAME=150`, `KB_SNAPSHOT_EXIT=1`, `KB_DEMO=1` (conversa de exemplo),
`KB_MODE=0..6` (força um estado do orb), `KB_ASK="pergunta"`, `KB_POPOUT=1`.
