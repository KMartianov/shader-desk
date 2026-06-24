// plugins/audio-provider/audio-provider.cpp

#include "data-provider.hpp"
#include "audio-data.hpp" 
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

/**
 * @class AudioProvider
 * @brief Плагин-поставщик данных. Слушает аудио-телеметрию от внешнего демона (audio-daemon)
 *        по UNIX-сокету и транслирует её в центральную шину памяти (BlackBoard).
 */
class AudioProvider : public IDataProvider {
    // Указатели на ячейки памяти в BlackBoard. 
    // Запись по этим указателям мгновенно обновляет данные для всех визуальных эффектов (Zero-Latency).
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; 

    // Дескриптор UNIX-сокета
    int sockfd = -1;

public:
    bool initialize(ICoreContext* core) override {
        // 1. Резервируем память в BlackBoard и получаем прямые указатели
        p_volume = core->get_blackboard().bind_float("audio.volume");
        p_bass   = core->get_blackboard().bind_float("audio.bass");
        p_mid    = core->get_blackboard().bind_float("audio.mid");
        p_treble = core->get_blackboard().bind_float("audio.treble");
        p_bands  = core->get_blackboard().bind_float_array("audio.bands", 64);

        // 2. Создаем датаграммный (UDP-подобный) сокет.
        // Флаг SOCK_NONBLOCK критически важен, чтобы чтение не заблокировало главный Wayland-цикл (epoll).
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        // 3. НАСТРОЙКА АБСТРАКТНОГО СОКЕТА LINUX
        // В отличие от обычных сокетов, абстрактные сокеты не создают файлов на диске (в /tmp/ и т.д.).
        // Они живут исключительно в памяти ядра и автоматически уничтожаются при закрытии дескриптора.
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const char* socket_name = "shader-desk-audio";
        
        // Магия абстрактного сокета: первый байт пути должен быть нулевым ('\0').
        addr.sun_path[0] = '\0'; 
        strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
        
        // Размер структуры для bind должен вычисляться точно по длине строки после '\0'.
        socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);
        
        if (bind(sockfd, (struct sockaddr*)&addr, addr_len) < 0) {
            close(sockfd);
            return false;
        }

        // 4. Отдаем наш сокет главному циклу Ядра (epoll).
        // Когда демон пришлет данные, epoll мгновенно вызовет on_data_ready().
        core->register_epoll_fd(sockfd, [this](uint32_t) { this->on_data_ready(); });
        
        std::cout << "[Provider] Audio Provider started on abstract socket '@" << socket_name << "'" << std::endl;
        return true;
    }

    /**
     * @brief Коллбэк, вызываемый epoll'ом при поступлении новых данных в сокет.
     */
    void on_data_ready() {
        AudioData datagram;
        
        // БЕЗОПАСНЫЙ ЦИКЛ ЧТЕНИЯ (Draining the buffer)
        // Так как сокет неблокирующий, мы должны вычитывать пакеты в цикле, 
        // пока буфер ядра полностью не опустеет.
        while (true) {
            // MSG_DONTWAIT гарантирует, что мы не зависнем, если данных больше нет
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            
            if (bytes_read < 0) {
                // EAGAIN / EWOULDBLOCK: Нормальная ситуация, буфер пуст, выходим из цикла.
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                // EINTR: Системный вызов был прерван системным сигналом ОС, нужно просто повторить чтение.
                if (errno == EINTR) continue; 
                
                // Иная критическая ошибка сокета
                break;
            }

            // Проверяем целостность пакета (размер и магическое число)
            if (bytes_read == sizeof(AudioData) && datagram.magic == 0x41554431) {
                // Пишем данные напрямую по указателям BlackBoard.
                // В этот же момент все визуальные плагины на всех мониторах "увидят" новые значения.
                *p_volume = datagram.volume;
                *p_bass   = datagram.bass;
                *p_mid    = datagram.mid;
                *p_treble = datagram.treble;
                
                // Копируем массив спектра частот
                std::memcpy(p_bands, datagram.bands, 64 * sizeof(float));
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) close(sockfd); 
        // ВАЖНО: unlink(path) больше не нужен, так как абстрактные сокеты 
        // не оставляют мусорных файлов в файловой системе!
    }
    
    const char* get_name() const override { return "Cava Audio Provider"; }
};

// --- C-ABI ИНТЕРФЕЙС ДЛЯ СИСТЕМЫ ПЛАГИНОВ ---
// Эти функции ищет dlopen() в загрузчике PluginManager.
extern "C" {
    IDataProvider* create_provider() { return new AudioProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}