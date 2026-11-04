# RCU i ograniczenia bezblokadowe

## NAME

`core::rcu_domain` - ograniczone odzyskiwanie epokowe; `core::rcu_vector` i
`core::rcu_hashmap` - niezmienne migawki bez blokad.

## SYNOPSIS

```cpp
core::rcu_domain<CpuCount, RetireCapacity> domain;
core::rcu_domain<>::read_guard guard(domain, cpu);
bool domain.retire(cpu, value, destroy);
u32 domain.poll();

core::rcu_vector<T, Capacity> vector;
core::rcu_vector<T, Capacity>::read_guard snapshot(vector);
bool vector.replace(values);

core::rcu_hashmap<Key, Value> map(hash, equal);
auto value = map.find(key);
bool map.insert_or_assign(key, value);
bool map.erase(key);
```

## DESCRIPTION

Struktury RCU Astii publikują niezmienne migawki. Czytelnik pobiera strażnika,
ładuje jedną migawkę i przegląda ją bez mutexa. Pisarz tworzy zamiennik,
publikuje go atomowo i odkłada starą migawkę do odzyskania. Strażnik chroni
migawkę tylko przez czas trwania sekcji odczytu.

## OPERATION/DATA FLOW

`rcu_domain<CpuCount, RetireCapacity>` śledzi zagnieżdżenie czytelników i epoki
dla każdego CPU. `read_guard` zapisuje bieżącą epokę przy pierwszym
zagnieżdżonym odczycie. `retire()` przechowuje wartość i destruktor w
ograniczonej kolejce odzyskiwania wybranego CPU. `poll()` przesuwa epokę i
wywołuje destruktory dopiero wtedy, gdy wszyscy czytelnicy, którzy mogli
zobaczyć odkładaną epokę, osiągnęli stan quiescent. `quiescent()` aktualizuje
obserwowaną epokę nieaktywnego CPU.

`rcu_vector<T, Capacity>` publikuje jedną niezmienną tablicę o ograniczonym
rozmiarze. Jego `read_guard` udostępnia `data()`, `size()` i indeksowany dostęp
tylko do odczytu. `replace()` alokuje i wypełnia nową migawkę, a następnie
ponawia publikację compare-and-swap, jeśli inny pisarz wygrał. Zastąpione
migawki są odzyskiwane po osiągnięciu przez licznik czytelników zera.

`rcu_hashmap` dzieli niezmienne migawki kubełków na fragmenty. `read_guard`
przeszukuje jeden fragment, a `find()` kopiuje wartość przed zwolnieniem
strażnika. Aktualizacje klonują wybrany fragment, wstawiają lub usuwają klucz
i ponawiają publikację CAS przy rywalizacji. Węzły kubełków utrzymują swojego
następcę przez posiadające referencje współdzielone.

## LIFETIME/OWNERSHIP

Aktywny strażnik nie może blokować, usypiać się, migrować między CPU ani
wywoływać kodu, który może zaplanować pracę. Utrzymuj go przy życiu dla
każdego wyłuskania wskaźnika. Skopiuj wartość albo zachowaj posiadającą
referencję przed opuszczeniem sekcji odczytu; surowy wskaźnik z migawki nie
może wydostać się poza strażnika.

Pisarze posiadają nowo zaalokowaną migawkę do chwili publikacji i odkładają
zastąpioną migawkę. Nie modyfikuj opublikowanego obiektu. Zniszczenie
`rcu_vector` albo `rcu_hashmap` wymaga, aby wszyscy czytelnicy osiągnęli stan
quiescent; `rcu_domain` musi żyć dłużej niż jego strażnicy i odłożone wartości.

## ERRORS

`rcu_domain::retire()` zwraca `false` dla nieprawidłowego CPU lub wartości,
brakującego destruktora albo pełnej ograniczonej kolejki odzyskiwania.
`rcu_vector::replace()` zwraca `false`, gdy dane wejściowe przekraczają
pojemność albo wymagana alokacja się nie powiedzie.

Wstawianie, przypisanie i usuwanie z hashmapy zwraca `false`, gdy alokacja
migawki, węzła albo rekordu odzyskiwania się nie powiedzie. Czytelnicy zwracają
pusty optional albo null pointer, gdy brakuje klucza lub migawki. Błędy
alokacji i ograniczonej kolejki są rzeczywistymi ścieżkami; czytelnicy nie
czekają na wolne miejsce.

## NOTES

Ochrona po stronie odczytu RCU nie czyni otaczającego stanu bezblokadowym.
Uzyskaj stabilną referencję przed przekroczeniem granicy podsystemu; nie
wywołuj callbacków systemu plików, urządzenia, planisty, pobudzenia,
mapowania ani niszczenia podczas trzymania niezwiązanej blokady; i maskuj
przerwania przed pobraniem blokady widocznej dla IRQ. Czytelnicy bezblokadowi
nie mogą blokować ani migrować między CPU.
