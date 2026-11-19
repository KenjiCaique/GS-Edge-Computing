/*
  UrbanFlow - Monitoramento do nível de água em um modelo de bueiro
  Global Solution 2026.2 - FIAP - Edge Computing & Computer Systems

  EQUIPE: UrbanFlow
  INTEGRANTES (todos os nomes devem constar aqui - exigência da atividade):
    - Augusto Mota Torin - RM 571119
    - Caique Kenji Yafuco - RM 570368
    - Thiago Alexandre Santos Silva - RM 571342
    - Fabricio Mendoza - RM 571571

  IMPORTANTE: o sensor NÃO detecta lixo e NÃO prova que o bueiro está entupido.
  Ele só mede o nível da água. Um nível alto é um INDÍCIO de possível dificuldade
  de escoamento (pode ser também só chuva forte). A confirmação é feita por pessoas.

  ---------------------------------------------------------------------------
  MODELO DO PROTÓTIPO (as medidas abaixo são do modelo simulado)
  ---------------------------------------------------------------------------
  O sensor fica preso na "tampa" do bueiro, apontado para baixo.
  Fundo do bueiro está a 50 cm do sensor.

        [ sensor ]  <- distância 0 cm
            |
            |  distância medida = espaço vazio até a água
            v
        ~~~~~~~~~~  <- superfície da água
            |
            |  nível da água = 50 - distância
            v
        __________  <- fundo (distância 50 cm = nível 0 cm)

  nível (cm) = PROFUNDIDADE - distância medida
  Quanto MENOR a distância, MAIOR o nível da água.

  Estados:
    NORMAL       nível  < 25 cm   (distância > 25 cm)  -> LED verde
    ATENCAO      nível >= 25 cm   (distância <= 25 cm) -> LED amarelo + bipe leve
    CRITICO      nível >= 40 cm   (distância <= 10 cm) -> LED vermelho + bipe rápido
    FALHA_SENSOR sem leituras válidas por várias vezes -> 3 LEDs piscando + bipe raro

  Controle por Serial Monitor: digite 'o' e envie para ligar/desligar a
  "internet simulada" (para demonstrar o funcionamento sem conexão).
*/

#include <Arduino.h>

// ---------------------------------------------------------------------------
// PINOS (ver tabela de ligações na documentação)
// ---------------------------------------------------------------------------
const int PIN_TRIG        = 5;   // HC-SR04 TRIG
const int PIN_ECHO        = 18;  // HC-SR04 ECHO
const int PIN_LED_VERDE   = 25;
const int PIN_LED_AMARELO = 26;
const int PIN_LED_VERMELHO = 27;
const int PIN_BUZZER      = 14;

// ---------------------------------------------------------------------------
// CONSTANTES DO PROTÓTIPO E LIMITES
// ---------------------------------------------------------------------------
const float PROFUNDIDADE_CM    = 50.0;  // distância do sensor até o fundo do modelo
const float LIMITE_ATENCAO_CM  = 25.0;  // nível a partir do qual entra em ATENCAO
const float LIMITE_CRITICO_CM  = 40.0;  // nível a partir do qual entra em CRITICO
const float HISTERESE_CM       = 2.0;   // folga para SAIR de um estado (evita "piscar" entre estados)

// Faixa de distâncias aceitas como válidas.
// O HC-SR04 não mede bem abaixo de 2 cm. Acima de 55 cm já é maior que o modelo
// (fundo = 50 cm + 5 cm de tolerância), então é uma leitura que não faz sentido.
const float DIST_MIN_VALIDA_CM = 2.0;
const float DIST_MAX_VALIDA_CM = 55.0;

