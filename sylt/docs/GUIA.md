# Sylt

> **Sylt 0.6**, un lenguaje de programación compilado con la velocidad de C++ y la legibilidad de Python. Sintaxis por indentación, structs con métodos, **genéricos ligeros `type Caja[T]`**, enums con `match` exhaustivo, módulos con `import`, **pruebas integradas con `sylt test`**, bindings `foreign` a C++ sin FFI, movimiento y préstamos al estilo Rust (versión ligera), y mensajes de error que **explican en vez de regañar**. El mismo binario trae formateador (`syltc fmt`), servidor de lenguaje (`syltc lsp`) y gestor de paquetes (`syltc pkg`).

| Qué promete | Cómo lo cumple |
|---|---|
| **Velocidad C++** | Transpila a C++17 limpio y compila con `-O2`/`-O3` (g++/clang). En el benchmark del repo: ~59.000 FPS con comprobaciones de seguridad, ~800.000 FPS con `--fast`, igual que el C++ puro equivalente |
| **Dos backends, un lenguaje** | El mismo AST y el mismo checker alimentan `--backend=cpp` (C++17) y `--backend=llvm` (IR textual verificado con `llc`, compilado con clang). La suite comprueba que ambos produzcan **salida idéntica** |
| **Se escribe como Python** | Bloques por sangría, sin punto y coma, sin tipos en las locales, interpolación `"{Nombre}"`, continuaciones de línea con operador al final |
| **Seguridad tipo Rust (lite)** | Los structs **se mueven**, no se copian, también al entrar en colecciones; préstamos `&T` / `&mut T`; inmutabilidad de `let`; `match` exhaustivo sobre enums; todo detectado **al compilar** |
| **Pruebas integradas** | `sylt test archivo.sy` ejecuta cada `fn Prueba*()` con `Afirma(cond, "mensaje")`: informe por prueba, línea del fallo y código de salida para CI |
| **Errores como rustc** | Código de error, caja con la línea, `^` apuntando al problema, **nota** con el origen y **ayuda** con la solución |
| **Ecosistema C++ gratis** | Un bloque `foreign` declara funciones de cualquier cabecera C/C++ y se llaman como funciones propias. La librería `std.*` se construye así y en Sylt mismo |
| **Foco back-end** | Binarios nativos, sin VM, sin GC, sin runtime pesado: la STL hace el trabajo (RAII, regla R.1). El ejemplo `ServidorHttp.sy` es un servidor HTTP escrito íntegramente en Sylt |


*Guía completa heredada de la v0.4. Para las novedades de la versión actual (JSON, Str con decimales, keep-alive), consulta el README.*

---

## 1. Filosofía (basada en las C++ Core Guidelines)

Sylt nace de tres reglas del documento *C++ Core Guidelines* (Stroustrup & Sutter):

- **P.1, "Express ideas directly in code"**: lo que escribes se lee como lo piensas. `let` para lo fijo, `var` para lo mutable, `mut self` para métodos que mutan. Nada de semánticas ocultas.
- **R.1, "Manage resources automatically using resource handles and RAII"**: cero gestión manual de memoria. Strings, arrays y mapas son handles RAII de la STL. No hay `new`/`delete`, no hay fugas, no hay GC pausando tu game loop.
- **ES.20, "Always initialize an object"**: en Sylt **no existen las variables sin inicializar**. `let X` sin `=` es un error de compilación, y los campos de struct sin valor por defecto son obligatorios en la construcción (el compilador te recuerda la ES.20 si te saltas uno).

Y una más, prestada de Rust: *los errores del compilador son documentación*. Cada mensaje dice **qué** pasó, **dónde**, **por qué** y **cómo arreglarlo**.

---

## 2. Instalación (30 segundos)

Requisitos: un compilador de C++ (`g++` o `clang++`). Sin dependencias más allá de la STL.

```bash
# 1. compila el compilador de Sylt (syltc), un comando
g++ -std=c++17 -O2 -o syltc src/*.cpp

# 2. prueba
./syltc run examples/Hola.sy
```

`syltc` busca `sylt_prelude.hpp` en este orden: `--prelude=RUTA` → `SYLT_PRELUDE` → junto al binario → `~/.local/share/sylt/`. La std se busca igual con `--lib=RUTA` → `SYLT_LIB` → `lib/` junto al binario → `~/.local/share/sylt/lib`. Instalación permanente de una línea:

```bash
mkdir -p ~/.local/share/sylt && cp -r syltc sylt_prelude.hpp lib ~/.local/share/sylt/
```

O simplemente `make` para compilar, `make test` para la suite completa y `make install` para dejarlo fijo. Las opciones (`--prelude`, `--backend`, `--fast`...) se aceptan en cualquier posición: `syltc --prelude=RUTA run prog.sy` y `syltc run --prelude=RUTA prog.sy` valen igual.

### FAQ: el archivo `syltc`

