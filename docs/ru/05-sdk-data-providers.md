Ниже представлен подробный и технически строгий раздел **05-sdk-data-providers.md**. Он описывает архитектуру Data Pipeline, основанную на изоляции процессов, неблокирующих сокетах и интеграции с `epoll` микроядра.

---

# Разработка провайдеров данных (Data Pipeline)

Для создания по-настоящему интерактивных обоев графическому ядру необходимы данные из внешнего мира: системный звук, движения мыши, погода, статус батареи или метаданные текущего трека (MPRIS).

Однако системные API ОС (такие как `libpulse`, `libevdev` или D-Bus) часто бывают блокирующими, медленными или нестабильными. Если вызывать их напрямую в цикле рендеринга композитора (до 144 раз в секунду), любое зависание звукового сервера приведет к зависанию Wayland-сеанса.

Архитектура Shader Desk решает эту проблему строгим разделением на два компонента:
1. **Standalone-демон (Процесс ОС):** Автономный процесс, который "общается" с ОС, выполняет тяжелую математику (например, Быстрое преобразование Фурье) и спамит готовыми пакетами в UNIX-сокет.
2. **In-Process Провайдер (C++ Плагин):** Легковесная `.so` библиотека внутри ядра, которая интегрирована в `epoll`, читает сокет с нулевой задержкой и пишет данные в шину `BlackBoard` для шейдеров.

---

## 1. Бинарный контракт (Datagram)

Первый шаг в создании нового источника данных — определить структуру пакета, который демон будет отправлять провайдеру. Эта структура должна быть идентичной в коде демона и коде плагина.

**Правила проектирования контракта:**
* Используйте `stdint.h` (например, `uint32_t`, `float`).
* Оберните структуру в `#pragma pack(push, 1)` для отключения выравнивания компилятором.
* Добавьте "магическое число" для примитивной валидации пакета.
* Зафиксируйте размер через `static_assert`.

*Пример `my-data.hpp`:*
```cpp
#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct MyDatagram {
    uint32_t magic = 0x4D594454; // "MYDT"
    float value_x;
    float value_y;
    uint8_t status_flag;
    uint8_t padding[3] = {0,0,0}; // Явное выравнивание вручную
};
#pragma pack(pop)

static_assert(sizeof(MyDatagram) == 16, "MyDatagram alignment failed");
```

---

## 2. Разработка Standalone-демона

Демон — это независимый исполняемый файл (Executable). Он может быть написан на любом языке, но обычно пишется на C++.

Его задача — читать API системы и отправлять структуру в абстрактный UNIX-сокет типа `SOCK_DGRAM` (датаграммы). Соединение работает без установки сессии (connectionless).

**Ключевой момент:** При отправке данных (`sendto`) **категорически обязательно** использовать флаг `MSG_DONTWAIT`. Если ядро Shader Desk закрыто или не успевает читать буфер сокета, демон не должен заблокироваться — он должен просто отбросить пакет (Drop) и продолжить работу.

*Пример минимального демона:*
```cpp
#include "my-data.hpp"
#include <shader-desk/ipc-utils.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>

int main() {
    int sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-myplugin");
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    socklen_t addr_len = sizeof(sa_family_t) + socket_path.length() + 1;

    while (true) {
        // 1. Получение данных от ОС (блокирующий вызов разрешен)
        // float x = os_api_get_x();
        
        // 2. Формирование пакета
        MyDatagram data{}; // {} зануляет паддинг
        data.value_x = 1.0f; // Данные от ОС
        
        // 3. Отправка с MSG_DONTWAIT (Zero-latency Fire-and-Forget)
        sendto(sockfd, &data, sizeof(data), MSG_DONTWAIT, (struct sockaddr*)&addr, addr_len);
        
        usleep(16000); // ~60 FPS
    }
    return 0;
}
```

---

## 3. Разработка плагина-провайдера (.so)

Провайдер — это C++ класс, наследующий `IDataProvider` (обертка над `IDataProviderABI`). Он загружается графическим ядром, биндит сокет и регистрирует его в главном цикле `epoll`.

### Шаг 1: Инициализация и регистрация в `epoll`
В методе `initialize()` необходимо создать сокет, забиндить его на тот же путь и передать дескриптор ядру. Также здесь запрашиваются указатели на ячейки `BlackBoard` для записи результатов.

