Вот исправленная версия документа **`explanation/architecture-overview.md`**. Маркетинговые термины убраны, акцент смещен на инженерную расширяемость, а синтаксис графа Mermaid скорректирован для безопасного парсинга (добавлены кавычки для текстовых блоков со спецсимволами).

***

# Обзор архитектуры: Microkernel & Внешняя экосистема

Shader Desk спроектирован не как монолитное приложение, а как расширяемая платформа на базе **Микроядра (Microkernel)**. Главная архитектурная цель проекта — предоставить сообществу надежный фундамент для создания сторонних компонентов: C++ плагинов, внешних демонов (на Rust, Go, Python), CLI-утилит, GLSL-шейдеров и комплексных Lua-сцен. 

Архитектура гарантирует, что ошибки в стороннем коде (будь то опечатка в скрипте или утечка памяти во внешнем процессе) не приведут к зависанию или падению сессии Wayland-композитора.

## Высокоуровневая топология

```mermaid
flowchart LR
    %% 1. ГЛОБАЛЬНАЯ СЕТКА: Слева направо. Гарантирует, что левая колонка всегда слева.

    subgraph LEFT_COL [" "]
        direction TB
        %% Направление сверху-вниз для левого столбца
        
        subgraph DATA_PAIRS ["Демон + Provider (N пар, поставляются вместе)"]
            direction TB
            DA1["Audio Daemon<br/>FFTW / PulseAudio"]
            DP_A[".so Provider<br/>Native FFTW Audio Provider"]
            DA1 -. "UNIX DGRAM: AudioDatagram" .-> DP_A

            DA2["Evdev Daemon<br/>Pointer Tracking"]
            DP_P[".so Provider<br/>Evdev Pointer Provider"]
            DA2 -. "UNIX DGRAM: PointerDatagram" .-> DP_P

            DA3["Custom Daemon N<br/>Rust / Go / Python"]
            DP_C[".so Provider N<br/>C++ / произвольный протокол"]
            DA3 -. "UNIX DGRAM: custom payload" .-> DP_C
        end

        subgraph VPLUGINS ["Визуальные плагины (N штук)"]
            direction TB
            VP1[".so Plugin A<br/>GLSL / C++"]
            VP2[".so Plugin B<br/>GLSL / C++"]
            VP1 ~~~ VP2
        end
        
        %% Жестко ставим плагины под демонами
        DATA_PAIRS ~~~ VPLUGINS
    end

    subgraph RIGHT_COL [" "]
        direction TB
        %% 2. ПРАВАЯ КОЛОНКА: Сверху вниз. Гарантирует, что TOP_ROW будет строго над KERNEL.

        subgraph TOP_ROW [" "]
            direction LR
            %% Горизонтальная строка для верхних блоков
            
            subgraph SCENES ["Сцены и конфиги (N репозиториев)"]
                direction TB
                SC1["init.lua + scenes/*.lua"]
                SC2["providers.lua"]
                SC1 ~~~ SC2
            end
            
            subgraph CLIENTS ["CLI / GUI утилиты (N клиентов)"]
                direction TB
                CL1["shader-desk-ctl<br/>(CLI)"]
                CL2["GTK / Qt конфигуратор<br/>(сторонний GUI)"]
                CL1 ~~~ CL2
            end
            
            %% Жестко ставим их бок о бок
            SCENES ~~~ CLIENTS
        end

        subgraph KERNEL ["Микроядро Shader Desk"]
            direction TB
            
            IPC_CTL(("Control Socket<br/>SOCK_STREAM"))
            LUA["Control Plane<br/>Lua Engine (LuaJIT)"]
            BB[("BlackBoard<br/>Zero-Copy Data Bus")]
            PM["Plugin Manager<br/>dlopen + Shadow-Commit"]
            DP_SLOT["Активные провайдеры данных<br/>(инстансы в памяти)"]
            VP_SLOT["Активные визуальные плагины<br/>(инстансы в памяти)"]
            RM["Render Pipeline<br/>Ping-Pong FBO"]

            %% Логика внутри ядра (естественное ветвление сделает его прямоугольным 4:3)
            IPC_CTL -->|"eval Lua string"| LUA
            LUA <-->|"bind_float: камера и т.д."| BB
            LUA -->|"core.providers: конфиг параметров"| DP_SLOT
            LUA -->|"граф сцены: слои, плагины по монитору"| PM
            PM -->|"instantiate"| VP_SLOT
            PM -->|"instantiate"| DP_SLOT
            DP_SLOT -->|"bind_float"| BB
            BB -->|"uniforms, O(1) read"| RM
            RM -->|"render(dt)"| VP_SLOT
        end
        
        W["Wayland Compositor<br/>EGL / Layer Shell"]

        %% Выстраиваем их по вертикали с помощью невидимых связей
        TOP_ROW ~~~ KERNEL
        KERNEL ~~~ W
    end

    %% Жестко связываем левую и правую макро-колонки
    LEFT_COL ~~~ RIGHT_COL

    %% =======================================================
    %% КРОСС-БЛОЧНЫЕ СВЯЗИ (теперь они не ломают макет)
    %% =======================================================
    
    %% Слева-направо:
    DP_A -. "dlopen" .-> PM
    DP_P -. "dlopen" .-> PM
    DP_C -. "dlopen" .-> PM
    VP1 -. "dlopen" .-> PM
    VP2 -. "dlopen" .-> PM

    %% Сверху-вниз:
    CL1 -. "UNIX STREAM" .-> IPC_CTL
    CL2 -. "UNIX STREAM" .-> IPC_CTL
    SC1 -. "VFS + inotify" .-> LUA
    SC2 -. "VFS + inotify" .-> LUA

    RM ==>|"EGL swap buffers"| W

    %% =======================================================
    %% СТИЛИЗАЦИЯ И СКРЫТИЕ СЛУЖЕБНОЙ СЕТКИ
    %% =======================================================
    
    classDef eco fill:#fff3e0,stroke:#e65100,stroke-width:1px,stroke-dasharray: 3 3,color:#5d4037;
    classDef kernelNode fill:#e3f2fd,stroke:#0d47a1,stroke-width:1px,color:#0d47a1;
    classDef sink fill:#eceff1,stroke:#263238,stroke-width:2px,color:#263238;

    class DA1,DP_A,DA2,DP_P,DA3,DP_C,VP1,VP2,CL1,CL2,SC1,SC2 eco
    class IPC_CTL,LUA,BB,PM,DP_SLOT,VP_SLOT,RM kernelNode
    class W sink

    %% Делаем контейнеры сетки полностью невидимыми
    style LEFT_COL fill:none,stroke:none
    style RIGHT_COL fill:none,stroke:none
    style TOP_ROW fill:none,stroke:none
    
    style DATA_PAIRS fill:#fffaf3,stroke:#e65100,stroke-width:1px
    style VPLUGINS fill:#fffaf3,stroke:#e65100,stroke-width:1px
    style SCENES fill:#fffaf3,stroke:#e65100,stroke-width:1px
    style CLIENTS fill:#fffaf3,stroke:#e65100,stroke-width:1px
    style KERNEL fill:#f4f8ff,stroke:#0d47a1,stroke-width:2px
```