- **¿Qué es?** No es un documento ni un tipo de archivo: es **el compilador**, un programa ejecutable, como `python` o `rustc`, pero para Sylt. El nombre significa *Sylt compiler*. Tú escribes los `.sy`; `syltc` es la herramienta que los lee, los comprueba y los convierte en un binario nativo. El `syltc` del repo ya viene compilado (son ~800 KB de código máquina; si lo abres verás caracteres raros, es normal).
- **`command not found`** → Linux solo busca programas en su `PATH`, y este está en tu carpeta. Llámalo con `./syltc` (el `./` le dice a la shell "este de aquí, no un comando del sistema").
- **`Permission denied`** → al subirlo a GitHub o descomprimirlo se pierde el permiso de ejecución: `chmod +x syltc` y listo.
- **`cannot execute binary file: Exec format error`** → ese binario fue compilado para otra arquitectura (p. ej. Mac ARM vs Linux x86-64). La solución de siempre funciona en cualquier sitio: compílalo tú mismo en 30 segundos con el comando de arriba.
- **¿Lo subo a GitHub?** No, el binario no va al repositorio: cada máquina lo compila con `make` o el `g++` de arriba. El `.gitignore` del repo ya lo excluye (junto con `T4` y restos de compilación).

---

## 3. Tu primer programa

```sylt
# Hola.sy
fn Main():
    let Nombre = "Sylt"
    let Version = 0.1
    Print("¡Hola, {Nombre} {Version}!")
```

```bash
$ syltc run examples/Hola.sy
¡Hola, Sylt 0.4!
```

Eso por dentro ha sido: **lex → parse → chequeo de tipos y movimiento → C++17 → g++ -O2 → ejecutable nativo**. Sin intérprete, sin VM.

---

## 4. Tour de sintaxis

### Variables: `let` (inmutable) y `var` (mutable)

```sylt
let Saldo = 1000        # tipo inferido: int (64 bits)
var Veces = 0           # mutable
Veces += 1
let Ratio: float = 3    # anotación opcional; el 3 se convierte a 3.0
```

- Los tipos de las **locales se infieren**. Los de parámetros, retornos y campos **se anotan** (así los errores salen claros y el binario vuela).
- Intentar modificar un `let` es **error de compilación** (E0504), no un aviso que nadie lee.

### Funciones

```sylt
fn EnviarSaludo(Nombre: string, Veces: int) -> string
    var Texto = ""
    for I in 0..Veces
        Texto += "Hola, {Nombre}!\n"
    return Texto
```

### Control de flujo

```sylt
if Puntos > 100 and not Bloqueado
    SubirNivel()
elif Puntos > 50
    Avisar()
else
    print_nada()

while QuedanTurnos > 0
    JugarTurno()

for I in 0..10          # 0,1,...,9 (el final es exclusivo, como range())
    Print("{I}")
for E in Lista          # arrays y claves de mapa también son iterables
    Print("{E}")
```

### Strings con interpolación (potencia de `printf`, comodidad de f-string)

```sylt
let Edad = 31
Print("{Nombre} tiene {Edad} años y media edad: {Edad / 2.0}")
Print("llaves literales: {{así}}")
```

Dentro de `{ }` va cualquier expresión, incluyendo strings anidados: `"{Mapa[\"clave\"]}"`.

### Structs y métodos (`self` / `mut self`)

```sylt
type Vec2:
    X: float = 0.0
    Y: float = 0.0

    fn Norma(self) -> float          # self = solo lectura
        return Sqrt(self.X * self.X + self.Y * self.Y)

    fn Escalar(mut self, K: float)   # mut self = puede modificar
        self.X = self.X * K
        self.Y = self.Y * K

fn Main():
    var A = Vec2(X: 3.0, Y: 4.0)
    Print("norma: {A.Norma()}")      # 5.0
    A.Escalar(2.0)                   # ok: A es var
```

- Construcción **siempre completa**: `Vec2(X: 3.0, Y: 4.0)`. Si un campo no tiene valor por defecto y no lo pasas → error E0403 con la regla ES.20 en la ayuda.
- Un método `mut self` sobre una variable `let` → error E0504.

### Colecciones: arrays `[T]`, mapas `{K: V}`, tuplas `(A, B)`

```sylt
var Numeros = [5, 3, 9]
Numeros.Push(7)
let Ultimo = Numeros.Pop()
let Esta = Numeros.Contiene(3)

let Edades = { "Ana": 31, "Luis": 25 }
for Nombre in Edades                 # itera las claves (ordenadas)
    Print("{Nombre}: {Edades[Nombre]}")

fn MinMax(A: int, B: int) -> (int, int)   # tuplas: varios valores (F.21)
    if A < B:
        return (A, B)
    return (B, A)

let (Menor, Mayor) = MinMax(14, 7)         # destructuración
```

### Funciones integradas

`Print(x)` · `Str(x)` · `Sqrt(f)` · `Abs(n)` · `Min(a, b)` · `Max(a, b)` · `Pow(b, e)` · `Floor(f)` · `Clone(x)` · `Afirma(cond, "mensaje")` (lanza el fallo de la prueba con su línea) · `TiempoMs()` (monótono, para medir).

Métodos integrados: arrays `Push` `Pop` `Len` `Contiene` · mapas `Len` `Contiene` `Claves` · strings `Len` `Contiene`.

### Enums: un valor que es una cosa u otra

```sylt
enum EstadoPedido:
    Pendiente
    Enviado(tracking: string)
    Entregado(dias: int)
    Cancelado(motivo: string)
```

Cada **variante** puede llevar valores (como las tuplas de Rust, con sintaxis Python). Se construyen cualificadas (`EstadoPedido.Enviado("SY-42")`) o, si el nombre es único, a secas. Un enum se imprime: `Print("{Pedido}")` muestra `Enviado{tracking: SY-42}`.