```cpp
bool MyProvider::initialize(ICoreContext* core) override {
    if (sockfd >= 0) return true; // Защита при Hot-Reload
    m_core = core;

    // Резервируем память в BlackBoard (визуальные плагины будут читать отсюда)
    p_x = core->get_blackboard()->bind_float("myplugin.x");
    p_y = core->get_blackboard()->bind_float("myplugin.y");

    // Создаем сокет для ЧТЕНИЯ
    sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-myplugin");
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    unlink(socket_path.c_str()); // Обязательно удаляем "мертвый" файл прошлого сеанса
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));

    // Интеграция в Wayland Epoll
    core->register_epoll_fd(sockfd, [](uint32_t events, void* user_data) {
        static_cast<MyProvider*>(user_data)->on_data_ready();
    }, this);
    
    return true;
}
```

### Шаг 2: Чтение данных (Zero-Latency Drain Pattern)
Метод `on_data_ready` вызывается ядром Linux только тогда, когда в буфере сокета появляются новые байты. 

**Критическое правило архитектуры:** Вы обязаны "осушить" (drain) сокет в цикле `while`, читая данные до тех пор, пока `recv` не вернет `EWOULDBLOCK` или `EAGAIN`. Если демон спамит пакетами быстрее, чем композитор успевает их читать, в сокете скопится очередь. Цикл гарантирует, что мы отбросим устаревшие пакеты и применим к шейдерам только **самый последний, актуальный пакет**.

```cpp
void MyProvider::on_data_ready() {
    MyDatagram datagram;
    MyDatagram latest_datagram;
    bool has_new_data = false;

    // Цикл "осушения" сокета
    while (true) {
        ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Сокет пуст, мы догнали реальное время
            if (errno == EINTR) continue;
            break;
        }

        // Если пакет цел, перезаписываем старый свежим
        if (bytes_read == sizeof(MyDatagram) && datagram.magic == 0x4D594454) {
            latest_datagram = datagram;
            has_new_data = true;
        }
    }

    // Применяем математику (например, сглаживание) только к финальному пакету
    if (has_new_data) {
        // Прямая запись в BlackBoard (данные мгновенно доступны всем визуальным плагинам)
        *p_x = latest_datagram.value_x * user_multiplier;
        *p_y = latest_datagram.value_y * user_multiplier;
    }
}
```

### Шаг 3: Корректная очистка (Cleanup)
Если пользователь отключит провайдер в `init.lua`, ядро вызовет `cleanup()`. Вы **обязаны** удалить файловый дескриптор из `epoll`, иначе ядро продолжит вызывать коллбэк для закрытого сокета, что приведет к крашу (Use-After-Free).

```cpp
void MyProvider::cleanup() override {
    if (sockfd >= 0) {
        if (m_core) m_core->unregister_epoll_fd(sockfd);
        close(sockfd);
        sockfd = -1;
    }
}
```

---

## 4. Конфигурация провайдера из Lua

Так же, как и визуальные плагины, провайдеры данных могут экспортировать параметры в Lua. Это позволяет пользователям настраивать чувствительность мыши, множители басов или инерцию сглаживания "на лету".

Реализуйте методы интерфейса `IDataProvider`:

```cpp
std::vector<EffectParameter> MyProvider::get_parameters() const override {
    return {
        {"multiplier", "Множитель входящих данных", user_multiplier},
        {"invert_axis", "Инвертировать ось X", user_invert}
    };
}

void MyProvider::set_parameter(const std::string& name, const EffectParameterValue& value) override {
    try {
        if (name == "multiplier") user_multiplier = std::get<float>(value);
        else if (name == "invert_axis") user_invert = std::get<bool>(value);
    } catch (...) {}
}
```
Эти параметры автоматически сгенерируются в конфигурационных файлах `plugins/` при вызове `interactive-wallpaper --init-config`.

### Итог
Разделение сбора данных и их рендеринга обеспечивает микроядерную надежность Shader Desk. Разрабатывая новые провайдеры (например, для парсинга конфигурации `Hyprland`, чтения температуры GPU или получения текста играющей песни по D-Bus), придерживайтесь паттерна *Fire-and-Forget* в демоне и *Zero-Latency Drain* в плагине.