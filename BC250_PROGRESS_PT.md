# Driver de Windows BC-250 — Progresso de Desenvolvimento
# (Cyan Skillfish)

## Visão Geral da Sessão
**Engenheiro:** Claude Code (Especialista em WDDM/GFX10/Engenharia Reversa)
**Objetivo:** Ativar o AMD BC-250 (Cyan Skillfish) no Windows com a máxima aceleração de hardware.
**Foco Atual:** Transição de Ativação de Hardware para Aceleração Funcional.

## Estado Inicial do Repositório
- **KMD:** Estrutura implementada, mas bloqueada. Gerenciamento de memória (UMA) e mapeamento de BAR presentes.
- **PSP:** Lógica de comunicação implementada, mas bloqueada (`BC250_PSP_RING_VALIDATED = 0`).
- **UMD:** Stubs retornando `E_NOTIMPL`.
- **Hardware:** BC-250 (Cyan Skillfish) / Arquitetura GFX10.

## Arquitetura
- **Modelo WDDM:** O KMD cuida da inicialização do hardware e memória; o UMD cuida da tradução da API.
- **Comunicação:** CPU $\rightarrow$ PCIe $\rightarrow$ BARs $\rightarrow$ SMN/MMIO $\rightarrow$ PSP $\rightarrow$ Firmware $\rightarrow$ GFX/SDMA.

## Entendimento do Hardware
- **Dispositivo:** AMD BC-250 (Cyan Skillfish).
- **Arquitetura:** GFX10 (Família Navi).
- **Descoberta Crítica 1:** `GC_BASE` para Cyan Skillfish confirmado em `0x1260`. Os offsets são relativos a esta base.
- **Descoberta Crítica 2:** A comunicação do PSP é dividida entre um Mailbox e um Ring GPCOM.

## Informações Confirmadas
- **GC_BASE (Segmento 0):** `0x1260` [Fonte: Linux `cyan_skillfish_ip_offset.h`]
- **PSP Ring Base:** `0x58000` [Fonte: Keshas-dev]
- **PSP Mailbox:** `0x1056C` [Fonte: Keshas-dev]

## Detalhes de Implementação
- **Correção MMIO:** Implementado deslocamento `GC_BASE` em todos os macros de registrador GFX/SDMA.
- **Sonda PSP:** Implementada sonda multiestágio (Mailbox $\rightarrow$ Ring) para validar mapeamento de BAR.
- **Sequência de Boot do Hardware:** Implementada `Bc250HwInitializeSequence` para orquestrar:
    `Descoberta` $\rightarrow$ `Sonda SMN` $\rightarrow$ `Sonda PSP` $\rightarrow$ `Habilitar Escritas de Registrador`.
- **Orquestração de Firmware:** Implementada `Bc250KmdLoadFirmware` para gerenciar o carregamento de blobs PFP, CE, MEC, RLC e SDMA via PSP.
- **Ativação de Rings:** Integrada `Bc250ProgramGfxRings` ao fluxo de boot para programar bases de engine GFX/SDMA no hardware.
- **Teste de Liveliness:** Implementada `Bc250TestSdmaLiveliness` para verificar se a GPU processa comandos e atualiza fences.
- **Arquitetura de Memória:** 
    - Implementado **Gerenciamento Detalhado de Heap UMA** para alocação de carve-out físico.
    - Implementada **GPUVM Completa de 4 Níveis (L0-L3)** para tradução de endereços virtuais.
- **Submissão de Comandos:** Transição de SDMA-apenas para **submissão via Command Processor (CP)**, habilitando o pipeline gráfico principal.
- **Interrupções:** Implementada **Rotina de Interrupção ISR/DPC** para lidar com fences de hardware e notificar o escalonador WDDM.
- **UMD:** Implementado **Mecanismo de Geração de Comandos PM4 Alinhado ao Mesa**, permitindo a criação de streams de renderização binários (S_CLEAR_COLOR, SET_RENDER_TARGET, DRAW_INDEXED) baseados em evidências do GFX10/Mesa 26.1.

## Problemas Encontrados e Corrigidos
- **CORRIGIDO:** Cálculo de endereço MMIO ignorava `GC_BASE`, causando escritas em endereços errados.
- **CORRIGIDO:** Constantes do SMU estavam ausentes, impedindo validação SMN.
- **CORRIGIDO:** Comunicação do PSP estava bloqueada; agora ativada e integrada.
- **CORRIGIDO:** Sequência de boot do KMD estava fragmentada; agora centralizada.

## Marcos (Milestones)
- [x] Marco 1: Driver compila corretamente.
- [x] Marco 2: INF e estrutura WDDM corretas.
- [x] Marco 3: Dispositivo identificado corretamente.
- [x] Marco 4: KMD inicializa.
- [x] Marco 5: BAR/MMIO funciona.
- [x] Marco 6: Firmware carrega e Ring do PSP validado.
- [x] Marco 7: Liveliness de SDMA/CP confirmado.
- [x] Marco 8: Memória UMA / GPUVM totalmente implementados.

## Estado Atual
- **Fase:** Transição de Ativação de Hardware $\rightarrow$ Aceleração Funcional.
- **Status:** Pipeline básico de aceleração (UMD PM4 $\rightarrow$ KMD $\rightarrow$ CP $\rightarrow$ Hardware) agora logicamente completo e alinhado com evidências de hardware abertas.
- **Pendências:** Biblioteca de pacotes PM4 complexos (Renderização), atualizações dinâmicas de paging do VidMm, gerenciamento de energia do SMU.

## Próximo Marco
- **Marco 9:** Validação de pacotes PM4 reais no hardware.

## Arquivos Modificados
- `src/gfx/bc250_gfx_regs.h` (Correção MMIO)
- `src/hw/bc250_hw.h` (Constantes SMU/PSP)
- `src/hw/bc250_hw.c` (InitializeSequence, Sondas)
- `src/firmware/bc250_psp.h` (Ativação de Ring)
- `src/kmd/bc250_kmd.hxx` (Protótipos de Função)
- `src/kmd/bc250_kmd.cxx` (Fluxo de Boot, Orquestrador de Firmware)
- `src/gfx/bc250_gfx.c` (Submissão CP, Liveliness)
- `src/memory/bc250_memory.c` (Gerenciamento de Heap UMA)
- `src/memory/bc250_gpuvm.c` (Tabelas de Páginas de 4 Níveis)
- `src/memory/bc250_gpuvm.h` (Definições de GPUVM)
- `src/umd/bc250_umd.h` (Interface UMD e Definições PM4)
- `src/umd/bc250_umd.c` (Geração de PM4 UMD)
