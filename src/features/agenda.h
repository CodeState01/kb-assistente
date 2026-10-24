#pragma once
#include <string>
#include <vector>

// Agenda da treinadora: rotina semanal (aulas, compromissos, horários livres) e pedidos de aula experimental.
// A IA monta a rotina pelo que a pessoa fala, mostra os horários livres e cada aula só é marcada
// depois que a treinadora confirma no notebook.
namespace agenda {

enum ItemType { Class = 0, Busy = 1, Free = 2 };
enum Status { Pending = 0, Confirmed = 1, Declined = 2, Canceled = 3 };

struct Item {
    std::string id, title, start = "19:00", end = "20:00";   // "HH:MM"
    std::vector<int> days;                                   // 0 = domingo ... 6 = sábado
    int type = Class;
    int trialSpots = 2;                                      // vagas de experimental por aula
};

struct Booking {
    std::string id, name, phone, date, start, end, title, source, note;  // date = "AAAA-MM-DD"
    int status = Pending;
    long long created = 0;
};

struct Slot {
    std::string key;     // "AAAA-MM-DD HH:MM"
    std::string date, start, end, title, label;  // label = "seg, 27/10 às 19:00"
    int spotsLeft = 1;
};

struct Data {
    std::string studio = "KB Muay Thai";
    std::string address;
    int daysAhead = 14;
    std::vector<Item> routine;
    std::vector<Booking> bookings;
};

Data Load();
void Save(const Data& d);
std::string NewId(const char* prefix);

std::vector<Slot> FreeSlots(const Data& d, int maxSlots = 40);
// Cria um pedido pendente. Retorna o id (vazio + erro se o horário não estiver mais livre).
std::string Request(const std::string& name, const std::string& phone, const std::string& slotKey,
                    const std::string& source, std::string* err);
bool SetStatus(const std::string& id, int status, Booking* out = nullptr);
bool Find(const std::string& id, Booking* out);

std::string DayName(int wday, bool shortName = true);     // "seg" / "segunda-feira"
std::string DaysText(const std::vector<int>& days);       // "seg, qua e sex"
std::string DateLabel(const std::string& date);           // "seg, 27/10"
int ParseDays(const std::string& s);                      // bitmask a partir de "segunda a sexta", "seg qua sex"...
std::string NormTime(const std::string& s);               // "7h" → "07:00", "19:30" → "19:30"; vazio se inválido

std::string RoutineText(const Data& d);                   // para a IA
std::string SlotsText(const Data& d, int max = 12);
std::string WhatsappConfirm(const Data& d, const Booking& b);   // link wa.me para avisar o aluno

} // namespace agenda