### match: agotador por diseño

```sylt
fn Describir(E: EstadoPedido) -> string:
    match E:
        case Pendiente:
            return "esperando pago"
        case Enviado(T):
            return "en camino con código {T}"
        case Entregado(D):
            return "entregado en {D} días"
        case Cancelado(M):
            return "cancelado: {M}"
```

- Si el sujeto es un enum, **todos los casos deben cubrirse** (o acabar en `case _:`). Si mañana añades una variante, el compilador te señala cada `match` que se quedó corto (E0601). Enlaza los valores de la variante por posición: `case Enviado(T):`.
- También funciona sobre `int`, `string` y `bool` con literales (`case 404:`, `case "GET":`) y comodín `case _:` obligatorio al final.
- Los patrones cualificados (`case EstadoPedido.Rojo:`) y a secas (`case Rojo:`) valen igual.

### Genéricos ligeros: `type Caja[T]`

```sylt
type Caja[T]:
    Valor: T

    fn Obtener(self) -> T:
        return self.Valor

type Pila[T]:
    Items: [T] = []
    N: int = 0

    fn Apila(mut self, V: T):
        self.Items.Push(V)
        self.N += 1
```

- Se instancian con tipos explícitos: `Caja[int](Valor: 42)`, `Pila[string]()`, `Par[string, int](...)`, y se anidan a cualquier profundidad: `Pila[Caja[int]]()`, `let M: [{string: [int]}]`.
- El compilador **monomorfiza**: cada `Caja[int]` y `Caja[string]` es un struct C++ real y distinto (`Caja_of_int`, `Caja_of_string`), con sus métodos comprobados por separado, un cuerpo que solo funciona con números falla al compilar para `Caja[string]`, con el error apuntando a tu código.
- Los métodos aceptan `T` en parámetros y retornos (`fn Apila(mut self, V: T)`), y los campos pueden ser colecciones de `T` (`Items: [T]`).
- Un genérico sin corchetes es error E0310 con el ejemplo de la solución; la aridad equivocada es E0311. Los enums genéricos todavía no (E0314).

### Continuación de líneas

Una línea que acaba en `+ - * / % , == != < <= .. &` continúa en la siguiente (la sangría de la continuación es libre):

```sylt
let Saludo = "Hola, " +
    Nombre +
    "! Bienvenido a Sylt."
```

Dentro de paréntesis y corchetes los saltos siempre se ignoran, como en Python.

---

## 5. Movimiento y préstamos (borrow checker lite)

Tres reglas, aplicadas **al compilar**:

1. **Los structs se mueven, no se copian.** `let D = C` deja `C` vacío: usar `C` después es error E0501. Los tipos básicos, strings, arrays y mapas sí se copian.
2. **¿Necesitas conservar el original? Copia en voz alta**: `Clone(C)`. Explícito, sin copias ocultas.
3. **Los préstamos se declaran en la firma y se piden con `&`**: `&T` (solo lectura) y `&mut T` (lectura-escritura, exige `var`). En una misma llamada, una variable no puede prestarse `&mut` y además usarse de otra forma (E0503).

```sylt
fn SubirNivel(J: &mut Jugador)      # préstamo mutable: puede modificar
    J.SumarPuntos(100)

fn LeerPuntos(J: &Jugador) -> int   # préstamo inmutable
    return J.Puntos

fn Main():
    var Heroe = Jugador(Nombre: "Ada")
    SubirNivel(&Heroe)              # & mutuo
    let N = LeerPuntos(&Heroe)      # & compartido

    let Respaldo = Clone(Heroe)     # copia explícita
    let Traspaso = Heroe            # ¡MOVER! Heroe ya no existe
    Traspaso.Saludar()
    Respaldo.Saludar()
    # Print("{Heroe.Nombre}")       # ← error[E0501]: uso después de mover
```

Esto es el borrow checker "menos avanzado" que pedía el diseño: sin lifetimes anotadas, sin `'a`… pero detectando use-after-move, aliasing conflictivo y mutación de inmutables antes de ejecutar nada.

---

## 6. Módulos: un archivo, un módulo

```sylt
import matematicas

fn Main():
    Print("el doble de 21 es {matematicas.Doble(21)}")
    let A = matematicas.Punto(X: 3.0, Y: 4.0)
    Print("norma: {A.Norma()}")
```

- `import X` carga el archivo `X.sy` de la misma carpeta. Todo lo que exporta se usa cualificado: `X.Funcion(...)`, `X.Tipo`, `X.EnumNombre`.
- Los tipos cualificados también valen en anotaciones: `let P: matematicas.Punto = ...`.
- Dentro de un módulo, sus propios nombres se usan a secas; los imports **no se propagan** (si `matematicas` importa `std.str`, tú no lo ves: importa tú también). Los ciclos de importación se detectan y se reportan (E0702).
- `import std.str` carga la librería estándar (ver sección 8).
- Cada módulo se comprueba con sus propios mensajes de error apuntando a **su** archivo.

---

## 7. foreign: todo el ecosistema C++ sin FFI

Aquí está la respuesta corta a "¿y las librerías?". Como Sylt compila a C++, llamar a C++ es gratis:

```sylt
foreign:
    include <cmath>
    fn fmod(X: float, Y: float) -> float
    fn sin(X: float) -> float
    include <cstdlib>
    fn atoi(S: string) -> int

fn Main():
    Print("sin(pi/2) = {sin(3.14159265 / 2.0)}")
    Print("fmod(10, 3) = {fmod(10.0, 3.0)}")
```