## 1. Микроядро и Контрольная плоскость (Control Plane)
Микроядро отвечает исключительно за управление жизненным циклом ресурсов, мультиплексирование ввода-вывода (`epoll`) и связь с Wayland. Оно не содержит графической или бизнес-логики.

**Control Plane** реализована на базе интерпретатора LuaJIT. Lua выступает в роли маршрутизатора конфигурации:
* Определяет, какие плагины (`.so`) загрузить на какие физические мониторы.
* Управляет математикой камеры и анимациями в хуке `on_frame(dt)`.
* Принимает команды от внешних утилит (через `shader-desk-ctl`) и изменяет состояние графического конвейера на лету.

## 2. Изоляция границ (Hourglass Pattern)
Загрузка пользовательских C++ плагинов в адресное пространство ядра несет риск повреждения памяти из-за несовместимости ABI (разные версии компиляторов, различные реализации STL-контейнеров, таких как `std::string` или `std::vector`).

Shader Desk решает это с помощью строгого **C-ABI (Паттерн "Песочные часы")**.
Граница между ядром и `.so` библиотекой сведена к передаче POD-структур (Plain Old Data). Обмен параметрами происходит через `ParamValueABI` — строго выровненное `alignas(8)` объединение (union) размером ровно 512 байт. Это гарантирует бинарную совместимость модулей независимо от среды компиляции плагина, полностью исключая утечки памяти на границе библиотек.