// ---------------------------------------------------------------------------
// CONSTANTES DE TEMPO (todas em milissegundos, controladas com millis())
// ---------------------------------------------------------------------------
const unsigned long INTERVALO_LEITURA_MS = 300;   // lê o sensor a cada 300 ms
const unsigned long INTERVALO_PRINT_MS   = 1000;  // mostra o status a cada 1 s
const unsigned long INTERVALO_ENVIO_MS   = 5000;  // "envia" à nuvem a cada 5 s (simulado)
const unsigned long TIMEOUT_ECHO_US      = 30000; // espera até 30 ms pelo eco (microssegundos)

// ---------------------------------------------------------------------------
// CONSTANTES DE SUAVIZAÇÃO E FALHA
// ---------------------------------------------------------------------------
const int TAMANHO_MEDIA        = 5;  // média das últimas 5 leituras válidas
const int AMOSTRAS_MINIMAS     = 3;  // só decide o estado com pelo menos 3 leituras na média
const int MAX_FALHAS_SEGUIDAS  = 5;  // 5 leituras inválidas seguidas (~1,5 s) = falha do sensor
const int MAX_PENDENTES        = 20; // limite do contador de envios pendentes (modo offline)

// ---------------------------------------------------------------------------
// ESTADOS
// ---------------------------------------------------------------------------
enum Estado {
  ESTADO_NORMAL,
  ESTADO_ATENCAO,
  ESTADO_CRITICO,
  ESTADO_FALHA_SENSOR
};

// ---------------------------------------------------------------------------
// VARIÁVEIS GLOBAIS
// ---------------------------------------------------------------------------
Estado estadoAtual = ESTADO_NORMAL;

float leituras[TAMANHO_MEDIA];   // guarda as últimas leituras válidas
int   indiceLeitura = 0;         // posição onde a próxima leitura será guardada
int   qtdLeituras   = 0;         // quantas leituras válidas há no vetor (0 a 5)
int   falhasSeguidas = 0;        // contador de leituras inválidas consecutivas

float ultimaDistanciaCm = 0;     // última leitura bruta válida
float distanciaMediaCm  = 0;     // média das últimas leituras
float nivelAguaCm       = 0;     // nível estimado da água

bool  buzzerLigado = false;      // para só chamar tone()/noTone() quando mudar

// Controle de tempo (millis)
unsigned long ultimaLeituraMs = 0;
unsigned long ultimoPrintMs   = 0;
unsigned long ultimoEnvioMs   = 0;

// Internet SIMULADA (não existe conexão real neste projeto)
bool internetOnline = true;
int  envioPendentes = 0;

// ---------------------------------------------------------------------------
// FUNÇÕES AUXILIARES
// ---------------------------------------------------------------------------

// Devolve o nome do estado para mostrar no Serial Monitor
const char* nomeEstado(Estado e) {
  switch (e) {
    case ESTADO_NORMAL:       return "NORMAL";
    case ESTADO_ATENCAO:      return "ATENCAO";
    case ESTADO_CRITICO:      return "CRITICO";
    case ESTADO_FALHA_SENSOR: return "FALHA_SENSOR";
  }
  return "DESCONHECIDO";
}

// Configura os pinos de entrada e saída
void configurarPinos() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARELO, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_TRIG, LOW);
}

// Faz UMA medição com o HC-SR04.
// Retorna a distância em cm, ou -1 se não houve eco (timeout).
float medirDistanciaCm() {
  // Pulso de 10 microssegundos no TRIG dispara a medição
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // Mede quanto tempo o ECHO fica em HIGH (ida e volta do som)
  unsigned long duracaoUs = pulseIn(PIN_ECHO, HIGH, TIMEOUT_ECHO_US);

  if (duracaoUs == 0) {
    return -1.0;  // sem eco: sensor desconectado, travado ou nada à frente
  }

  // Velocidade do som ~ 0,0343 cm/us. Divide por 2 porque o som vai e volta.
  return (duracaoUs * 0.0343) / 2.0;
}