- Cada `fn` de un bloque `foreign` es una función C++ real, con ese nombre y esa firma. Se comprueban los tipos en la frontera y se llama sin intermediarios: cero coste, cero wrappers generados, cero ABI inventado.
- Tipos admitidos en la frontera: `int`, `float`, `bool`, `string` y colecciones planas de estos (`[T]`, `{K: V}`, tuplas). Los structs y enums viven dentro del mundo Sylt.
- `include <cmath>` (o `include "cabecera.hpp"`) añade la cabecera al archivo generado.
- ¿Una librería con tipos complejos? Envuélvela: escribe en C++ un fichero de funciones planas (estilo C) y decláralas con `foreign`. Es la misma técnica que usa Zig con `@cImport` o Nim con `importc`, pero aquí el "binding" son dos líneas.

---

## 8. La librería estándar `std.*` (y la estrategia de librerías)

La std de Sylt no es C++ disfrazado: está escrita **en Sylt mismo** y solo usa `foreign` para las primitivas del runtime (`sy_*` en `sylt_prelude.hpp`). Es, además, el mejor ejemplo de cómo se construyen librerías en este lenguaje.

| Módulo | Qué trae |
|---|---|
| `std.str` | `Dividir`, `Unir`, `Recortar`, `Mayusculas`, `Minusculas`, `Reemplazar`, `EmpiezaCon`, `TerminaCon`, `Buscar`, `Repetir`, `Subcadena`, `Caracteres` (UTF-8), `AEntero` |
| `std.io` | `LeerLinea`, `LeerArchivo`, `EscribirArchivo`, `AnexarArchivo`, `Existe` |
| `std.time` | `AhoraMs` (reloj de pared), `MonoMs` (monótono para medir) |
| `std.net` | TCP directo: `TcpConectar`, `TcpIntentar`, `TcpEnviar`, `TcpRecibir`, `TcpCerrar`, `TcpEscuchar`, `TcpAceptar`, `TcpDireccion` |
| `std.http` | `Get(url) -> (codigo, cuerpo)`, un cliente HTTP escrito en Sylt sobre `std.net` y `std.str` |

El ejemplo `examples/ServidorHttp.sy` es un servidor HTTP completo (rutas, cabeceras, 404) en ~60 líneas de Sylt:

```sylt
import std.net
import std.str
import std.time

fn Main():
    let Servidor = std.net.TcpEscuchar(8080)
    Print("Servidor Sylt escuchando en http://127.0.0.1:8080")
    while true:
        let Cliente = std.net.TcpAceptar(Servidor)
        Atender(Cliente, std.net.TcpDireccion(Cliente))
```

### La estrategia de librerías, explicada

Python y Node tienen decenas de miles de paquetes porque llevan 20-30 años acumulando comunidad; ningún lenguaje nuevo compite ahí a corto plazo, y intentar clonar su stdlib sería quemar años en reinventar `requests`. La jugada correcta, la que hicieron Rust (FFI a C), Zig (`@cImport`), Nim (`importc`) y Go (cgo), es distinta:

1. **Núcleo pequeño y honesto.** La std cubre lo que todo back-end toca: strings, io, tiempo, red. Nada más por ahora.
2. **El ecosistema ajeno es TU ecosistema.** Por transpilar a C++, toda librería C/C++ (y son la mayoría de las que existen: SQLite, OpenCV, OpenSSL, libcurl...) queda a un bloque `foreign` de distancia. No se porta nada: se declara y se usa a velocidad nativa.
3. **Las librerías de Sylt se escriben en Sylt** (como `std.http` sobre `std.net`) y se reparten como archivos `.sy` que se importan. Sin binarios, sin formatos de paquete, sin repositorio central: en v0.2 basta con copiar el archivo junto al tuyo.
4. **Un registro de paquetes (estilo crates.io) es un problema de comunidad, no de lenguaje.** Entra en la hoja de ruta cuando haya alguien más aparte de ti publicando código.

### La deuda freestanding (custardOS)

La std de Sylt descansa en dos cimientos que **no existirán** en custardOS: la STL de C++ y el sistema operativo. El prelude (`sylt_prelude.hpp`) implementa 26 primitivas `sy_*`; cada una es deuda que habrá que reescribir sin libstdc++ y con syscalls propias. El núcleo del lenguaje (tipos, match, movimiento, préstamos) no debe nada a la STL, la deuda está confinada a:

| Grupo | Primitivas | De qué depende hoy |
|---|---|---|
| runtime básico | strings (`std::string`), arrays (`std::vector`), mapas (`std::unordered_map`), tuplas, `Print` | STL (RAII, allocación) |
| `std.str` (11) | `sy_str_dividir` … `sy_str_a_entero` | `std::string`, regex no; todo es mano |
| `std.io` (5) | `sy_io_*` | `<fstream>`, `<cstdio>`, POSIX |
| `std.time` (1) | `sy_time_epoch_ms` | `std::chrono` → `clock_gettime` |
| `std.net` (8) | `sy_net_*` | sockets BSD POSIX |

El plan cuando llegue custardOS: reescribir el prelude contra un allocador propio + syscalls del kernel objetivo (las 26 tienen firmas planas a propósito, para que el puerto sea mecánico), y mantener `lib/std/*.sy` intacta, está escrita en Sylt y no toca C++ directamente, solo las primitivas.