## 3. Шина данных BlackBoard ($O(1)$ Routing)
Поскольку кадр должен рендериться за ~6 миллисекунд (для мониторов 144Hz), передача данных от ОС к шейдеру не может использовать хэш-таблицы, парсинг строк или аллокации.

**BlackBoard** — это центральная шина данных микроядра.
1. При инициализации плагин или демон "биндит" ключ (например, `audio.eq_curve`).
2. BlackBoard выделяет непрерывный массив в памяти и возвращает "сырой" указатель (`float*`).
3. В цикле рендера обмен сотнями параметров происходит за $O(1)$ путем прямой записи/чтения по предзакэшированному указателю.

**Защита от Segfault (Trash Buffers):** Если плагин запрашивает несуществующий ключ или выходит за пределы выделенной памяти, BlackBoard бесшовно подменяет указатель на статический `Trash Buffer` (размещенный в сегменте `.bss`). Это предотвращает падение композитора при агрессивном или некорректном чтении памяти сторонним плагином.

## 4. Zero-Latency IPC (Drain Pattern)
Внешние демоны (например, чтение аудио-спектра из PipeWire или координат мыши из `/dev/input`) выполняются как отдельные независимые процессы. Они передают данные в ядро через **файловые UNIX-сокеты типа `SOCK_DGRAM`**.

В главном `epoll` цикле ядро применяет паттерн **Drain (Осушение)**:
Вместо чтения одного пакета за кадр, ядро выгребает сокет в цикле `while(true)` до получения сигнала `EAGAIN / EWOULDBLOCK` от планировщика Linux. Для рендера используется только самая последняя, актуальная структура данных. Это нивелирует буферизацию ОС и обеспечивает нулевую задержку между событием в системе (например, басом в музыке) и реакцией шейдера на экране.

## 5. Отказоустойчивость (Fault Tolerance)
Платформа, ориентированная на расширение силами сообщества, подразумевает, что сторонний код может содержать ошибки. Ядро реализует два уровня изоляции сбоев:

1. **Shadow-Commit (Hot-Reload):** При сохранении пользователем измененного `.glsl` файла, ядро инстанцирует теневую копию плагина и пытается скомпилировать EGL-пайплайн в фоне. Если в шейдере есть синтаксическая ошибка, теневая копия уничтожается. Сессия Wayland продолжает рендерить предыдущий успешный кадр без прерываний.
2. **Native EGL Context Recovery:** Движок перехватывает сигнал `EGL_CONTEXT_LOST` (возникающий при выходе драйвера NVIDIA/Mesa из спящего режима). Ядро не завершает процесс (что привело бы к миганию черного экрана), а детерминированно разрушает старый графический конвейер, переинициализирует EGLDisplay и пересоздает все `.so` плагины на лету, восстанавливая их состояние из Lua Control Plane.

***

### Следующие шаги
* Спецификация структур памяти: [C-ABI Specification](../reference/c-abi-spec.md) *(Reference)*
* Детали работы графического конвейера: [Zero-Allocation Loop & Ping-Pong FBO](zero-allocation-loop.md) *(Explanation)*