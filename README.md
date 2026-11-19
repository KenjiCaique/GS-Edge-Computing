# UrbanFlow — CitizenTech IoT com ESP32

Global Solution 2026.2 · FIAP · Edge Computing & Computer Systems

**Equipe: UrbanFlow**

**Integrantes**

| Nome completo | RM |
|---|---|
| Augusto Mota Torin | 571119 |
| Caique Kenji Yafuco | 570368 |
| Thiago Alexandre Santos Silva | 571342 |
| Fabricio Mendoza | 571571 |

---

## 1. Descrição do projeto

**Cidadão beneficiado:** [moradores de ___ , bairro ___ , São Paulo — a equipe completa]

**Problema em uma frase:** [ex.: "Moradores de ___ ficam sem aviso quando a água sobe no bueiro da rua e ela pode transbordar."]

O UrbanFlow monitora o **nível da água** em um modelo de bueiro e responde sozinho, no próprio dispositivo (edge): mede, decide e alerta com LEDs e buzzer, mesmo sem internet.

> **Limite importante:** o sensor ultrassônico não vê lixo e não prova entupimento. Ele só mede a distância até a água. Nível alto é um **indício** de possível dificuldade de escoamento (pode ser só chuva forte). A confirmação deve ser feita por uma pessoa no local.

**ODS conectados:** 11 (cidades e comunidades sustentáveis), 13 (ação contra a mudança do clima), 9 (inovação e infraestrutura) e 3 (saúde e bem-estar).

---

## 2. Componentes utilizados

| Componente | Qtd | Por que está no circuito |
|---|---|---|
| ESP32 DevKit V1 | 1 | Lê o sensor, decide o estado e aciona os alertas |
| Sensor ultrassônico HC-SR04 | 1 | Mede a distância entre o sensor e a superfície da água |
| LED verde, amarelo, vermelho | 3 | Mostram o estado de longe |
| Resistor 220 Ω | 3 | Limita a corrente de cada LED |
| Buzzer | 1 | Alerta sonoro para quem não está olhando |

A protoboard não foi usada: os componentes ligam direto ao ESP32 para manter o diagrama simples e sem fios cruzados.

### Ligações

| Componente | Pino | ESP32 |
|---|---|---|
| HC-SR04 | VCC | 3V3 |
| HC-SR04 | TRIG | GPIO 5 |
| HC-SR04 | ECHO | GPIO 18 |
| HC-SR04 | GND | GND (direita) |
| LED verde (via 220 Ω) | Anodo | GPIO 25 |
| LED amarelo (via 220 Ω) | Anodo | GPIO 26 |
| LED vermelho (via 220 Ω) | Anodo | GPIO 27 |
| LEDs | Catodo | GND (esquerda) |
| Buzzer | Pino 2 (+) | GPIO 14 |
| Buzzer | Pino 1 (−) | GND (esquerda) |

Em hardware real, o HC-SR04 comum usa 5 V e seu ECHO exigiria divisor de tensão para o ESP32 (3,3 V). No Wokwi isso não é necessário.

---

## 3. Lógica de funcionamento

### Modelo do protótipo
O sensor fica na "tampa" do bueiro apontando para baixo; o fundo está a **50 cm**.

```
nível da água (cm) = 50 − distância medida (cm)
```

### Estados e limites

| Estado | Nível | Distância | LEDs | Buzzer | Serial Monitor |
|---|---|---|---|---|---|
| NORMAL | < 25 cm | > 25 cm | Verde | Desligado | Mensagem de normalidade |
| ATENÇÃO | ≥ 25 cm | ≤ 25 cm | Amarelo | Bipe leve (1 kHz, a cada 2 s) | Mensagem de atenção |
| CRÍTICO | ≥ 40 cm | ≤ 10 cm | Vermelho | Bipe rápido (2 kHz, a cada 0,4 s) | Mensagem de risco |
| FALHA_SENSOR | desconhecido | — | 3 LEDs piscando | Bipe grave raro | Aviso de falha |

**Por que esses limites:** são valores para o modelo simulado. A água na metade da profundidade (25 de 50 cm) pede atenção; a 80% (40 cm) é risco alto. Num bueiro real, seriam calibrados com a profundidade e o nível normal de cada ponto.

### Estabilidade e erros
- **Suavização:** média das últimas 5 leituras válidas.
- **Histerese de 2 cm:** para sair de um estado, o nível precisa baixar 2 cm abaixo do limite, evitando trocas por pequenas oscilações.
- **Leitura inválida** (sem eco ou fora de 2–55 cm): é descartada, não entra na média e não muda o estado.
- **Falha do sensor:** após 5 leituras inválidas seguidas (~1,5 s), o sistema entra em FALHA_SENSOR. Uma falha **nunca** vira alerta crítico de água.
- **Tempo:** leituras, impressões e bipes usam `millis()`, sem `delay()` para controle.

### Funcionamento sem internet
O monitoramento e os alertas rodam inteiramente no ESP32. A comunicação com a nuvem é **apenas simulada**: o código imprime no Serial Monitor o JSON que seria enviado (`[NUVEM-SIMULADA]`). Digitar `o` no Serial Monitor liga/desliga a internet simulada; com ela desligada, LEDs e buzzer continuam normais. Não há integração real com FIWARE ou outra plataforma.

### Arquitetura em 3 camadas (proposta)
1. **Dispositivo IoT:** ESP32 + sensor + LEDs + buzzer (implementado).
2. **Back-end:** FIWARE (Orion + IoT Agent MQTT) receberia `{nivel_cm, distancia_cm, estado, uptime_s}` (apenas simulado).
3. **Aplicação:** dashboard ou app mostraria nível, estado e histórico para moradores e responsáveis (apenas proposta).

---

## 4. Como executar a simulação

1. Abra o projeto no Wokwi pelo link: **https://wokwi.com/projects/480990336231367681**.
2. Clique em **Start Simulation** e abra o **Serial Monitor**.
3. Clique no HC-SR04 e mude o campo **Distance**. A resposta leva ~1,5 s (média de 5 leituras).

| Para testar | Distance | Resultado |
|---|---|---|
| Normal | 45 cm | LED verde, sem som |
| Atenção | 20 cm | LED amarelo, bipe leve |
| Crítico | 6 cm | LED vermelho, bipe rápido |
| Leitura inválida | 300 cm | Avisos no Serial; após ~1,5 s entra em FALHA_SENSOR (3 LEDs piscando) |
| Falha do sensor | Apagar o fio do ECHO | Mesmo comportamento de falha |
| Sem internet | Digitar `o` + Enter no Serial | Alertas locais continuam; avisos de modo offline |