El backend llvm ya va por delante en esta carrera: su runtime de strings vive en IR puro sobre `malloc` y no incluye ni una línea de STL. Lo que le falta es lo mismo que a la std: colecciones e IO con ABI propia.

---

## 9. Galería de errores (salidas reales de `syltc`)

Todos los mensajes de esta sección son salidas literales del compilador (archivos en `examples/errores/`).

### E0501 · uso después de mover

```text
error[E0501]: no puedes usar `C` porque su valor ya se movió
  ┌─ examples/errores/UsoDespuesDeMover.sy:9:17
  │
 9 │     Print("C = {C.Marca}")
   │                 ^
  │
nota: el valor se movió en esta otra línea
  ┌─ examples/errores/UsoDespuesDeMover.sy:7:13
  │
 7 │     let D = C
   │             ^
  │
ayuda: los structs no se copian: se mueven. Si quieres conservar el original, escribe `Clone(C)` en el punto de movimiento
```

### E0401 · tipos incompatibles

```text
error[E0401]: no puedo ordenar `int` con `string`
  ┌─ examples/errores/TipoErroneo.sy:4:8
  │
 4 │     if Edad < Saludo:
   │        ^^^^^^^^^^^^^
  │
ayuda: < <= > >= funcionan con números y con strings (orden alfabético)
```

### E0301 · nombre desconocido, con sugerencia

```text
error[E0301]: la variable `Nombre2` no existe en este programa
  ┌─ examples/errores/VariableTypo.sy:4:21
  │
 4 │     Print("Adiós, {Nombre2}")
   │                     ^^^^^^^
  │
ayuda: ¿quisiste decir `Nombre`?
```

### E0504 · mutar un inmutable

```text
error[E0504]: no puedes modificar `self` en un método con `self` (sin `mut`)
  ┌─ examples/errores/MutacionInmutable.sy:5:9
  │
 5 │         self.Valor += N
   │         ^^^^
  │
nota: declarado aquí
  ┌─ examples/errores/MutacionInmutable.sy:4:13
  │
 4 │     fn Sube(self, N: int):
   │             ^^^^
  │
ayuda: cambia la firma del método a `mut self`
```

### E0403 · falta un campo en la construcción (con ES.20)

```text
error[E0403]: falta el campo `Y` en `Punto(...)`
  ┌─ examples/errores/FaltaCampo.sy:6:18
  │
 6 │     let P = Punto(X: 1)
   │                 ^
  │
nota: campo declarado aquí
  ┌─ examples/errores/FaltaCampo.sy:3:5
  │
 3 │     Y: int
   │     ^
  │
ayuda: añádelo: Punto(X: ..., Y: ...)
ayuda: regla ES.20 de las C++ Core Guidelines: inicializa siempre un objeto
```

### E0601 · match no exhaustivo (el checker de enums)

```text
error[E0601]: este match no cubre todos los casos del enum `Semforo`
faltan: mbar
  ┌─ examples/errores/MatchIncompleto.sy:7:11
  │
 7 │     match S:
  │           ^
  │
ayuda: añade un `case` para cada variante, o un `case _:` al final como comodín
ayuda: Sylt es exhaustivo: si mañana añades una variante, el compilador te dirá qué match se quedó corto
```

### E0602 · variante o patrón equivocado

Escribir `Semforo.Ambar()` cuando la variante es `mbar` produce E0602 con sugerencia; también salta si enlazas un número equivocado de valores en un patrón o repites un caso.

### E0503 · préstamos conflictivos en la misma llamada

```text
error[E0503]: préstamos conflictivos: `C` se presta como mutable y también se usa en la misma llamada
  ┌─ examples/errores/PrestamoConflictivo.sy:10:18
  │
10 │     let N = Roba(&C, &C)
   │                  ^
  │
nota: y también se usa aquí
  ┌─ examples/errores/PrestamoConflictivo.sy:10:22
  │
10 │     let N = Roba(&C, &C)
   │                      ^ y también se usa aquí
  │
ayuda: hazlo en dos pasos: primero la llamada con &mut y después la otra
```

Y en ejecución, por si algo escapa al análisis estático:

```text
error en ejecución: índice 10 fuera de rango en `L` (Len=3)
error en ejecución: la clave b no existe en `M`
error en ejecución: división entera por cero
```

---

## 10. El compilador `syltc`

```text
syltc run   archivo.sy      compila en un temporal y ejecuta
syltc build archivo.sy      crea un ejecutable (-o para nombrarlo)
syltc check archivo.sy      solo comprueba (tipos + movimiento), ideal para CI
syltc test  archivo.sy      ejecuta las pruebas Prueba* del archivo
syltc emit  archivo.sy      muestra el C++ generado (para curiosos y depurar)
syltc fmt   ruta...         formatea archivos o carpetas de .sy en el sitio
syltc lsp                   servidor de lenguaje (JSON-RPC por stdio)
syltc pkg   add|list|remove gestor de paquetes (carpeta local o git)

--O0 --O1 --O2 --O3   nivel de optimización (por defecto -O2)
--fast                sin comprobaciones de límites/índices en caliente
--cc=clang++          usa otro compilador C++
--backend=cpp|llvm    cpp: transpila a C++17 (por defecto); llvm: emite IR (.ll)
--prelude=RUTA        dónde está sylt_prelude.hpp
--lib=RUTA            dónde está la librería estándar (carpeta con std/)
--pkg=RUTA            dónde está el registro local de paquetes (o $SYLT_PKG)
--check               con fmt: no escribe, solo informa lo que cambiaría
--keep                conserva el código generado (en /tmp/sylt-*)
--no-color            salida plana (también se activa con NO_COLOR)
```

