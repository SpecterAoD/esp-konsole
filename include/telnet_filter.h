// Telnet-Aufbereitung für die ESP-Konsole (ohne Arduino-Abhängigkeiten, auf dem Laptop testbar).
//
// Richtung Telnet-Programm -> Pi: Telnet-Steuerfolgen (IAC …) entfernen, damit nur getippte
// Zeichen beim Pi ankommen; "CR LF" und "CR NUL" werden zu einem einzelnen CR (sonst doppelte Eingabe).
// Richtung Pi -> Telnet-Programm: ein Datenbyte 0xFF muss als 0xFF 0xFF verschickt werden.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace telnet {

const uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
const uint8_t OPT_ECHO = 1, OPT_SGA = 3;

// Beim Verbindungsaufbau senden: "ich (der Server) übernehme das Echo" und
// "kein Go-Ahead" -> das Telnet-Programm schaltet sein lokales Echo ab (kein "pp" mehr).
const uint8_t BEGRUESSUNG[] = {IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA, IAC, DO, OPT_SGA};

class Filter {
 public:
  // Verarbeitet empfangene Bytes; schreibt Nutzdaten nach out (mindestens n Bytes Platz).
  // Rückgabe: Anzahl Bytes in out.
  size_t eingang(const uint8_t* in, size_t n, uint8_t* out) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
      uint8_t b = in[i];
      switch (zustand_) {
        case NORMAL:
          if (b == IAC) { zustand_ = NACH_IAC; break; }
          if (nach_cr_ && (b == '\n' || b == 0)) { nach_cr_ = false; break; }
          nach_cr_ = (b == '\r');
          out[k++] = b;
          break;
        case NACH_IAC:
          if (b == IAC) { out[k++] = IAC; nach_cr_ = false; zustand_ = NORMAL; }  // echtes 0xFF
          else if (b >= WILL && b <= DONT) zustand_ = OPTION;                    // 3-Byte-Befehl
          else if (b == SB) zustand_ = UNTERVERHANDLUNG;
          else zustand_ = NORMAL;                                                 // 2-Byte-Befehl
          break;
        case OPTION:
          zustand_ = NORMAL;
          break;
        case UNTERVERHANDLUNG:
          if (b == IAC) zustand_ = UNTERVERHANDLUNG_IAC;
          break;
        case UNTERVERHANDLUNG_IAC:
          zustand_ = (b == SE) ? NORMAL : UNTERVERHANDLUNG;
          break;
      }
    }
    return k;
  }
  void zuruecksetzen() { zustand_ = NORMAL; nach_cr_ = false; }

 private:
  enum Zustand { NORMAL, NACH_IAC, OPTION, UNTERVERHANDLUNG, UNTERVERHANDLUNG_IAC };
  Zustand zustand_ = NORMAL;
  bool nach_cr_ = false;
};

// Pi -> Telnet: 0xFF verdoppeln. out braucht bis zu 2*n Bytes. Rückgabe: Anzahl Bytes.
inline size_t ausgang(const uint8_t* in, size_t n, uint8_t* out) {
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    out[k++] = in[i];
    if (in[i] == IAC) out[k++] = IAC;
  }
  return k;
}

}  // namespace telnet
