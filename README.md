# estacao-microclima-bmp280
# 🌱 Estação Preditiva de Microclima para Plantas Xerófitas

Projeto desenvolvido com **ESP32 + BMP280** para monitoramento de temperatura e pressão atmosférica, previsão simplificada de chuva baseada em tendência barométrica (algoritmo inspirado no **Zambretti**) e apoio à **decisão de irrigação** de plantas de clima árido, como cactos e suculentas.

Os dados são processados diretamente no microcontrolador e enviados para a nuvem através do **ThingSpeak**, onde podem ser visualizados em gráficos em tempo real.

---

## 👥 Integrantes

- Julia Leoni  
- Youssef Rodrigues

---

## 🎯 Objetivo

Desenvolver um sistema de **baixo custo e fácil implementação** capaz de:

- Monitorar condições climáticas locais (microclima).
- Identificar **tendências de mudança de tempo** a partir da variação da pressão atmosférica.
- Indicar automaticamente quando **regar ou não regar** plantas de clima árido, evitando:
  - excesso de irrigação,
  - apodrecimento das raízes,
  - desperdício de água.

---

## 🧠 Funcionamento do Sistema

O projeto utiliza um fluxo simples:

1. Leitura da **temperatura e pressão atmosférica** com o sensor BMP280.
2. Aplicação de **Filtro de Kalman** para reduzir ruídos nas leituras da pressão.
3. **Correção barométrica por altitude**, convertendo a pressão local para pressão ao nível do mar.
4. Análise da **tendência da pressão** comparando leituras atuais com as de horas anteriores.
5. Aplicação de um algoritmo inspirado no **método Zambretti**:
   - `0` → tempo estável (sol)
   - `1` → instabilidade/tempo de transição
   - `2` → alta probabilidade de chuva
6. Geração da **decisão de rega**:
   - **Não regar** se houver previsão de chuva.
   - **Regar permitido** caso contrário.
7. Envio dos dados para o **ThingSpeak**.
8. Visualização por meio de gráficos online.
9. O dispositivo entra em **Deep Sleep** para economia de energia.

---

## ⚙️ Componentes Utilizados

### Hardware
- **ESP32 DevKit**
- **BMP280** (sensor de pressão e temperatura)
- Cabo USB para alimentação

### Software
- Arduino IDE
- Bibliotecas:
  - `Adafruit_BMP280`
  - `WiFi.h`
  - `HTTPClient.h`
  - `time.h`
- Plataforma de nuvem:
  - **ThingSpeak**

---

## ☁️ Monitoramento em Nuvem

Os dados coletados são enviados para o ThingSpeak e organizados em gráficos:

- **Temperatura (°C)**
- **Pressão atmosférica corrigida (hPa)**
- **Classificação Zambretti (0, 1 ou 2)**
- **Decisão automática de rega**

Isso permite o acompanhamento remoto em tempo real por celular ou computador.

---

## 🌿 Impacto do Projeto

Este sistema contribui para:

- Agricultura urbana inteligente.
- Economia de água.
- Redução de perdas de plantas sensíveis ao excesso de umidade.
- Democratização do uso de **IoT aplicada à sustentabilidade**.

---

## 🚀 Como utilizar o projeto

1. Monte o circuito com o ESP32 e o BMP280 via barramento I²C.
2. Configure no código:
   - SSID e senha da rede Wi‑Fi.
   - Chave de API do ThingSpeak.
3. Envie o código para o ESP32 via Arduino IDE.
4. Acesse o painel do ThingSpeak para visualizar os dados.

---

## 🧪 Principais conceitos utilizados

### 🔹 Filtro de Kalman
Método matemático para reduzir ruídos dos sensores e estimar valores mais confiáveis de pressão.

### 🔹 Algoritmo Zambretti (simplificado)
Modelo clássico de previsão meteorológica baseado em:
- valor da pressão atmosférica,
- tendência de subida ou queda da pressão,
- ajuste sazonal.

---

## ✅ Resultados

O protótipo demonstrou:

- Funcionamento estável do sensor.
- Previsão coerente de instabilidades climáticas.
- Visualização clara e remota dos dados.
- Geração automática da recomendação de irrigação.

---

## 📌 Considerações Finais

O projeto valida a utilização de soluções **simples de IoT** como ferramentas práticas para resolver problemas do cotidiano, unindo tecnologia, educação ambiental e sustentabilidade.

---