// Uma leitura só é válida se existe (>0) e está dentro da faixa aceita
bool leituraValida(float distanciaCm) {
  if (distanciaCm < 0) return false;                    // timeout
  if (distanciaCm < DIST_MIN_VALIDA_CM) return false;   // perto demais
  if (distanciaCm > DIST_MAX_VALIDA_CM) return false;   // longe demais para o modelo
  return true;
}

// Guarda a leitura no vetor circular (as mais antigas são sobrescritas)
void adicionarLeitura(float distanciaCm) {
  leituras[indiceLeitura] = distanciaCm;
  indiceLeitura = (indiceLeitura + 1) % TAMANHO_MEDIA;
  if (qtdLeituras < TAMANHO_MEDIA) {
    qtdLeituras++;
  }
}

// Média das leituras guardadas (suavização)
float calcularMedia() {
  float soma = 0;
  for (int i = 0; i < qtdLeituras; i++) {
    soma += leituras[i];
  }
  return soma / qtdLeituras;
}

// Esquece as leituras antigas (usado quando o sensor falha)
void limparLeituras() {
  indiceLeitura = 0;
  qtdLeituras = 0;
}

// Converte distância (sensor até a água) em nível da água (a partir do fundo)
float distanciaParaNivel(float distanciaCm) {
  float nivel = PROFUNDIDADE_CM - distanciaCm;
  if (nivel < 0) nivel = 0;
  if (nivel > PROFUNDIDADE_CM) nivel = PROFUNDIDADE_CM;
  return nivel;
}

// Decide o estado a partir do nível.
// Histerese: para SAIR de um estado, o nível precisa baixar um pouco abaixo
// do limite. Assim, pequenas oscilações perto do limite não ficam trocando o estado.
Estado definirEstado(float nivelCm, Estado atual) {
  float limiteCritico = LIMITE_CRITICO_CM;
  float limiteAtencao = LIMITE_ATENCAO_CM;

  if (atual == ESTADO_CRITICO) {
    limiteCritico -= HISTERESE_CM;
  }
  if (atual == ESTADO_ATENCAO || atual == ESTADO_CRITICO) {
    limiteAtencao -= HISTERESE_CM;
  }

  if (nivelCm >= limiteCritico) return ESTADO_CRITICO;
  if (nivelCm >= limiteAtencao) return ESTADO_ATENCAO;
  return ESTADO_NORMAL;
}

// Troca de estado e avisa no Serial Monitor
void mudarEstado(Estado novo) {
  Serial.printf("[ESTADO] %s -> %s\n", nomeEstado(estadoAtual), nomeEstado(novo));
  estadoAtual = novo;

  // Desliga o buzzer para o novo estado começar do zero (outro tom/ritmo)
  noTone(PIN_BUZZER);
  buzzerLigado = false;

  switch (novo) {
    case ESTADO_NORMAL:
      Serial.println("[MSG] Nivel da agua normal.");
      break;
    case ESTADO_ATENCAO:
      Serial.println("[MSG] ATENCAO: nivel da agua acima do esperado. Acompanhar.");
      break;
    case ESTADO_CRITICO:
      Serial.println("[MSG] RISCO: nivel da agua muito alto! Possivel dificuldade de escoamento. Verificar o local.");
      break;
    case ESTADO_FALHA_SENSOR:
      Serial.println("[MSG] FALHA DO SENSOR: sem leituras validas. Nivel DESCONHECIDO (nao e alerta de agua).");
      break;
  }
}

