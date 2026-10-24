#pragma once

class App;

// Timer de rounds para Muay Thai / boxe: preparação, rounds, descanso, aviso de 10 s e vários sinais.
namespace timer {
void Tick(App& app);                 // chamar todo frame (mesmo com a janela escondida)
void Window(App& app, bool* open);   // painel acoplável "Timer"
bool Running();
void StartNow(App& app);           // inicia do zero (comando de voz)
// Texto curto para a barra de título (ex.: "R2 · 1:42"); vazio quando parado.
const char* Badge();
}
