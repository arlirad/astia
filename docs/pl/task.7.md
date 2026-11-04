# Zadania korutynowe jądra i włókna

## NAME

`core::task<T>`, `core::task<void>` - przenośne, bezstosowe operacje
korutynowe; `core::fiber` - stosowy kontekst wykonywania jądra.

## SYNOPSIS

```cpp
core::task<T> operation();
core::task<void> operation_without_value();

bool task.resume();
bool task.done() const;
bool task.valid() const;
error_t task.error() const;

bool fiber.start();
bool fiber.resume();
static bool fiber::yield();
```

## DESCRIPTION

Astia używa korutyn C++20 do bezstosowego asynchronicznego przepływu
sterowania. `core::task<T>` posiada uchwyt korutyny i rozpoczyna działanie w
stanie wstrzymanym. `task<T>` przechowuje zwróconą wartość albo `error_t`, a
`task<void>` jest wariantem przechowującym tylko błąd. Oba typy można wyłącznie
przenosić i wymagają jednego właściciela decydującego, kiedy wywołać
`resume()`.

Zmienne lokalne, które muszą przetrwać oczekiwanie, znajdują się w ramce
korutyny. Oczekiwanie na zadanie wiąże kontynuację zadania potomnego z jego
rodzicem, a końcowe wstrzymanie potomnego przekazuje sterowanie tej
kontynuacji. Zadania nie wolno wznawiać równocześnie na więcej niż jednym CPU.

## OPERATION

`resume()` wykonuje jeden krok albo oczekujące zadanie potomne i zwraca
`false`, gdy zadanie jest puste lub zakończone. `co_await` zużywa wynik potomny
i niszczy jego ramkę. `value()` udostępnia wartość ukończonego `task<T>`, a
`error()` i `failed()` zgłaszają wynik obietnicy.

Ramki korutyn `task<void>` korzystają z bezblokadowej puli 32 miejsc, z których
każde ma limit 2048 bajtów. Ramki `task<T>` korzystają ze zwykłej ścieżki
alokacji. Pula ogranicza wyłącznie pamięć ramek; nie posiada obiektów, do
których odwołuje się ramka.

## LIFETIME/OWNERSHIP

Zniszczenie lub zresetowanie zadania niszczy jego ramkę korutyny. Zniszczenie
`task<void>` zwraca jego miejsce do puli. Przeniesienie przekazuje jedynego
właściciela zadania i czyści źródło. Referencjonowane pliki, bufory i inne
obiekty muszą pozostać ważne przez całą operację, chyba że ramka przechowuje
uchwyt posiadający własność.

`core::fiber` używa stosu. Uruchomienie przydziela stos jądra o rozmiarze 16 KiB
oraz dwa konteksty architektury. Funkcja wejściowa może wywoływać zwykły kod i
wstrzymać ten stos wywołań przez `fiber::yield()`. Włókno pozostaje własnością
CPU, na którym zostało uruchomione.

## ERRORS

Zbyt duża ramka `task<void>` albo wyczerpanie puli ramek tworzy nieprawidłowe
zadanie z `error_t::enomem`. Przed wznowieniem sprawdź `valid()`, `done()` lub
`error()`; oczekiwanie na zadanie, którego alokacja się nie powiodła, daje
`error_t::enomem`.

`fiber::start()`, `fiber::resume()` i `fiber::yield()` zwracają `false` dla
nieprawidłowej albo ukończonej operacji, nieprawidłowego punktu wejścia, błędu
alokacji lub kontekstu, migracji CPU albo zagnieżdżonego aktywnego włókna.

## NOTES

Użyj zadania, gdy punkty zawieszenia mieszczą się w ramce korutyny. Użyj włókna,
gdy wymagane jest zachowanie zwykłego stosu wywołań. Żaden z tych mechanizmów
sam nie zapewnia planowania, anulowania ani migracji między CPU.