### Pruebas integradas: `sylt test`

```sylt
fn Doble(N: int) -> int:
    return N * 2

fn PruebaDoble():
    Afirma(Doble(2) == 4, "Doble(2) debería ser 4")
```

```bash
$ syltc test examples/Pruebas.sy
ok PruebaDoble
ok PruebaTextos
fallo: PruebaMala (línea 13): esta prueba debe fallar a propósito
4 pruebas, 1 fallo
```

- Toda función global `fn PruebaNombre()` sin parámetros es una prueba; `Afirma(cond, "mensaje")` la corta con el número de línea si la condición es falsa.
- El informe sigue tras un fallo, y el código de salida es 1 si algo falló: encaja directo en CI junto a `syltc check`.
- Sin ninguna `Prueba*` → E0210; una `Prueba*` con parámetros o dentro de un módulo → E0211.

### El backend llvm (v0.3; completo desde v0.5.1)

```bash
syltc emit  --backend=llvm examples/Fib.sy   # ver el IR textual
syltc build --backend=llvm --cc=clang examples/Fib.sy   # ejecutable nativo vía clang
```

- **Cubre el núcleo del lenguaje**: int/float/bool, aritmética y comparaciones, `if/elif/else`, `while`, `for` sobre rangos, funciones y recursión, structs con métodos (`self`, `mut self`, `&T`/`&mut T`), structs genéricos monomorfizados, enums con `match` exhaustivo y vinculaciones, strings con interpolación (runtime propio en IR sobre malloc), `Clone`, `Print`, `Str`, `Sqrt`, `Pow`, `Floor`, `Abs`, `Min`, `Max`, módulos propios y bloques `foreign`.
- **Desde v0.5.1, además**: colecciones (arrays, mapas, tuplas), los módulos `std.*` y las pruebas de `sylt test`. Cualquier laguna restante se señala con **E0901** en la línea exacta; con `--backend=cpp` compila todo.
- **Verificación**: el IR pasa el verificador de LLVM (`llc archivo.ll`). La suite (`tests/llvm_diff.py`) compila cada programa por los DOS backends y exige la misma salida byte a byte: 13 casos, incluidos structs, enums, préstamos y módulos.
- **Runtime mínimo**: el IR generado trae su propio runtime de strings (`%SyltStr = {i64 len, ptr datos}`) escrito en IR puro sobre `malloc`/libc, sin STL ni libstdc++. Es el mismo camino que seguirá custardOS.
- La frontera `foreign` declara los tipos Sylt tal cual (`int` = `i64`): para funciones C que devuelvan `int` de 32 bits, envuélvelas en una función intermedia de C o Sylt.

Los módulos `std.*` se buscan en `--lib`, luego en `SYLT_LIB`, luego en `lib/` junto al binario, luego en `~/.local/share/sylt/lib`. El prelude se busca en `--prelude`, `SYLT_PRELUDE`, junto al binario y en `~/.local/share/sylt/`. Con `check` en verde puedes confiar: los errores de tipo, movimiento, préstamos y exhaustividad ya no existen.

### Formateo: `syltc fmt`

```bash
syltc fmt archivo.sy          # formatea en el sitio
syltc fmt src/                # todos los .sy de una carpeta, recursivamente
syltc fmt --check src/        # no escribe; avisa qué cambiaría y sale con 1
```

`syltc fmt` aplica el estilo canónico del lenguaje: sangría de 4 espacios, un espacio alrededor de los operadores, `{ "clave": valor }` en los mapas, `fn Nombre(...)` sin espacio antes del paréntesis, líneas en blanco de como mucho una entre bloques y un único salto final.

Lo que **no** toca: tus comentarios quedan donde están (los de línea propia se reindantan con su bloque), los strings se conservan byte a byte (escapes e interpolaciones incluidos) y los saltos de línea que escribiste dentro de expresiones largas se respetan: el formateador normaliza sangría y espacios, no reenvuelve líneas.

Es idempotente: formatear dos veces deja el mismo texto. Con `--check` en CI basta una línea para exigir estilo uniforme:

```bash
syltc fmt --check . || echo "hay archivos sin formatear"
```

### El servidor de lenguaje: `syltc lsp`

```bash
syltc lsp
```

Habla JSON-RPC 2.0 por stdin/stdout con la codificación estándar `Content-Length`, así que vale cualquier cliente LSP. Lo que ofrece en v0.6:

- **Diagnósticos en vivo**: al abrir o editar un archivo (`textDocument/didOpen` / `didChange`) ejecuta el mismo lexer, parser y checker que `syltc check` y publica los errores con código, posición y ayuda: tipos, movimiento, préstamos, `match` incompleto, nombres mal escritos con sugerencia.
- **Formato del documento**: la petición `textDocument/formatting` devuelve el texto que produciría `syltc fmt`, para formatear desde el editor.