// Lê o sensor, valida, suaviza e atualiza o estado
void processarLeitura() {
  float distancia = medirDistanciaCm();

  // 1) Leitura inválida: NÃO muda o estado de água por causa dela.
  if (!leituraValida(distancia)) {
    falhasSeguidas++;
    if (distancia < 0) {
      Serial.printf("[AVISO] Sem eco do sensor (%d/%d falhas seguidas).\n", falhasSeguidas, MAX_FALHAS_SEGUIDAS);
    } else {
      Serial.printf("[AVISO] Leitura invalida: %.1f cm fora da faixa %.0f-%.0f cm (%d/%d falhas seguidas).\n",
                    distancia, DIST_MIN_VALIDA_CM, DIST_MAX_VALIDA_CM, falhasSeguidas, MAX_FALHAS_SEGUIDAS);
    }

    // Só declara falha do sensor depois de várias leituras inválidas seguidas
    if (falhasSeguidas >= MAX_FALHAS_SEGUIDAS && estadoAtual != ESTADO_FALHA_SENSOR) {
      limparLeituras();  // as leituras antigas já não são confiáveis
      mudarEstado(ESTADO_FALHA_SENSOR);
    }
    return;
  }

  // 2) Leitura válida
  falhasSeguidas = 0;
  ultimaDistanciaCm = distancia;
  adicionarLeitura(distancia);

  // Depois de uma falha, espera juntar algumas leituras antes de decidir
  if (qtdLeituras < AMOSTRAS_MINIMAS) {
    Serial.printf("[INFO] Coletando leituras (%d/%d)...\n", qtdLeituras, AMOSTRAS_MINIMAS);
    return;
  }

  // 3) Suavização + nível + estado
  distanciaMediaCm = calcularMedia();
  nivelAguaCm = distanciaParaNivel(distanciaMediaCm);

  Estado novo = definirEstado(nivelAguaCm, estadoAtual);
  if (novo != estadoAtual) {
    mudarEstado(novo);
  }
}

// ---------------------------------------------------------------------------
// ALERTAS LOCAIS (LEDs e buzzer) - funcionam com ou sem internet
// ---------------------------------------------------------------------------
void atualizarLeds() {
  bool verde = false, amarelo = false, vermelho = false;

  switch (estadoAtual) {
    case ESTADO_NORMAL:  verde = true;    break;
    case ESTADO_ATENCAO: amarelo = true;  break;
    case ESTADO_CRITICO: vermelho = true; break;
    case ESTADO_FALHA_SENSOR: {
      // Os 3 LEDs piscam juntos (500 ms ligado / 500 ms desligado)
      bool pisca = (millis() % 1000) < 500;
      verde = amarelo = vermelho = pisca;
      break;
    }
  }

  digitalWrite(PIN_LED_VERDE, verde);
  digitalWrite(PIN_LED_AMARELO, amarelo);
  digitalWrite(PIN_LED_VERMELHO, vermelho);
}

// O buzzer "bipa" de forma intermitente usando millis(), sem delay().
// periodo = tempo entre o início de um bipe e o próximo
// duracao = quanto tempo cada bipe dura
void atualizarBuzzer() {
  unsigned long periodo = 0, duracao = 0;
  int frequencia = 0;

  switch (estadoAtual) {
    case ESTADO_ATENCAO:       periodo = 2000; duracao = 150; frequencia = 1000; break; // bipe leve
    case ESTADO_CRITICO:       periodo = 400;  duracao = 250; frequencia = 2000; break; // bipe rápido/agudo
    case ESTADO_FALHA_SENSOR:  periodo = 4000; duracao = 100; frequencia = 500;  break; // bipe grave e raro
    default: break;  // NORMAL: buzzer desligado
  }

  bool deveLigar = (periodo > 0) && ((millis() % periodo) < duracao);

  if (deveLigar && !buzzerLigado) {
    tone(PIN_BUZZER, frequencia);
    buzzerLigado = true;
  } else if (!deveLigar && buzzerLigado) {
    noTone(PIN_BUZZER);
    buzzerLigado = false;
  }
}

