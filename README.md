# BC-250 (Cyan Skillfish) Windows Driver Project 🚀

![Architecture](https://img.shields.io/badge/Architecture-GFX10-red)
![OS](https://img.shields.io/badge/OS-Windows_10%2F11-blue)
![Model](https://img.shields.io/badge/WDDM-Model_Full-green)

## 📌 Project Overview
This project is a specialized research implementation of a Windows Display Driver Model (WDDM) driver for the **AMD BC-250 (Cyan Skillfish)** GPU. The primary objective is to transition the device from a basic "detected" state to a fully functional acceleration state by implementing the GFX10 command pipeline.

The driver implements the full path from User Mode (UMD) command generation to Kernel Mode (KMD) hardware submission, utilizing a 4-level GPU Virtual Memory system and Command Processor (CP) integration.

## 🛠️ Technical Architecture

### 1. User Mode Driver (UMD)
- **PM4 Generation Engine**: Implements a binary packet generator aligned with **Mesa 26.1 / GFX10 ISA**.
- **Command Streams**: Capable of generating `S_CLEAR_COLOR`, `SET_RENDER_TARGET`, `SET_PIPELINE_STATE`, and `DRAW_INDEXED` packets.
- **Validation Tracing**: Integrated binary dump system (`.bin`) for cross-verification with Linux `amdgpu` traces.

### 2. Kernel Mode Driver (KMD)
- **Boot Sequence**: Centralized hardware initialization: `Discovery` $\rightarrow$ `SMN Probe` $\rightarrow$ `PSP Handshake` $\rightarrow$ `MMIO Enable`.
- **Command Processor (CP)**: Transitioned from SDMA-only to a full CP submission pipeline, allowing for real graphics processing.
- **Interrupt System**: Implemented a high-performance ISR/DPC routine for hardware fence synchronization and WDDM scheduler notification.

### 3. Memory Management (UMA/GPUVM)
- **4-Level Page Tables**: Implemented a complete L0 $\rightarrow$ L1 $\rightarrow$ L2 $\rightarrow$ L3 GPUVM system to support 48-bit virtual addressing.
- **Dynamic Residency**: Integrated with `VidMm` to update Page Table Entries (PTEs) in real-time as buffers move in system RAM.
- **Tiered Heap Manager**: Manages memory across `Local (Carve-out)`, `Aperture`, and `System` segments to optimize bandwidth.

## 🚀 Implementation Milestones
- [x] PCI Identification & BAR Mapping (GC_BASE shift implemented).
- [x] PSP Firmware Orchestration & Attestation.
- [x] Full 4-Level GPUVM implementation.
- [x] CP-based Command Submission pipeline.
- [x] ISR/DPC Interrupt routine for fence updates.
- [x] Mesa-aligned PM4 command generation.

## 📜 Credits & Acknowledgments
This project is a result of collective research and reverse engineering efforts. Special thanks to:
- **Keshas-dev**: For critical real-hardware bring-up fixes and PSP communication insights.
- **The Mesa/amdgpu Community**: For the GFX10 ISA specifications and open-source driver implementations.
- **Linux Kernel Contributors**: For the BC-250 IP offset definitions.

## ⚠️ Disclaimer
**This is a research prototype.** It is intended for educational purposes and hardware bring-up analysis. Use with caution on real hardware as it interacts directly with kernel-mode memory and GPU registers.

---

# Driver de Windows BC-250 (Cyan Skillfish) 🚀 (Português)

## 📌 Visão Geral do Projeto
Este projeto é uma implementação de pesquisa de um driver WDDM (Windows Display Driver Model) para a GPU **AMD BC-250 (Cyan Skillfish)**. O objetivo principal é levar o dispositivo de um estado básico de "detecção" para um estado de aceleração funcional, implementando o pipeline de comandos GFX10.

## 🛠️ Arquitetura Técnica

### 1. Driver de Modo Usuário (UMD)
- **Engine de Geração PM4**: Implementa a geração de pacotes binários alinhados com a **ISA GFX10 / Mesa 26.1**.
- **Streams de Comandos**: Capaz de gerar pacotes `S_CLEAR_COLOR`, `SET_RENDER_TARGET`, `SET_PIPELINE_STATE` e `DRAW_INDEXED`.
- **Tracer de Validação**: Sistema de dump binário integrado (`.bin`) para verificação cruzada com traces do `amdgpu` no Linux.

### 2. Driver de Modo Kernel (KMD)
- **Sequência de Boot**: Inicialização centralizada do hardware: `Descoberta` $\rightarrow$ `Sonda SMN` $\rightarrow$ `Handshake PSP` $\rightarrow$ `Habilitar MMIO`.
- **Command Processor (CP)**: Transição do SDMA para um pipeline de submissão via CP, permitindo o processamento gráfico real.
- **Sistema de Interrupções**: Implementação de rotina ISR/DPC de alta performance para sincronização de fences e notificação do escalonador WDDM.

### 3. Gerenciamento de Memória (UMA/GPUVM)
- **Tabelas de Páginas de 4 Níveis**: Implementação completa de L0 $\rightarrow$ L1 $\rightarrow$ L2 $\rightarrow$ L3 para suporte a endereçamento virtual de 48 bits.
- **Residência Dinâmica**: Integração com `VidMm` para atualizar as entradas de página (PTEs) em tempo real conforme buffers se movem na RAM.
- **Gestor de Heaps em Camadas**: Gerencia a memória entre segmentos `Local (Carve-out)`, `Aperture` e `System` para otimizar a largura de banda.

## 🚀 Marcos de Implementação
- [x] Identificação PCI & Mapeamento de BAR (Shift de GC_BASE implementado).
- [x] Orquestração de Firmware PSP & Attestation.
- [x] Implementação completa de GPUVM de 4 níveis.
- [x] Pipeline de submissão baseado em CP.
- [x] Rotina de Interrupção ISR/DPC para atualização de fences.
- [x] Geração de comandos PM4 alinhados ao Mesa.

## 📜 Créditos e Agradecimentos
Este projeto é resultado de esforços coletivos de pesquisa e engenharia reversa. Agradecimentos especiais a:
- **Keshas-dev**: Pelas correções críticas de bring-up em hardware real e insights de comunicação do PSP.
- **Comunidade Mesa/amdgpu**: Pelas especificações de ISA GFX10 e implementações de drivers open-source.
- **Contribuidores do Kernel Linux**: Pelas definições de offsets de IP do BC-250.

## ⚠️ Aviso Legal
**Este é um protótipo de pesquisa.** Destina-se a fins educacionais e análise de bring-up de hardware. Use com cautela em hardware real, pois interage diretamente com a memória do kernel e registradores da GPU.
