# Asynchroniczne wejście-wyjście

## NAME

`core::async_operation<T>`, `fs::io_packet` - jednorazowe ukończenie
wejścia-wyjścia jądra; Astia AIO - współdzielone pierścienie zgłoszeń i
ukończeń.

## SYNOPSIS

```cpp
core::task<i64> read(const mm::shptr<open_file>&, span<u8>, u64 offset);
core::task<i64> write(const mm::shptr<open_file>&, span<const u8>, u64 offset);
core::task<i64> read_exact(const mm::shptr<open_file>&, span<u8>, u64 offset);
```

```c
astia_sq_publish(ring, count);
astia_io_submit(ring, count);
while (astia_cq_ready(ring) != 0) {
    astia_cqe *cqe = astia_cq_peek(ring);
    handle_completion(cqe->user_data, cqe->result);
    astia_cq_consume(ring, 1);
}
```

## DESCRIPTION

Asynchroniczne operacje plikowe jądra zgłaszają pracę do VFS bez zmuszania
metody urządzenia do oczekiwania. Żądanie może ukończyć się natychmiast,
zostać uśpione, gdy urządzenie nie może kontynuować, albo ukończyć się ze
ścieżki obsługi przerwania.

`core::async_operation<T>` jest jednorazowym obiektem oczekiwalnym ze stanami
oczekującym, kończącym się, ukończonym i anulowanym. Przechowuje wartość albo
`error_t`; jeden producent wygrywa wyścig ukończenia. Jeden oczekujący dołącza
jedną kontynuację i zużywa opcjonalny wynik bez wywołania zwrotnego ani blokady.

## OPERATION/DATA FLOW

`fs::io_packet` przechowuje referencję do otwartego pliku oraz stabilne
`io_request` dla jednego lotu urządzenia. Jego żądanie posiada ukończenie
`async_operation<i64>`. `submit()` przekazuje żądanie plikowi i zwraca
natychmiastowy wynik zgłoszenia. Wynik ujemny jest błędem zgłoszenia; zero
oznacza operację w toku; wynik dodatni jest natychmiastową liczbą bajtów.
Późniejsza liczba bajtów albo błąd operacji jest przekazywany przez ukończenie.

Ścieżki przerwań urządzeń i AIO używają `complete_deferred()`. Publikuje ono
wynik i umieszcza kontynuację przez `schedule_resume()` w kolejce CPU, które
publikuje ukończenie. Tylko to CPU opróżnia ograniczoną kolejkę wznowień;
planista opróżnia ją po przełączeniu kontekstu, nie dopuszczając dowolnych
korutyn w obsłudze przerwań i zachowując założenia o CPU właściciela. Pełna
kolejka zwraca `false` i pozostawia kontynuację niewznowioną, choć wynik
ukończenia pozostaje opublikowany.

Pomocnicze funkcje plikowe zwracają zadania korutynowe. `read()` i `write()`
mogą zwrócić skrócone liczby bajtów. `read_exact()` wykonuje jedno odczytanie i
zwraca `error_t::eio`, gdy jest ono krótsze; nie wykonuje pętli w celu
wypełnienia zakresu.

AIO przestrzeni użytkownika przedstawia każdą operację, która może blokować,
przez SQE `OP_*`. Jądro publikuje CQE, a `user_data` z SQE jest do niego
kopiowane, aby wywołujący mógł skierować wynik bez dodatkowej tablicy.

## LIFETIME/OWNERSHIP

Wywołujący musi utrzymać ważność referencji do pliku, bufora i zwróconego
zadania aż do zakończenia operacji. `io_packet` posiada referencję do otwartego
pliku i utrzymuje swoje żądanie stabilne przez cały lot urządzenia. Jego
ukończenie musi żyć dłużej niż żądanie urządzenia i jest zużywane raz.

Każdy pierścień ma jednego producenta zgłoszeń i jednego odbiorcę CQ.
Użycie między wątkami wymaga synchronizacji przestrzeni użytkownika.
Zarejestrowane pliki i bufory pozostają przypięte i mogą być wybierane przez
flagi SQE; operacje niezarejestrowane używają zwykłej walidacji deskryptora i
bufora.

## ERRORS

`io_packet::submit()` natychmiast zwraca `-EBADF` dla brakującego pliku oraz
inne ujemne błędy zgłoszenia. Alokacja ukończenia jądra może zwrócić
`error_t::enomem`. Wynik CQE jest nieujemnym ładunkiem przy powodzeniu, takim
jak liczba bajtów, deskryptor, identyfikator procesu lub maska gotowości, a
przy błędzie jest ujemnym `-errno`. Anulowanie kończy operację asynchroniczną
z `error_t::eagain`.

Skrócony odczyt jest zwykłym wynikiem `read()`, ale z `read_exact()` daje
`error_t::eio`. Pełna kolejka wznowień właściciela CPU może zgubić pobudzenie i
ścieżka właściciela musi potraktować to jako błąd zasobu odroczonego ukończenia.

## NOTES

`astia_io_submit()` jest tylko impulsem do zgłoszenia. Zgłasza partię i wraca
bez oczekiwania na ukończenie urządzenia; źródłem prawdy jest CQE, a nie wynik
impulsu. Publikacja SQ używa porządku release, a odczyty CQ porządku acquire.
`SIGAIO` jedynie powiadamia proces właściciela, że CQE mogą być dostępne.
Opróżnij CQ szybko, następnie przesuń jego głowę z porządkiem release;
zużycie co najmniej jednego CQE potwierdza generację powiadomienia, a
opróżnianie CQ nie wymaga wywołania systemowego.
