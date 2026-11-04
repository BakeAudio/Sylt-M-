# Sylt

Sylt es un lenguaje de programación compilado. Se escribe con la sintaxis por indentación de Python, se transpila a C++17 y se compila a binario nativo. Incluye un sistema ligero de movimiento y préstamos inspirado en Rust, comprobado por el compilador.

- **Velocidad nativa**: sin máquina virtual, sin intérprete, sin recolector de basura. El binario generado compila con `g++` o `clang++` y `-O2`.
- **Sintaxis por indentación**: bloques con sangría, sin punto y coma, strings con interpolación `"{Nombre}"`.
- **Comprobación en compilación**: uso después de mover, préstamos conflictivos, mutación de inmutables y `match` no exhaustivo son errores, no advertencias.
- **JSON incluido**: `std.json` parsea y serializa con un enum recursivo `JsonValor`, con errores que indican la posición exacta del problema.
- **Acceso al ecosistema C++**: un bloque `foreign` declara funciones de cualquier cabecera C/C++ y se llaman a velocidad nativa, sin FFI.
- **Herramientas del lenguaje**: formateador con estilo canónico (`syltc fmt`), servidor de lenguaje para editores (`syltc lsp`) y gestor de paquetes (`syltc pkg`), todo en el mismo binario.

Versión actual: **0.6.0**. Requisitos: `g++` o `clang++` con soporte de C++17. Probado en Linux (x86-64). Licencia no comercial ([ver licencia](#licencia)).

---

## Un ejemplo

`examples/Estados.sy`, tal como está en el repositorio:

```sylt
enum EstadoPedido:
    Pendiente
    Pagado
    Enviado(tracking: string)
    Entregado(dias: int)
    Cancelado(motivo: string)

fn Describir(E: EstadoPedido) -> string:
    match E:
        case Pendiente:
            return "esperando pago"
        case Pagado:
            return "pagado, preparando envío"
        case Enviado(T):
            return "en camino con código {T}"
        case Entregado(D):
            return "entregado en {D} días"
        case Cancelado(M):
            return "cancelado: {M}"

fn Main():
    let Pedido = EstadoPedido.Enviado("SY-2026-0042")
    Print("estado inicial: {Pedido}")
    Print(Describir(Pedido))
```

Salida:

```text
estado inicial: Enviado{tracking: SY-2026-0042}
en camino con código SY-2026-0042
```

Si mañana se añade una variante al enum, el compilador señala cada `match` que quedó corto, indicando el nombre de la variante que falta.

---

## Instalación

```bash
cd sylt
make
./syltc run examples/Hola.sy
```

`make` compila el compilador (`syltc`) en unos 30 segundos. `make test` ejecuta la suite de pruebas y `make install` instala el compilador, el runtime y la biblioteca estándar en `~/.local/share/sylt/`, desde donde funciona en cualquier carpeta:

```bash
make install
syltc run examples/Hola.sy
```

`syltc` es el compilador: un ejecutable, como `python` o `rustc`, pero para Sylt. Problemas frecuentes:

| Mensaje | Causa | Solución |
|---|---|---|
| `command not found` | Linux solo busca ejecutables en el `PATH` | Llámalo como `./syltc` desde su carpeta, o usa `make install` |
| `Permission denied` | El permiso de ejecución se perdió al copiar o descomprimir | `chmod +x syltc` |
| `Exec format error` | El binario fue compilado para otra arquitectura | Compílalo en tu máquina: `make` |
| `no encuentro sylt_prelude.hpp` | El binario se movió fuera de su carpeta | `make install`, o pasa `--prelude=RUTA` |

---

## El lenguaje

### Variables

`let` declara inmutables, `var` mutables. El tipo de las locales se infiere; el de parámetros, retornos y campos se anota.

```sylt
let Saldo = 1000
var Veces = 0
Veces += 1
let Ratio: float = 3
```

Modificar un `let` es error de compilación (E0504), y las variables sin inicializar no existen: `let X` sin `=` no compila.

### Funciones y control de flujo

```sylt
fn MinMax(A: int, B: int) -> (int, int):
    if A < B:
        return (A, B)
    return (B, A)

let (Menor, Mayor) = MinMax(14, 7)

for I in 0..10:
    Print("{I}")
```

`if`/`elif`/`else`, `while`, `for I in 0..N` (el extremo derecho es exclusivo) y `for E in coleccion` para arrays y para las claves de un mapa. Los bloques se delimitan por sangría, y una línea que termina en operador binario continúa en la siguiente.

### Strings con interpolación

Dentro de `{ }` va cualquier expresión:

```sylt
let Edad = 31
Print("{Nombre} tiene {Edad} años y media edad: {Edad / 2.0}")
```

### Colecciones

Arrays `[T]`, mapas `{K: V}` y tuplas:

```sylt
var Numeros = [5, 3, 9]
Numeros.Push(7)
let Esta = Numeros.Contiene(3)

let Edades = { "Ana": 31, "Luis": 25 }
for Nombre in Edades:
    Print("{Nombre}: {Edades[Nombre]}")
```

Los índices fuera de rango y las claves ausentes producen un error de ejecución claro con el valor exacto, no un fallo de segmentación.

### Structs y métodos

```sylt
type Vec2:
    X: float = 0.0
    Y: float = 0.0

    fn Norma(self) -> float:
        return Sqrt(self.X * self.X + self.Y * self.Y)

    fn Escalar(mut self, K: float):
        self.X = self.X * K
        self.Y = self.Y * K

fn Main():
    var A = Vec2(X: 3.0, Y: 4.0)
    Print("norma: {A.Norma()}")
    A.Escalar(2.0)
```

La construcción de un struct siempre es completa: si un campo no tiene valor por defecto y no se pasa, es error (E0403). Un método `mut self` sobre una variable `let` tampoco compila.

### Enums y match

Cada variante puede llevar valores. El `match` es exhaustivo: deben cubrirse todos los casos, o terminar con `case _:`.

```sylt
fn DiasHastaEntrega(E: EstadoPedido) -> int:
    match E:
        case Entregado(D):
            return D
        case _:
            return 0
```

### Genéricos

```sylt
type Pila[T]:
    Items: [T] = []
    N: int = 0

    fn Apila(mut self, V: T):
        self.Items.Push(V)
        self.N += 1
```

Se instancian con tipos explícitos (`Pila[string]()`, `Caja[int](Valor: 42)`) y el compilador monomorfiza: cada instanciación es un struct distinto con sus métodos comprobados por separado. Se anidan a cualquier profundidad (`Pila[Caja[int]]()`).

### Movimiento y préstamos

Los structs se mueven, no se copian. `let D = C` deja `C` vacío; usar `C` después es error E0501. Para copiar: `Clone(C)`. Las funciones pueden pedir préstamos en la firma: `&T` de solo lectura y `&mut T` de lectura-escritura.

```sylt
fn SubirNivel(J: &mut Jugador):
    J.SumarPuntos(100)

fn LeerPuntos(J: &Jugador) -> int:
    return J.Puntos

fn Main():
    var Heroe = Jugador(Nombre: "Ada")
    SubirNivel(&Heroe)
    let N = LeerPuntos(&Heroe)
```

En una misma llamada, una variable no puede prestarse como `&mut` y usarse además de otra forma (E0503). Los elementos que entran en un array o mapa también se mueven; los que salen (`P[0]`, `Pop()`, iterar) se copian.

### Módulos

Un archivo `.sy` es un módulo:

```sylt
import matematicas

fn Main():
    Print("el doble de 21 es {matematicas.Doble(21)}")
```

Todo lo exportado se usa cualificado. Los ciclos de importación se detectan (E0702) y los imports no se propagan entre módulos.

### Interfaz con C++

```sylt
foreign:
    include <cmath>
    fn sin(X: float) -> float
    fn fmod(X: float, Y: float) -> float

fn Main():
    Print("sin(pi/2) = {sin(3.14159265 / 2.0)}")
```

Cada `fn` de un bloque `foreign` es una función C++ real con esa firma: se comprueban los tipos en la frontera y la llamada no tiene intermediarios. Para una librería con tipos complejos, la técnica habitual es escribir en C++ un fichero de funciones planas y declararlas aquí.

### Pruebas integradas

Toda función global `fn PruebaNombre()` sin parámetros es una prueba:

```sylt
fn PruebaDoble():
    Afirma(Doble(2) == 4, "Doble(2) debería ser 4")
```

```bash
$ syltc test examples/Pruebas.sy
ok PruebaDoble
ok PruebaTextos
4 pruebas, 0 fallos
```

El informe continúa tras un fallo indicando la línea, y el código de salida es 1 si algo falló, encaja en CI junto a `syltc check`.

### Detalles que conviene saber

- `/` entre enteros es división entera; `%` solo existe para enteros.
- `int` es de 64 bits con signo.
- `let` es inmutabilidad profunda: tampoco se pueden mutar campos de un `let`.
- Iterar un mapa produce sus claves, en orden (la salida es determinista).
- Comentarios con `#` hasta fin de línea. Los identificadores van en `PascalCase` en los ejemplos y en la biblioteca estándar.
- Los nombres que empiezan por `Sylt` y `sy_` están reservados para el runtime.

---

## Errores del compilador

Los mensajes siguen el formato de rustc: código, archivo y posición, la línea con un caret señalando el problema, notas con el origen y ayuda con la solución. Salida real de `syltc`:

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

En `examples/errores/` hay 13 archivos que no compilan a propósito, con su error esperado; la suite los comprueba uno a uno.

---

## Herramientas

### Formateo: syltc fmt

```bash
syltc fmt archivo.sy          # formatea en el sitio
syltc fmt src/                # todos los .sy de una carpeta, recursivamente
syltc fmt --check .           # no escribe; avisa qué cambiaría y sale con 1
```

`syltc fmt` aplica el estilo canónico del lenguaje: sangría de 4 espacios, un espacio alrededor de los operadores, `{ "clave": valor }` en los mapas, `fn Nombre(...)` sin espacio antes del paréntesis y un único salto final. Conserva los comentarios en su sitio y los strings byte a byte (escapes e interpolaciones incluidos). Los saltos de línea del autor se respetan: normaliza sangría y espacios, no reenvuelve líneas.

Es idempotente (formatear dos veces deja el mismo texto) y con `--check` encaja en CI para exigir estilo uniforme. Todo el repositorio ya está en el estilo canónico y la suite lo comprueba.

### El servidor de lenguaje: syltc lsp

```bash
syltc lsp
```

Habla JSON-RPC 2.0 por stdin/stdout, el protocolo estándar de los editores. Al abrir o editar un archivo ejecuta el mismo lexer, parser y checker que `syltc check` y publica los errores con código, posición y ayuda; la petición de formato del editor devuelve lo que produciría `syltc fmt`.

Lo que ves subrayado en el editor es exactamente lo que `syltc build` dirá: no hay un segundo checker que se desincronice. Vale cualquier cliente LSP; basta asociar `syltc lsp` a los archivos `.sy`.

### Paquetes: syltc pkg

Un paquete es una carpeta con un módulo principal `nombre.sy` y los módulos que necesite junto a él. Se instala en el registro local (`~/.local/share/sylt/pkg/`) desde una carpeta o desde git:

```bash
syltc pkg add ./mi-paquete                               # carpeta local
syltc pkg add https://github.com/usuario/paquete         # clona e instala
syltc pkg list                                           # qué hay instalado
syltc pkg remove paquete                                 # desinstala
```

Tras instalarlo, `import nombre` lo encuentra igual que un módulo local: primero junto al archivo que importa, después en el registro. Cada instalación deja origen y versión anotados en `<nombre>.meta`. La ruta del registro se cambia con `--pkg=RUTA` o la variable `SYLT_PKG`, útil para registros aislados por proyecto.

---

## Rendimiento

`examples/BenchJuego.sy` simula un game loop de 2.000 entidades durante 120 frames y mide con `TiempoMs()`:

| Configuración (g++ -O2) | FPS |
|---|---|
| Sylt con todas las comprobaciones de seguridad | ~59.000 |
| Sylt `--fast` (sin comprobaciones de índices en caliente) | ~800.000 |
| C++ equivalente, mismo algoritmo | ~800.000 |

Con `--fast` el código generado es equivalente al C++ escrito a mano: no hay capa de interpretación entre medias.

---

## Cómo funciona

```text
archivo.sy  (+ sus import .sy)
   │  lexer: sangría → INDENT/DEDENT, continuaciones, interpolación
   ▼
parser descendente + Pratt → AST
   │
   ▼
checker: tipos, structs, enums + match, módulos, movimiento, préstamos
   │
   ├──▶ generador de C++17 ────▶ g++/clang++ -O2 ──▶ ejecutable nativo
   │
   └──▶ generador de LLVM IR ──▶ llc (verifica) + clang (enlaza)
        (--backend=llvm)
```

El compilador tiene dos backends que comparten AST y checker. El de C++ aprovecha la STL y el optimizador de g++/clang. El de LLVM emite IR textual que pasa el verificador oficial de LLVM, y la suite exige que ambos backends produzcan salida idéntica en sus casos de prueba. El runtime de strings del backend LLVM está escrito en IR puro sobre `malloc`, sin STL.

Con `syltc emit archivo.sy` se puede ver el C++ generado, y con `--backend=llvm` el IR.

---

## Biblioteca estándar

La biblioteca estándar está escrita en Sylt y se apoya en 29 primitivas `sy_*` del runtime (`sylt_prelude.hpp`). Se importa por módulos:

| Módulo | Contenido |
|---|---|
| `std.str` | `Dividir`, `Unir`, `Recortar`, `Mayusculas`, `Reemplazar`, `Subcadena`, `Caracteres` (UTF-8), `AEntero` |
| `std.io` | `LeerLinea`, `LeerArchivo`, `EscribirArchivo`, `AnexarArchivo`, `Existe` |
| `std.time` | `AhoraMs` (reloj de pared), `MonoMs` (monótono) |
| `std.net` | TCP: `TcpConectar`, `TcpEnviar`, `TcpRecibir`, `TcpEscuchar`, `TcpAceptar` |
| `std.http` | `Get(url) -> (codigo, cuerpo)`, escrito en Sylt sobre `std.net` y `std.str` |
| `std.hilo` | `Lanzar(fn) -> id`, `Esperar(id)`, `Dormir(ms)`: hilos reales del sistema |
| `std.json` | `Parse(t) -> (ok, error, JsonValor)`, `Texto(v)`, `DeArchivo`, `HaciaArchivo`; enum recursivo `JsonValor` con `Nulo`, `Logico`, `Numero`, `Texto`, `Lista`, `Objeto` |

`examples/ServidorHttp.sy` es un servidor HTTP con rutas, cabeceras, 404 y keep-alive (varias peticiones por conexión) en unas 70 líneas de Sylt; la suite lo compila, lo levanta, le hace peticiones reales y comprueba que curl reutiliza la conexión.

Para librerías externas, la vía es `foreign`: toda librería C/C++ queda a un bloque de distancia y a velocidad nativa. Las librerías propias se reparten como archivos `.sy` que se importan directamente.

---

## Ejemplos

| Ejemplo | Tema |
|---|---|
| `examples/Hola.sy` | Hola mundo con interpolación |
| `examples/Fib.sy` | Recursión |
| `examples/Figuras.sy` | Structs, métodos, préstamos, movimiento |
| `examples/Estados.sy` | Enums con valores y match exhaustivo |
| `examples/Colecciones.sy` | Arrays, mapas, tuplas, destructuración |
| `examples/Prestamos.sy` | `&T` / `&mut T` y `Clone` |
| `examples/Genericos.sy` | Genéricos monomorfizados |
| `examples/Modulos.sy` | Import entre archivos |
| `examples/Externo.sy` | Bloques `foreign` |
| `examples/Pruebas.sy` | Pruebas integradas con `Afirma` |
| `examples/Json.sy` | `std.json`: parsear, serializar, archivos |
| `examples/Hilos.sy` | Hilos con `std.hilo` |
| `examples/ServidorHttp.sy` | Servidor HTTP con keep-alive |
| `examples/ServidorHilos.sy` | Servidor HTTP multihilo |
| `examples/BenchJuego.sy` | Benchmark del game loop |

---

## Estructura del repositorio

```text
sylt/
├── Makefile              make (compila) · make test · make install
├── .github/              CI (GitHub Actions) y plantillas de issues
├── docs/                 guía completa del lenguaje
├── src/                  el compilador en C++17
│   ├── lex, parse        lexer con sangría significativa + parser Pratt
│   ├── check             tipos, enums, módulos, movimiento, préstamos
│   ├── gen, genllvm      backends C++17 y LLVM IR
│   ├── loader, json      carga de módulos/paquetes + JSON del protocolo
│   ├── fmt, lsp, pkg     formateador, servidor de lenguaje, gestor de paquetes
│   └── main.cpp          CLI: run, build, check, test, emit, fmt, lsp, pkg
├── sylt_prelude.hpp      runtime: RAII/STL + primitivas sy_*
├── lib/std/              biblioteca estándar escrita en Sylt
├── examples/             programas de ejemplo (errores/ con errores a propósito)
└── tests/                suite completa + cliente lsp + verificación diferencial llvm
```

`make test` ejecuta la suite: compila los ejemplos, comprueba que cada archivo de `examples/errores/` produce su código de error, valida las pruebas integradas, exige el estilo canónico con `fmt --check`, instala y borra un paquete, habla con el lsp y levanta los servidores HTTP. Con LLVM instalado, añade la verificación diferencial entre backends; sin él, esa parte se omite sola.

---

## Hoja de ruta

- **v0.5.0**: `std.json` completo (parser, serializador, archivos) sobre enums recursivos, `Str(x, decimales)`, keep-alive en el servidor HTTP, y dos correcciones de codegen descubiertas al construir JSON (tipos recursivos en módulos y strings dentro de tuplas).
- **v0.5.1**: backend llvm completo (colecciones, `std.*`, `sylt test`) y concurrencia con hilos (`std.hilo`, servidor multihilo).
- **v0.6.0 (esta versión)**: `syltc fmt` (estilo canónico, comentarios preservados, `--check` para CI), `syltc lsp` (diagnósticos del checker real en el editor y formato del documento) y `syltc pkg` (paquetes locales y de git en `~/.local/share/sylt/pkg`).
- **v0.7**: hover y autocompletado en el lsp, registro público de paquetes con versiones, `syltc doc`.

---

## Contribuir

Los bugs se reportan en el repositorio con un archivo `.sy` mínimo que lo reproduzca y la salida completa de `syltc`. Para proponer cambios en el diseño del lenguaje, describe primero el problema que quieres resolver. Toda contribución debe dejar `make test` en verde.

---

## Licencia

Sylt se distribuye bajo una licencia propia de uso no comercial; el texto completo está en el archivo `LICENSE` de este repositorio. Para usos comerciales, contacta con el autor. La licencia se revisará cuando el lenguaje alcance la versión 1.0.
