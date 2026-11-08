# Núcleo híbrido C++20 + Rust

## 1.3 Arquitectura del Puente Híbrido C++20 ↔ Rust (`libvegaflow_core.a`)

Para garantizar que el propio software de optimización no introduzca latencia ni compita por la memoria del sistema, toda la lógica algorítmica y de evaluación en tiempo real fue desarrollada en **Rust**, compilada como `staticlib` sin dependencia de la biblioteca estándar de C runtime (CRT) en el bucle caliente, operando exclusivamente sobre estructuras POD (*Plain Old Data*) `#[repr(C)]` ubicadas en la pila:

```mermaid
flowchart LR
    subgraph CPP["Capa de Sistema C++20 (Win32 / DXGI / GUI)"]
        direction TB
        MP["main.cpp<br/>Bucle de servicio y despacho"]
        HP["hardware_profiler.cpp<br/>Topología Win32 y detección"]
        CO["cpu_orchestrator.cpp<br/>Gestión de Handles de proceso"]
        MM["memory_manager.cpp<br/>Presupuesto DXGI 1.4"]
        SG["sandbox_guard.cpp<br/>Validación perimetral"]
    end

    subgraph ABI["Puente C ABI #[repr(C)]"]
        RB["rust_bridge.h<br/>Estructuras POD de tamaño fijo"]
    end

    subgraph RUST["Núcleo Determinista en Rust (rust/vegaflow-core)"]
        direction TB
        CG["cpu_governor.rs<br/>• vf_rust_partition_cpu_sets()<br/>• vf_rust_evaluate_bg_confinement()"]
        UM["uma_memory.rs<br/>• vf_rust_evaluate_uma_pressure()<br/>• vf_rust_query_memory_lists()"]
        SC["sandbox_core.rs<br/>• vf_rust_check_path_boundary()<br/>• vf_rust_detect_anticheat_path()"]
    end

    subgraph KERNEL["Kernel NT (ntdll.dll / kernel32.dll)"]
        direction TB
        NT1["NtQuerySystemInformation(Class 80)<br/>NtSetSystemInformation(Purge Standby)"]
        NT2["SetProcessDefaultCpuSets<br/>SetProcessInformation(EcoQoS)<br/>NtSetInformationProcess(I/O VeryLow)"]
    end

    HP --> RB
    CO --> RB
    MM --> RB
    SG --> RB
    RB --> CG
    RB --> UM
    RB --> SC
    UM --> NT1
    CO --> NT2

    style CPP fill:#1b2432,stroke:#3b82f6,color:#f3f4f6
    style ABI fill:#2b2338,stroke:#a855f7,color:#f3f4f6
    style RUST fill:#331d17,stroke:#f97316,color:#f3f4f6
    style KERNEL fill:#182b29,stroke:#14b8a6,color:#f3f4f6
```

### Módulos del Núcleo Rust:
* **[`cpu_governor.rs`](../../rust/vegaflow-core/src/cpu_governor.rs):** Recibe la lista plana de identificadores de CPU Sets del sistema, calcula los índices de pertenencia a complejos de núcleos y determina si un proceso ruidoso debe ingresar o salir del estado de confinamiento mediante un algoritmo de histéresis sin asignaciones dinámicas.
* **[`uma_memory.rs`](../../rust/vegaflow-core/src/uma_memory.rs):** Enlaza directamente contra `ntdll.dll` invocando `NtQuerySystemInformation(SystemMemoryListInformation)`. Evalúa en microsegundos si el subsistema de memoria compartida supera el umbral crítico (`>= 85%` de presupuesto o `< 1536 MB` de RAM física disponible), gobernando la purga selectiva de la lista de espera (*Standby List*) únicamente cuando el bus de disco opera en reposo (`< 15 MB/s`).
* **[`sandbox_core.rs`](../../rust/vegaflow-core/src/sandbox_core.rs):** Escanea secuencias de caracteres UTF-16 en tiempo constante. Si identifica patrones correspondientes a sistemas anti-trampas comerciales o directorios del sistema protegidos, activa un estado de *Lockdown* inmediato, impidiendo modificaciones en el árbol de archivos.

---

## 1.4 Ciclo de Vida del Daemon y Gobernanza de Procesos

El diagrama de secuencia describe el ciclo de vida continuo del servicio desde su arranque en sesión elevada hasta la terminación de la aplicación:

```mermaid
sequenceDiagram
    autonumber
    actor U as Usuario / Sistema
    participant D as Daemon C++20 (main.cpp)
    participant R as Núcleo Rust (vegaflow-core)
    participant K as Kernel Windows NT / Driver GPU
    participant G as Proceso de Juego 3D

    U->>D: Inicia servicio vegaflow_daemon.exe
    D->>R: vf_rust_partition_cpu_sets(topología lógica)
    R-->>D: Partición calculada: Game Sets [0..9] | Background [10..11]
    D->>K: Crea respaldo en Registro (RegBackup) y configura EPP

    loop Cada 1.5 s en reposo (0.00% CPU, 1.99 MB RAM)
        D->>K: Monitorea ventana en primer plano (GetForegroundWindow)
    end

    U->>G: Ejecución del Juego (ej. Octopath Traveler II)
    D->>R: vf_rust_detect_anticheat_path(ruta del binario)
    alt Presencia de Anti-Cheat (ej. HoYoKProtect / BattlEye)
        R-->>D: Retorna LOCKDOWN -> Se deshabilitan escrituras en disco
    else Binario sin Anti-Cheat
        R-->>D: Perfil estándar -> Aplica Preset optimizado de motor
    end

    loop Durante la sesión de juego
        D->>R: vf_rust_evaluate_bg_confinement(telemetría de CPU de procesos)
        alt Proceso de fondo supera umbral (CPU >= 0.5)
            R-->>D: Confinamiento requerido
            D->>K: Aplica Afinidad 0xC00 + EcoQoS + I/O VeryLow
        else Proceso de fondo cae bajo umbral (CPU < 0.25)
            R-->>D: Liberación permitida
            D->>K: Restaura Afinidad global y Prioridad normal
        end
        D->>R: vf_rust_evaluate_uma_pressure()
        opt Presión de memoria crítica (>85% y disco inactivo)
            R->>K: NtSetSystemInformation (Purga Standby List)
        end
    end

    U->>G: Cierre del Juego
    D->>K: RestoreAll() -> Revierte afinidades, prioridades, energía y registro
```

---


---

## 1.6 Doble Capa: Explicación de Ingeniería Profunda vs. Explicación Sencilla

### Capa 1: Perspectiva de Ingeniería Profunda
En la prueba sintética, confinar el juego a un subconjunto de núcleos (*Core Pinning* del juego) restaba un **-18.9%** de rendimiento debido a que los hilos auxiliares de compilación y despacho D3D11 quedaban asfixiados. La arquitectura óptima es **asimétrica**: mantener el juego con acceso libre a todos los núcleos físicos y reubicar de manera forzada únicamente a los intrusos en el último complejo físico. Al confinar los 6 workers a los hilos lógicos 10 y 11, la caché L3 asociada a los núcleos 0 a 4 permanece libre de desalojos forzados, permitiendo que la tasa de aciertos (*Cache Hit Rate*) supere el 96%, reduciendo los ciclos de parada de ejecución (*Stall Cycles*) en la tubería de despacho de comandos de la GPU.

### Capa 2: Explicación Sencilla y Cotidiana (La Autopista de 6 Carriles y los Chefs de Cocina)
* **La Autopista de 6 Carriles:** Imagina una autopista de 6 carriles donde un automóvil deportivo (el juego) necesita viajar a 100 km/h sin frenar. Sin regulación, una docena de camiones de carga pesada y furgonetas de reparto (navegadores, actualizaciones, tareas del sistema) circulan zigzagueando por todos los carriles. Aunque el deportivo sea potente, se verá obligado a frenar en seco cada vez que un camión se le cruce en su carril. VegaFlow actúa como un policía de tránsito de alta velocidad: no le pone límites al vehículo prioritario, pero **obliga a todos los camiones pesados a circular exclusivamente por el carril lento de la extrema derecha**. Con los otros 5 carriles despejados, el vehículo rueda a máxima velocidad sin interrupciones.
* **El Restaurante y los Chefs:** Un procesador de 6 núcleos funciona como 6 cocineros en la misma cocina. Si los clientes de una fiesta ruidosa entran a la cocina y le piden favores a los 6 cocineros a la vez, los cocineros pierden las comandas del plato principal y la comida sale con retraso (los tirones en pantalla). VegaFlow asigna a **un solo cocinero en una barra auxiliar para atender a los invitados ruidosos**, dejando a los otros 5 cocineros dedicados por completo a servir el banquete del juego.

---

