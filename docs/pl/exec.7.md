# Asynchroniczne ładowanie ELF i `execve`

## NAME

`execve(2)`, `elf::load_executable_stream()` - zastępuje obraz procesu obrazem
z pliku ELF, gdy wejście-wyjście jądra jest asynchroniczne.

## SYNOPSIS

```c
long execve(const char *path, char *const argv[], char *const envp[]);
```

```cpp
core::task<elf::image> elf::load_executable_stream(
    elf::async_env, usize file_size);
```

## DESCRIPTION

`execve` jest synchroniczne z perspektywy przestrzeni użytkownika, ale
wewnątrz jądra działa asynchronicznie. Wywołanie systemowe nie wraca, gdy jego
wykonawca czeka na wejście-wyjście pliku; wywołujący wątek jądra ustępuje, a
planista uruchamia inną pracę. Loader nigdy nie publikuje częściowo
zastąpionego obrazu.

## OPERATION/DATA FLOW

`elf::load_executable_stream()` odczytuje nagłówek ELF i tablicę nagłówków
programu, waliduje obraz, a następnie odczytuje każdy zakres `PT_LOAD` w
fragmentach po 4096 bajtów. Mapuje cały zakres, wypełnia zerami końcówkę
pamięci, stosuje obsługiwane relokacje `R_X86_64_RELATIVE`, odczytuje szablon
statycznego TLS oraz zapisuje punkt wejścia i przesunięcie ładowania. ET_EXEC
używa stałego adresu, a ET_DYN otrzymuje przesunięcie wybrane przez alokator.

Proces najpierw buduje przejściową mapę stron. Przygotowuje w niej strony
segmentów, TLS i stos startowy. Nieudane odczyty, zniekształcone metadane,
nieobsługiwane relokacje, błędy mapowania i błędy ramek odwzorowują oczekujące
zakresy i zwalniają ich ramki. Przyjmowany jest tylko kompletny obraz.

Wywołanie systemowe kopiuje ścieżkę, wektor argumentów i wektor środowiska do
`exec_continuation`. Jego wykonawca oczekuje na
`process::exec_user_file_async()`. Gdy ładowanie i budowa stosu się powiedzie,
przetwarzane są deskryptory close-on-exec, stary obraz zostaje zastąpiony, a
zapisana ramka wywołania systemowego jest zmieniana na nowy punkt wejścia i
stos. Przy błędzie stary obraz pozostaje na miejscu, a wywołanie zwraca błąd.

## LIFETIME/OWNERSHIP

Kontynuacja utrzymuje proces, wywołujący wątek, otwarty plik, przygotowane
argumenty i zadanie wykonawcy aż do ukończenia. Proces przygotowania posiada
nową mapę stron i mapowania przed zastąpieniem. Ramka korutyny zachowuje stan
loadera, przesunięcia, oczekujące mapowania i wyniki odczytów między
oczekiwaniami.

Stary obraz nie jest zmieniany, dopóki przygotowany obraz i stos startowy nie
będą poprawne. Każda ścieżka błędu musi zakończyć sprzątanie, zanim
kontynuacja i przechowywane przez nią referencje zostaną zwolnione.

## ERRORS

Nieprawidłowe albo niedostępne wskaźniki ścieżki, argumentów i środowiska
zwracają `-EFAULT`. Błędy alokacji kontynuacji, przygotowania argumentów,
ramki korutyny, mapy stron, ramki fizycznej lub obrazu zwracają `-ENOMEM`.
Brakujące pliki zwracają `-ENOENT`, a puste lub niepoprawne pliki wykonywalne
`-ENOEXEC` albo zmapowane przez loader `-EINVAL`. Błędy pliku i urządzenia
przekazują swoje ujemne errno.

Żadna ścieżka błędu nie zmienia starego obrazu procesu. Udane `execve` nie
wraca do starej instrukcji przestrzeni użytkownika; wchodzi do nowego obrazu
z jego nowym punktem wejścia i stosem startowym.

## NOTES

Ścieżka strumieniowa obsługuje obrazy ELF64 ET_EXEC i ET_DYN oraz tylko formy
relokacji akceptowane przez loader. TLS jest materializowany dla nowego
procesu. Kopiowanie argumentów i każda alokacja są zakończone i sprawdzone
przed przetwarzaniem close-on-exec lub publikacją obrazu.
