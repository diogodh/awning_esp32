/*
  comando_optoacoplador.ino — controlar o comando Gaviota Elite por opto-acopladores.
  O ESP32-C6 "carrega" nos botoes SUBIR / STOP / DESCER atraves de 3x PC817.
  O comando gera os codigos cifrados / rolling code sozinho; nos so simulamos o dedo.

  Hardware: ESP32-C6 + 3x PC817 + 3x resistencia 330R.
  O comando continua alimentado pela SUA pilha (CR2032, 3V) -> fica isolado do ESP32.

  Ligacao POR CADA canal (um por botao):
     GPIO ---[330R]---> PC817 pino 1 (anodo do LED)
     PC817 pino 2 (catodo do LED) ---> GND do ESP32
     PC817 pino 4 (coletor) ---> contacto do botao a potencial MAIS ALTO
     PC817 pino 3 (emissor) ---> o OUTRO contacto do botao
  (o transistor do opto fica em paralelo com o botao; ativar o GPIO = carregar no botao)
  Se um botao nao reagir, troca os pinos 3<->4 (polaridade do transistor).

  Monitor serie: 115200 baud, fim de linha Newline.
  Comandos:  u = subir   s = stop   d = descer   p<ms> = duracao do impulso (ex.: p600)
*/

// --- escolhe 3 GPIOs livres do ESP32-C6 (evita 4,5,8,9,12,13,15) ---
#define PIN_UP    2
#define PIN_STOP  3
#define PIN_DOWN  1   // GP1 (o GP6 do ESP32-C6-Zero so existe como pad na parte de tras)

int pulseMs = 400;   // duracao de cada "carregar" em ms. Ajusta com 'p<ms>'.

void press(int pin){
  digitalWrite(pin, HIGH);   // LED do opto ON -> transistor conduz -> botao "carregado"
  delay(pulseMs);
  digitalWrite(pin, LOW);    // solta o botao
}

void subir(){  Serial.println("-> SUBIR");  press(PIN_UP);   }
void parar(){  Serial.println("-> STOP");   press(PIN_STOP); }
void descer(){ Serial.println("-> DESCER"); press(PIN_DOWN); }

void setup(){
  Serial.begin(115200); delay(300);
  // arrancam todos em LOW (botao solto) ANTES de qualquer coisa
  pinMode(PIN_UP, OUTPUT);   digitalWrite(PIN_UP, LOW);
  pinMode(PIN_STOP, OUTPUT); digitalWrite(PIN_STOP, LOW);
  pinMode(PIN_DOWN, OUTPUT); digitalWrite(PIN_DOWN, LOW);
  Serial.println("Pronto. Comandos: u=subir  s=stop  d=descer  p<ms> (ex.: p600)");
  Serial.printf("Impulso atual: %d ms\n", pulseMs);
}

String line;
void loop(){
  while(Serial.available()){
    char c = Serial.read();
    if(c=='\n' || c=='\r'){
      line.trim();
      if(line.length()){
        if(line=="u")      subir();
        else if(line=="s") parar();
        else if(line=="d") descer();
        else if(line.startsWith("p")){ pulseMs = line.substring(1).toInt(); Serial.printf("pulso = %d ms\n", pulseMs); }
        else Serial.println("Comandos: u / s / d / p<ms>");
      }
      line = "";
    } else if(line.length() < 20) line += c;
  }
}