Los diagnósticos salen del mismo checker que el compilador: lo que ves en el editor es exactamente lo que `syltc build` dirá. El archivo se analiza contra los módulos de su carpeta y la `std.*`; el registro de paquetes (`SYLT_PKG`) también participa.

### Paquetes: `syltc pkg`

Un paquete es una carpeta con un módulo principal `nombre.sy` y los módulos que necesite junto a él. Se instala en el registro local (`~/.local/share/sylt/pkg/`) desde una carpeta o desde git:

```bash
syltc pkg add ./mi-paquete                                # carpeta local
syltc pkg add https://github.com/usuario/paquete          # clona (depth 1) e instala
syltc pkg add https://github.com/u/paquete otro-nombre    # nombre explícito
syltc pkg list                                            # qué hay instalado
syltc pkg remove paquete                                  # desinstala
```

Tras instalarlo, `import nombre` lo encuentra igual que un módulo local: primero se busca junto al archivo que importa, después en el registro. Cada instalación deja su origen y versión anotados en `<nombre>.meta`; desde git se anota el commit. La ruta del registro se cambia con `--pkg=RUTA` o la variable `SYLT_PKG`, lo que también permite registros aislados por proyecto.

---

## 11. Benchmark: 2.000 entidades × 120 frames

`examples/BenchJuego.sy` simula un game loop (física de entidades, energía cinética) y mide con `TiempoMs()`:

| Configuración (máquina de pruebas, g++ -O2) | Total (120 frames) | ms/frame | FPS |
|---|---|---|---|
| **Sylt** (con comprobaciones de índices) | ~2,0 ms | ~0,017 ms | **~59.000** |
| **Sylt `--fast`** | ~0,15 ms | ~0,0012 ms | **~800.000** |
| C++ puro equivalente (mismo algoritmo) | ~0,15 ms | ~0,0012 ms | ~800.000 |

Es decir: a 60 FPS tienes margen de **×1.000**; a 120 FPS, margen de **×500**. Y con `--fast` Sylt es, literalmente, el mismo C++.

Reprodúcelo:

```bash
syltc run examples/BenchJuego.sy            # con seguridad
syltc run --fast examples/BenchJuego.sy     # modo salvaje
```

---

## 12. Cómo funciona por dentro

```text
archivo.sy (+ sus import sy)
   |  lexer (sangría -> INDENT/DEDENT, continuaciones, strings con {interp})
   v
tokens --> parser descendente + Pratt --> AST (uno por archivo, fusionados)
   |
   v  checker: tipos, structs, enums+match, módulos, movimientos, préstamos
   |         (todo error se explica con código, caja, nota y ayuda)
   |
   +--> generador de C++17  --> /tmp/.../main_sylt.cpp --> g++/clang++ -O2
   |                                                              v
   |                                                        ejecutable nativo
   +--> generador de LLVM IR --> /tmp/.../main_sylt.ll  --> llc (verifica)
        (--backend=llvm)                                   clang -lm (enlaza)
```

**¿Por qué dos backends?** El de C++ da el runtime gratis (STL, RAII, optimizador de años). El de LLVM demuestra que el checker es el corazón del lenguaje: el mismo AST alimentó otro generador completo y las salidas son idénticas. Es además el camino natural a custardOS: el runtime del backend llvm ya es freestanding-friendly (malloc + libc, cero STL). Mapeo de tipos: `int` → `int64_t` / `i64` · `float` → `double` · `string` → `std::string` / `%SyltStr` · `[T]` → `std::vector<T>` · `{K: V}` → `std::unordered_map<K, V>` · tuplas → `std::tuple` · structs → structs C++ / `%type` de LLVM · enums → struct con etiqueta + campos de todas sus variantes · `match` → cadena de `if/else` (C++) o despacho por etiqueta (IR).

Los símbolos de un módulo se prefijan con su nombre (`matematicas.Doble` → `matematicas__Doble`), así que todo el programa viaja en un único archivo C++ sin namespaces ni cabeceras propias.

---

## 13. Reglas del lenguaje (las que de verdad importan)

- `:` al final de `fn/type/enum/import/foreign/if/elif/else/while/for/match/case` (como Python). Sin punto y coma.
- Sangría con **espacios**; mezclar tabs y espacios en la sangría es error E0104. Una línea que acaba en operador binario continúa en la siguiente.
- Los tipos de parámetros/retorno/campos **se anotan**; los de locales se infieren.
- `/` entre enteros es **división entera** (C++/Rust); usa literales decimales para división real. `%` solo con enteros.
- `int` es de 64 bits con signo; el desbordamiento no está definido (como en C++).
- `for I in 0..N` es **exclusivo** por la derecha (como `range(0, N)`).
- Iterar un mapa produce sus **claves** (ordenadas, para que el output sea determinista).
- `let` es inmutabilidad **profunda**: no puedes mutar campos de un `let`. Las vinculaciones de `match` son de solo lectura.
- **Los elementos que ENTRAN en un array/mapa se mueven**: `Pila.Push(N)` deja `N` vacío (usarlo después es E0501); lo mismo en literales `[N, M]` y valores de mapa. Si lo necesitas también, `Clone(N)`.
- Los elementos que SALEN se copian: `P[0]`, iterar un array o `Pop()` devuelven una copia.
- Los nombres que empiezan por `Sylt` (runtime) y `sy_` (primitivas de la std) están reservados.
- `Clone(x)` copia explícitamente cualquier cosa, incluidos structs y enums.
- `Str(x)` e interpolación imprimen estructuras como `Vec2{X: 3.0, Y: 4.0}`, enums como `Enviado{tracking: SY-42}`, arrays `[1, 2, 3]` y mapas `{Ana: 31}`.
- Comentarios con `#` hasta fin de línea. Conviene que los identificadores vayan en **PascalCase**, es el estilo de los ejemplos y de la std, no una norma del compilador.