// ---------------------------------------------------------------------------
// COMUNICAÇÃO COM O BACK-END - SOMENTE SIMULADA
// Nenhum dado é realmente enviado. O código só imprime o que SERIA enviado.
// ---------------------------------------------------------------------------
void simularEnvioNuvem() {
  char payload[160];
  unsigned long uptimeS = millis() / 1000;

  if (estadoAtual == ESTADO_FALHA_SENSOR) {
    // Em falha não existe nível confiável, então não enviamos um valor inventado
    snprintf(payload, sizeof(payload),
             "{\"estado\":\"%s\",\"nivel_cm\":null,\"uptime_s\":%lu}",
             nomeEstado(estadoAtual), uptimeS);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"nivel_cm\":%.1f,\"distancia_cm\":%.1f,\"estado\":\"%s\",\"uptime_s\":%lu}",
             nivelAguaCm, distanciaMediaCm, nomeEstado(estadoAtual), uptimeS);
  }

  if (internetOnline) {
    Serial.printf("[NUVEM-SIMULADA] Enviando: %s\n", payload);
  } else {
    if (envioPendentes < MAX_PENDENTES) envioPendentes++;
    Serial.printf("[OFFLINE-SIMULADO] Sem internet. Alertas locais continuam ativos. Envios pendentes: %d\n",
                  envioPendentes);
  }
}

// Lê comandos digitados no Serial Monitor. 'o' liga/desliga a internet simulada.
void tratarComandosSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'o' || c == 'O') {
      internetOnline = !internetOnline;
      if (internetOnline) {
        Serial.println("[REDE-SIMULADA] Conexao restabelecida.");
        if (envioPendentes > 0) {
          Serial.printf("[NUVEM-SIMULADA] Reenviando %d leituras pendentes (simulado).\n", envioPendentes);
          envioPendentes = 0;
        }
      } else {
        Serial.println("[REDE-SIMULADA] Internet DESLIGADA. O monitoramento local continua.");
      }
    }
  }
}

// Mostra o resumo no Serial Monitor
void imprimirStatus() {
  if (estadoAtual == ESTADO_FALHA_SENSOR) {
    Serial.printf("[STATUS] estado=%s | nivel=DESCONHECIDO | rede=%s\n",
                  nomeEstado(estadoAtual), internetOnline ? "ONLINE(sim)" : "OFFLINE(sim)");
  } else {
    Serial.printf("[STATUS] dist=%.1f cm | media=%.1f cm | nivel=%.1f cm | estado=%s | rede=%s\n",
                  ultimaDistanciaCm, distanciaMediaCm, nivelAguaCm,
                  nomeEstado(estadoAtual), internetOnline ? "ONLINE(sim)" : "OFFLINE(sim)");
  }
}

// ---------------------------------------------------------------------------
// SETUP E LOOP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  configurarPinos();

  Serial.println("=== UrbanFlow - monitor de nivel de agua ===");
  Serial.printf("Limites: ATENCAO >= %.0f cm | CRITICO >= %.0f cm (nivel a partir do fundo)\n",
                LIMITE_ATENCAO_CM, LIMITE_CRITICO_CM);
  Serial.println("Digite 'o' e envie para ligar/desligar a internet SIMULADA.");
  Serial.println("Aviso: nivel alto indica POSSIVEL problema de escoamento, nao confirma entupimento.");
}

void loop() {
  unsigned long agora = millis();

  tratarComandosSerial();

  // Leitura do sensor a cada INTERVALO_LEITURA_MS
  if (agora - ultimaLeituraMs >= INTERVALO_LEITURA_MS) {
    ultimaLeituraMs = agora;
    processarLeitura();
  }

  // Status no Serial Monitor
  if (agora - ultimoPrintMs >= INTERVALO_PRINT_MS) {
    ultimoPrintMs = agora;
    imprimirStatus();
  }

  // Envio (simulado) ao back-end
  if (agora - ultimoEnvioMs >= INTERVALO_ENVIO_MS) {
    ultimoEnvioMs = agora;
    simularEnvioNuvem();
  }

  // Alertas locais rodam sempre, independente da "internet"
  atualizarLeds();
  atualizarBuzzer();
}
