// Test der Telnet-Aufbereitung auf dem Laptop:  g++ -std=c++17 -I../include test_filter.cpp -o /tmp/t && /tmp/t
#include <cstdio>
#include <string>
#include <vector>
#include "telnet_filter.h"

static int fehler = 0;
static void pruefe(const char* name, std::vector<uint8_t> ein, std::vector<uint8_t> soll, telnet::Filter* f = nullptr) {
  telnet::Filter lokal;
  if (!f) f = &lokal;
  std::vector<uint8_t> aus(ein.size() + 1);
  aus.resize(f->eingang(ein.data(), ein.size(), aus.data()));
  bool ok = aus == soll;
  printf("%s %s\n", ok ? "ok    " : "FEHLER", name);
  if (!ok) fehler++;
}

int main() {
  using namespace telnet;
  pruefe("normaler Text", {'p', 'i', 'r', 'o', 'o', 't'}, {'p', 'i', 'r', 'o', 'o', 't'});
  pruefe("CR LF -> CR", {'a', '\r', '\n', 'b'}, {'a', '\r', 'b'});
  pruefe("CR NUL -> CR", {'a', '\r', 0, 'b'}, {'a', '\r', 'b'});
  pruefe("einzelnes LF bleibt", {'a', '\n'}, {'a', '\n'});
  pruefe("Antwort DO ECHO entfernt", {IAC, DO, OPT_ECHO, 'x'}, {'x'});
  pruefe("Unterverhandlung entfernt", {IAC, SB, 24, 0, 'x', 't', 'e', 'r', 'm', IAC, SE, 'y'}, {'y'});
  pruefe("IAC IAC -> 0xFF", {IAC, IAC}, {IAC});
  pruefe("2-Byte-Befehl (NOP) entfernt", {IAC, 241, 'z'}, {'z'});
  // Befehl über zwei Pakete verteilt
  Filter f;
  pruefe("geteilt Teil 1", {'a', IAC}, {'a'}, &f);
  pruefe("geteilt Teil 2", {WILL, 31, 'b'}, {'b'}, &f);
  pruefe("CR am Paketende, LF im nächsten", {'c', '\r'}, {'c', '\r'}, &f);
  pruefe("... LF wird verworfen", {'\n', 'd'}, {'d'}, &f);
  // Ausgang: 0xFF verdoppeln
  uint8_t in[] = {'a', IAC, 'b'}, out[6];
  size_t k = ausgang(in, 3, out);
  bool ok = k == 4 && out[1] == IAC && out[2] == IAC && out[3] == 'b';
  printf("%s Ausgang verdoppelt 0xFF\n", ok ? "ok    " : "FEHLER");
  if (!ok) fehler++;
  printf("%d Fehler\n", fehler);
  return fehler ? 1 : 0;
}