---

## 14. Hoja de ruta

**v0.3, backend LLVM**: emisor de IR textual (mismo AST, mismo checker), runtime de strings en IR puro, verificación diferencial contra el backend cpp con llc/clang.

**v0.4.0, ergonomía**: genéricos ligeros `type Caja[T]` con monomorfización y métodos, mover elementos hacia colecciones (`Push`, literales y mapas mueven), pruebas integradas `sylt test` + `Afirma`.

**v0.5.0, JSON y HTTP**: `std.json` completo sobre un enum recursivo, `Str(x, decimales)`, keep-alive en el servidor HTTP, correcciones de codegen descubiertas al construir JSON.

**v0.5.1, concurrencia y llvm completo**: hilos (`std.hilo`) con servidor multihilo de demostración, backend llvm con colecciones, `std.*` y `sylt test`.

**v0.6.0, herramientas (esta versión)**: `syltc fmt` con estilo canónico y `--check` para CI, `syltc lsp` con diagnósticos en vivo y formato desde el editor, `syltc pkg` con paquetes locales y de git.

**v0.7, en marcha**: hover y autocompletado en el lsp, registro público de paquetes con versiones, `syltc doc`.

---

## 15. Referencia rápida de errores

| Código | Significado |
|---|---|
| E0101-E0104 | Léxico: carácter/escape/indentación |
| E0201-E0204 | Sintaxis: falta un token, asignación inválida, `return` fuera de sitio |
| E0210-E0211 | Pruebas: `sylt test` sin ninguna `Prueba*`, prueba no ejecutable |
| E0301-E0305 | Nombres: variable/función/tipo inexistente (con sugerencia), redeclaración |
| E0310-E0314 | Genéricos: sin argumentos de tipo, aridad equivocada, argumento inválido, no-genérico con corchetes, enum genérico |
| E0401-E0408 | Tipos: incompatible, condición no-bool, no-iterable, falta `return`, struct recursivo |
| E0501-E0504 | Movimiento y préstamos: use-after-move, asignar a movido, préstamos conflictivos, mutar inmutable |
| E0601-E0603 | Enums: match no exhaustivo, patrón/variante equivocado, campo compartido con tipos distintos |
| E0701-E0702 | Módulos: no encontrado, importación circular |
| E0801 | foreign: tipo no admitido en la frontera C++ |
| E0901 | backend llvm: construcción no soportada todavía (el núcleo, colecciones, `std.*` y pruebas sí lo están) |
| W0001 | Variable declarada y no usada (nómbrala `_X` para silenciar) |

---

## 16. Estructura del proyecto

```text
sylt/
├── Makefile              # make (compila), make test, make install
├── .gitignore            # excluye el binario syltc y restos de compilación
├── src/                  # el compilador en C++17
│   ├── common.hpp        #   spans, fuente, Levenshtein (sugerencias)
│   ├── diag.*            #   diagnósticos estilo rustc (cajas, notas, ayuda)
│   ├── lex.*             #   lexer con sangría significativa y continuaciones
│   ├── ast.*             #   tipos del lenguaje + AST
│   ├── parse.*           #   parser descendente + Pratt + interpolación
│   ├── check.*           #   checker: tipos + enums + módulos + movimiento
│   ├── gen.*             #   backend cpp: generador de C++17
│   ├── genllvm.*         #   backend llvm: emisor de IR textual
│   ├── loader.hpp        #   carga de módulos, paquetes y rutas de instalación
│   ├── json.*            #   JSON mínimo (protocolo del lsp y metadatos de pkg)
│   ├── fmt.*             #   formateador con estilo canónico
│   ├── lsp.*             #   servidor de lenguaje sobre stdio
│   ├── pkg.*             #   gestor de paquetes (add/list/remove)
│   └── main.cpp          #   CLI (run/build/check/test/emit/fmt/lsp/pkg)
├── sylt_prelude.hpp      # runtime (RAII/STL + primitivas sy_* de la std)
├── lib/std/              # librería estándar escrita en Sylt (str, io, time, net, http, json, hilo)
├── examples/             # Hola, Fib, Figuras, Colecciones, Prestamos, Estados,
│   │                     # Modulos, Externo, Genericos, Pruebas, ServidorHttp,
│   │                     # ServidorHilos, Json, Hilos, BenchJuego...
│   └── errores/          # archivos con errores a propósito (galería del README)
├── tests/
│   ├── run_tests.sh      # suite: ejemplos, errores esperados, sylt test, fmt,
│   │                     #   pkg, lsp, servidor HTTP, bench
│   ├── lsp_prueba.py     # cliente JSON-RPC que prueba el lsp de punta a punta
│   ├── llvm_diff.py      # batería diferencial cpp vs llvm (llc + clang)
│   └── llvm/             # programas de prueba para el backend llvm
└── README.md
```

*Los ejemplos de esta guía usan los nombres en español y PascalCase acordados para Sylt.*
