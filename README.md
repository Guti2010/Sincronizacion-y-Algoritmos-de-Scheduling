# 🏭 Línea de Ensamblaje con C, POSIX y Docker

Simulación de una **línea de ensamblaje con tres estaciones (E1, E2, E3)** usando **C**, **pipes**, **colas thread-safe** (mutex + semáforos POSIX), **`fork()` + `pthread`**, y políticas **FCFS** y **Round Robin (RR)** con **quantum configurable**. Se ejecuta con **Docker** y **Docker Compose** *(sin Makefile)*.

---

## ✨ Características

- **3 estaciones** en **procesos separados** (cada una con 1 **hilo lector** + 1 **hilo worker**).
- **Comunicación** entre estaciones vía **pipes**; dentro de cada estación hay **cola thread-safe** (productor/consumidor).
- **FCFS (E1)** y **RR (E2/E3)** con **quantum configurable** (p. ej. `200 ms`).
- **Bursts por estación** (fijos y comunes a todos los productos): `E1=400 ms`, `E2=600 ms`, `E3=300 ms` → **burst total** por producto **= 1.300 s**.
- **Tiempos de llegada simulados**: `0, 1, 2, … s`.
- **Registro de slices** para imprimir un **Gantt** por estación (inicio–fin por producto).
- **Resumen final**: promedio **WT**, promedio **TAT**, y **orden final** (IDs en el orden en que salen de E3).

---

## 📊 Métricas por producto

| Campo | Descripción |
|------:|-------------|
| `arrival_s` | Tiempo de llegada simulado (seg.) |
| `t_in_s[i] / t_out_s[i]` | Entrada/salida en cada estación `i=0..2` (seg., relativos a `epoch_s`) |
| `TAT_total` | `t_out_s[2] − arrival_s` |
| `WT_total` | `TAT_total − (burst_E1 + burst_E2 + burst_E3)` *(en segundos)* |

> **Resumen (en E3):** `avg(WT_total)`, `avg(TAT_total)` y **orden final** de IDs.

---

## 🧰 Requisitos

- **Docker** ≥ 20.x  
- **Docker Compose V2**

---

## ▶️ Cómo ejecutar

```bash
# Primera vez: compila el binario y arranca el contenedor
docker compose up --build

# Siguientes ejecuciones
docker compose up

# Limpiar (contenedores + volúmenes)
docker compose down --volumes
